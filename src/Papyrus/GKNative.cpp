#include "Papyrus/GKNative.h"

#include "State/GameState.h"
#include "State/Labyrinth.h"

// =============================================================================
// Phase 1: actor tracking (roles + status). Resource / labyrinth / orphan
// functions arrive in later phases (see docs/PLAN-V1.md §9).
// =============================================================================

namespace {
    constexpr std::string_view kClass = "GordianKnotNative"sv;

    // Resolve a FormID to a live reference / keyword for return to Papyrus.
    RE::TESObjectREFR* AsRef(RE::FormID a_id) {
        auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
        return form ? form->As<RE::TESObjectREFR>() : nullptr;
    }

    // Resolve a list of actor FormIDs back to live RE::Actor* (dropping any that no
    // longer resolve) for return to Papyrus as an Actor[].
    std::vector<RE::Actor*> ResolveActors(const std::vector<RE::FormID>& a_ids) {
        std::vector<RE::Actor*> out;
        out.reserve(a_ids.size());
        for (const auto id : a_ids) {
            if (auto* form = RE::TESForm::LookupByID(id)) {
                if (auto* actor = form->As<RE::Actor>()) {
                    out.push_back(actor);
                }
            }
        }
        return out;
    }

    // --- roles ----------------------------------------------------------------

    void SetActorRoles(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_roles) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Actors().SetRoles(a_actor->GetFormID(), static_cast<std::uint32_t>(a_roles));
    }

    void AddActorRole(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_role) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Actors().AddRole(a_actor->GetFormID(), static_cast<std::uint32_t>(a_role));
    }

    void RemoveActorRole(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_role) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Actors().RemoveRole(a_actor->GetFormID(), static_cast<std::uint32_t>(a_role));
    }

    std::int32_t GetActorRoles(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return GK::Role::kNone;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return static_cast<std::int32_t>(state->Actors().GetRoles(a_actor->GetFormID()));
    }

    bool IsWanderer(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return (state->Actors().GetRoles(a_actor->GetFormID()) & GK::Role::kWanderer) != 0;
    }

    bool IsWarden(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return (state->Actors().GetRoles(a_actor->GetFormID()) & GK::Role::kWarden) != 0;
    }

    // --- status ---------------------------------------------------------------

    void SetActorStatus(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_status) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Actors().SetStatus(a_actor->GetFormID(), a_status);
    }

    std::int32_t GetActorStatus(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return GK::Status::kIdle;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Actors().GetStatus(a_actor->GetFormID());
    }

    // --- queries --------------------------------------------------------------

    std::vector<RE::Actor*> GetActorsByRole(RE::StaticFunctionTag*, std::int32_t a_roleMask) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return ResolveActors(state->Actors().GetByRole(static_cast<std::uint32_t>(a_roleMask)));
    }

    std::vector<RE::Actor*> GetActorsByStatus(RE::StaticFunctionTag*, std::int32_t a_status) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return ResolveActors(state->Actors().GetByStatus(a_status));
    }

    void ForgetActor(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Actors().Forget(a_actor->GetFormID());
    }

    // --- config / lifecycle ---------------------------------------------------

    void ConfigureKeywords(RE::StaticFunctionTag*, RE::BGSKeyword* a_cellDoor, RE::BGSKeyword* a_patrolMarker,
                           RE::BGSKeyword* a_furniture, RE::BGSKeyword* a_inMarker, RE::BGSKeyword* a_outMarker) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        auto& kw = state->Keywords();
        kw.cellDoor = a_cellDoor;
        kw.patrolMarker = a_patrolMarker;
        kw.furniture = a_furniture;
        kw.inMarker = a_inMarker;
        kw.outMarker = a_outMarker;
        if (kw.Valid()) {
            logger::info("GKNative: resource keywords configured.");
        } else {
            logger::warn("GKNative: ConfigureKeywords received one or more null keywords.");
        }
    }

    void RegisterLabyrinth(RE::StaticFunctionTag*, RE::TESObjectREFR* a_anchor) {
        if (!a_anchor) {
            logger::warn("GKNative: RegisterLabyrinth ignored (null anchor).");
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Labyrinths().Register(a_anchor->GetFormID());
        logger::info("GKNative: registered labyrinth (anchor {:08X}).", a_anchor->GetFormID());
    }

    std::int32_t ScanAllLabyrinths(RE::StaticFunctionTag*) {
        // Global one-shot sweep across all registered labyrinths; finds persistent
        // resources without their cells being loaded. GK::ScanAllForms locks itself.
        return GK::ScanAllForms();
    }

    // --- cells ----------------------------------------------------------------

    RE::TESObjectREFR* GetCellDoor(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? AsRef(cell->door) : nullptr;
    }

    RE::TESObjectREFR* GetCellInMarker(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? AsRef(cell->inMarker) : nullptr;
    }

    RE::TESObjectREFR* GetCellOutMarker(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? AsRef(cell->outMarker) : nullptr;
    }

    std::int32_t GetCellMaxOccupants(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? static_cast<std::int32_t>(cell->maxOccupants) : 0;
    }

    void SetCellMaxOccupants(RE::StaticFunctionTag*, std::int32_t a_cell, std::int32_t a_max) {
        if (a_max < 0) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (auto* cell = state->Resources().CellPool().FindByHandle(a_cell)) {
            cell->maxOccupants = static_cast<std::uint32_t>(a_max);
        }
    }

    RE::TESObjectREFR* GetCellLabyrinth(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? AsRef(cell->labyrinth) : nullptr;
    }

    std::vector<std::int32_t> GetCells(RE::StaticFunctionTag*, RE::TESObjectREFR* a_labyrinth) {
        if (!a_labyrinth) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Resources().CellPool().HandlesInLabyrinth(a_labyrinth->GetFormID());
    }

    // --- markers --------------------------------------------------------------

    RE::TESObjectREFR* GetMarkerRef(RE::StaticFunctionTag*, std::int32_t a_marker) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* marker = state->Resources().MarkerPool().FindByHandle(a_marker);
        return marker ? AsRef(marker->ref) : nullptr;
    }

    std::int32_t GetMarkerMaxOccupants(RE::StaticFunctionTag*, std::int32_t a_marker) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* marker = state->Resources().MarkerPool().FindByHandle(a_marker);
        return marker ? static_cast<std::int32_t>(marker->maxOccupants) : 0;
    }

    void SetMarkerMaxOccupants(RE::StaticFunctionTag*, std::int32_t a_marker, std::int32_t a_max) {
        if (a_max < 0) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (auto* marker = state->Resources().MarkerPool().FindByHandle(a_marker)) {
            marker->maxOccupants = static_cast<std::uint32_t>(a_max);
        }
    }

    RE::TESObjectREFR* GetMarkerLabyrinth(RE::StaticFunctionTag*, std::int32_t a_marker) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* marker = state->Resources().MarkerPool().FindByHandle(a_marker);
        return marker ? AsRef(marker->labyrinth) : nullptr;
    }

    std::vector<std::int32_t> GetMarkers(RE::StaticFunctionTag*, RE::TESObjectREFR* a_labyrinth) {
        if (!a_labyrinth) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Resources().MarkerPool().HandlesInLabyrinth(a_labyrinth->GetFormID());
    }

    // --- furniture ------------------------------------------------------------

    RE::TESObjectREFR* GetFurnitureRef(RE::StaticFunctionTag*, std::int32_t a_furniture) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* furniture = state->Resources().FurniturePool().FindByHandle(a_furniture);
        return furniture ? AsRef(furniture->ref) : nullptr;
    }

    RE::TESObjectREFR* GetFurnitureLabyrinth(RE::StaticFunctionTag*, std::int32_t a_furniture) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* furniture = state->Resources().FurniturePool().FindByHandle(a_furniture);
        return furniture ? AsRef(furniture->labyrinth) : nullptr;
    }

    std::vector<std::int32_t> GetFurnitures(RE::StaticFunctionTag*, RE::TESObjectREFR* a_labyrinth) {
        if (!a_labyrinth) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Resources().FurniturePool().HandlesInLabyrinth(a_labyrinth->GetFormID());
    }
}

namespace GK::Papyrus {
    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            logger::error("GKNative: null VM; skipping Papyrus registration.");
            return false;
        }

        a_vm->RegisterFunction("SetActorRoles", kClass, SetActorRoles);
        a_vm->RegisterFunction("AddActorRole", kClass, AddActorRole);
        a_vm->RegisterFunction("RemoveActorRole", kClass, RemoveActorRole);
        a_vm->RegisterFunction("GetActorRoles", kClass, GetActorRoles);
        a_vm->RegisterFunction("IsWanderer", kClass, IsWanderer);
        a_vm->RegisterFunction("IsWarden", kClass, IsWarden);

        a_vm->RegisterFunction("SetActorStatus", kClass, SetActorStatus);
        a_vm->RegisterFunction("GetActorStatus", kClass, GetActorStatus);

        a_vm->RegisterFunction("GetActorsByRole", kClass, GetActorsByRole);
        a_vm->RegisterFunction("GetActorsByStatus", kClass, GetActorsByStatus);
        a_vm->RegisterFunction("ForgetActor", kClass, ForgetActor);

        a_vm->RegisterFunction("ConfigureKeywords", kClass, ConfigureKeywords);
        a_vm->RegisterFunction("RegisterLabyrinth", kClass, RegisterLabyrinth);
        a_vm->RegisterFunction("ScanAllLabyrinths", kClass, ScanAllLabyrinths);

        a_vm->RegisterFunction("GetCellDoor", kClass, GetCellDoor);
        a_vm->RegisterFunction("GetCellInMarker", kClass, GetCellInMarker);
        a_vm->RegisterFunction("GetCellOutMarker", kClass, GetCellOutMarker);
        a_vm->RegisterFunction("GetCellMaxOccupants", kClass, GetCellMaxOccupants);
        a_vm->RegisterFunction("SetCellMaxOccupants", kClass, SetCellMaxOccupants);
        a_vm->RegisterFunction("GetCellLabyrinth", kClass, GetCellLabyrinth);
        a_vm->RegisterFunction("GetCells", kClass, GetCells);

        a_vm->RegisterFunction("GetMarkerRef", kClass, GetMarkerRef);
        a_vm->RegisterFunction("GetMarkerMaxOccupants", kClass, GetMarkerMaxOccupants);
        a_vm->RegisterFunction("SetMarkerMaxOccupants", kClass, SetMarkerMaxOccupants);
        a_vm->RegisterFunction("GetMarkerLabyrinth", kClass, GetMarkerLabyrinth);
        a_vm->RegisterFunction("GetMarkers", kClass, GetMarkers);

        a_vm->RegisterFunction("GetFurnitureRef", kClass, GetFurnitureRef);
        a_vm->RegisterFunction("GetFurnitureLabyrinth", kClass, GetFurnitureLabyrinth);
        a_vm->RegisterFunction("GetFurnitures", kClass, GetFurnitures);

        logger::info("GKNative: registered actor + labyrinth/resource Papyrus functions.");
        return true;
    }
}
