#include "Papyrus/GKNative.h"

#include "State/GameState.h"

// =============================================================================
// Phase 1: actor tracking (roles + status). Resource / labyrinth / orphan
// functions arrive in later phases (see docs/PLAN-V1.md §9).
// =============================================================================

namespace {
    constexpr std::string_view kClass = "GKNative"sv;

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

        logger::info("GKNative: registered actor Papyrus functions.");
        return true;
    }
}
