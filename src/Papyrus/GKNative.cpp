#include "Papyrus/GKNative.h"

#include "State/AliasPool.h"
#include "State/GameState.h"
#include "State/Labyrinth.h"
#include "State/RoleFactions.h"

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

    // Resolve a list of FormIDs back to live references (dropping any that no longer
    // resolve) for return to Papyrus as an ObjectReference[] (e.g. labyrinth anchors).
    std::vector<RE::TESObjectREFR*> ResolveRefs(const std::vector<RE::FormID>& a_ids) {
        std::vector<RE::TESObjectREFR*> out;
        out.reserve(a_ids.size());
        for (const auto id : a_ids) {
            if (auto* ref = AsRef(id)) {
                out.push_back(ref);
            }
        }
        return out;
    }

    // --- tracking + roles -----------------------------------------------------
    // Roles come in two kinds (see GK::Role): GLOBAL (Wanderer) live on the actor
    // and take no labyrinth; SCOPED (Warden/Prisoner) are an association with one
    // labyrinth (its anchor). The per-labyrinth generic functions below operate on
    // SCOPED bits only -- any global bit in the passed mask is ignored (use the
    // dedicated Wanderer functions for that). ADDING mutators track the actor first
    // (via AliasPool::EnsureTrackable -- see State/AliasPool.h) and return false if
    // no pool alias could take it; CLEARING mutators never track -- clearing on an
    // untracked actor is a no-op.

    bool AddActor(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (!GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return false;
        }
        state->Actors().AddActor(a_actor->GetFormID());
        return true;
    }

    bool SetActorRoles(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth,
                       std::int32_t a_roles) {
        if (!a_actor || !a_labyrinth) {
            return false;
        }
        const auto masked = static_cast<std::uint32_t>(a_roles) & GK::Role::kScopedMask;
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto id = a_actor->GetFormID();
        const auto labID = a_labyrinth->GetFormID();
        if (masked != GK::Role::kNone && !GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return false;  // a pure clear (masked == 0) never tracks and always "succeeds"
        }
        const auto old = state->Actors().GetRoles(id, labID);
        state->Actors().SetRoles(id, labID, masked);
        GK::RoleFactions::Apply(*state, *a_actor, labID, masked & ~old, old & ~masked);
        return true;
    }

    bool AddActorRole(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth, std::int32_t a_role) {
        if (!a_actor || !a_labyrinth) {
            return false;
        }
        const auto masked = static_cast<std::uint32_t>(a_role) & GK::Role::kScopedMask;
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (!GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return false;
        }
        const auto id = a_actor->GetFormID();
        const auto labID = a_labyrinth->GetFormID();
        const auto old = state->Actors().GetRoles(id, labID);
        state->Actors().AddRole(id, labID, masked);
        GK::RoleFactions::Apply(*state, *a_actor, labID, masked & ~old, 0);
        return true;
    }

    void RemoveActorRole(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth,
                         std::int32_t a_role) {
        if (!a_actor || !a_labyrinth) {
            return;
        }
        const auto masked = static_cast<std::uint32_t>(a_role) & GK::Role::kScopedMask;
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto id = a_actor->GetFormID();
        const auto labID = a_labyrinth->GetFormID();
        const auto old = state->Actors().GetRoles(id, labID);
        state->Actors().RemoveRole(id, labID, masked);
        GK::RoleFactions::Apply(*state, *a_actor, labID, 0, old & masked);
    }

    std::int32_t GetActorRoles(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth) {
        if (!a_actor || !a_labyrinth) {
            return GK::Role::kNone;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return static_cast<std::int32_t>(state->Actors().GetRoles(a_actor->GetFormID(), a_labyrinth->GetFormID()));
    }

    // The actor's global (non-labyrinth) role mask, e.g. Wanderer.
    std::int32_t GetActorGlobalRoles(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return GK::Role::kNone;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return static_cast<std::int32_t>(state->Actors().GetGlobalRoles(a_actor->GetFormID()));
    }

    // Shared body for the scoped Is<Role> tests (Warden/Prisoner in one labyrinth).
    bool HasRoleIn(RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth, std::uint32_t a_flag) {
        if (!a_actor || !a_labyrinth) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return (state->Actors().GetRoles(a_actor->GetFormID(), a_labyrinth->GetFormID()) & a_flag) != 0;
    }

    bool IsWanderer(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return (state->Actors().GetGlobalRoles(a_actor->GetFormID()) & GK::Role::kWanderer) != 0;
    }
    bool IsWarden(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth) {
        return HasRoleIn(a_actor, a_labyrinth, GK::Role::kWarden);
    }
    bool IsPrisoner(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth) {
        return HasRoleIn(a_actor, a_labyrinth, GK::Role::kPrisoner);
    }

    // Track a_actor (pool gate), OR a scoped role flag in for one labyrinth, and
    // sync factions. Shared body of SetWarden/SetPrisoner/Capture. Caller holds
    // the GameState lock.
    bool AddScopedRole(GK::GameState& a_state, RE::Actor& a_actor, RE::FormID a_labID, std::uint32_t a_flag) {
        if (!GK::AliasPool::EnsureTrackable(a_state, a_actor)) {
            return false;
        }
        const auto id = a_actor.GetFormID();
        const auto old = a_state.Actors().GetRoles(id, a_labID);
        a_state.Actors().AddRole(id, a_labID, a_flag);
        GK::RoleFactions::Apply(a_state, a_actor, a_labID, a_flag & ~old, 0);
        return true;
    }

    // Per-role set/clear convenience wrappers. Scoped variants take the labyrinth.
    bool SetRoleFlag(RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth, std::uint32_t a_flag) {
        if (!a_actor || !a_labyrinth) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return AddScopedRole(*state, *a_actor, a_labyrinth->GetFormID(), a_flag);
    }

    void ClearRoleFlag(RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth, std::uint32_t a_flag) {
        if (!a_actor || !a_labyrinth) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto id = a_actor->GetFormID();
        const auto labID = a_labyrinth->GetFormID();
        const auto old = state->Actors().GetRoles(id, labID);
        state->Actors().RemoveRole(id, labID, a_flag);
        GK::RoleFactions::Apply(*state, *a_actor, labID, 0, old & a_flag);
    }

    // Wanderer is global -> no labyrinth.
    bool SetWanderer(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (!GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return false;
        }
        state->Actors().AddGlobalRole(a_actor->GetFormID(), GK::Role::kWanderer);
        return true;
    }
    void ClearWanderer(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Actors().RemoveGlobalRole(a_actor->GetFormID(), GK::Role::kWanderer);
    }
    bool SetWarden(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth) {
        return SetRoleFlag(a_actor, a_labyrinth, GK::Role::kWarden);
    }
    void ClearWarden(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth) {
        ClearRoleFlag(a_actor, a_labyrinth, GK::Role::kWarden);
    }
    bool SetPrisoner(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth) {
        return SetRoleFlag(a_actor, a_labyrinth, GK::Role::kPrisoner);
    }
    void ClearPrisoner(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth) {
        ClearRoleFlag(a_actor, a_labyrinth, GK::Role::kPrisoner);
    }

    // Capture: imprison a_actor in the labyrinth a_warden keeps -- resolves the
    // labyrinth from a_warden's Warden role, then behaves exactly like SetPrisoner
    // on it (pool gate + faction sync). One atomic native call, so the lookup and
    // the mutation can't interleave with another thread.
    bool Capture(RE::StaticFunctionTag*, RE::Actor* a_warden, RE::Actor* a_actor) {
        if (!a_warden || !a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto labs = state->Actors().GetLabyrinthsByRole(a_warden->GetFormID(), GK::Role::kWarden);
        if (labs.empty()) {
            logger::warn("GKNative: Capture failed (actor {:08X} is warden of no labyrinth).", a_warden->GetFormID());
            return false;
        }
        if (labs.size() > 1) {
            logger::warn("GKNative: Capture: warden {:08X} keeps {} labyrinths; using {:08X}.", a_warden->GetFormID(),
                         labs.size(), labs.front());
        }
        return AddScopedRole(*state, *a_actor, labs.front(), GK::Role::kPrisoner);
    }

    // Role-flag constants (single source of truth mirrored from GK::Role). Papyrus
    // has no true constants on a Hidden global script, so we expose them as getters.
    std::int32_t RoleWanderer(RE::StaticFunctionTag*) { return static_cast<std::int32_t>(GK::Role::kWanderer); }
    std::int32_t RoleWarden(RE::StaticFunctionTag*) { return static_cast<std::int32_t>(GK::Role::kWarden); }
    std::int32_t RolePrisoner(RE::StaticFunctionTag*) { return static_cast<std::int32_t>(GK::Role::kPrisoner); }

    // --- status ---------------------------------------------------------------

    bool SetActorStatus(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_status) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (!GK::AliasPool::EnsureTrackable(*state, *a_actor)) {  // status on an untracked actor is an adder
            return false;
        }
        state->Actors().SetStatus(a_actor->GetFormID(), a_status);
        return true;
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

    std::vector<RE::Actor*> GetActorsByRole(RE::StaticFunctionTag*, RE::TESObjectREFR* a_labyrinth,
                                            std::int32_t a_roleMask) {
        if (!a_labyrinth) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return ResolveActors(
            state->Actors().GetByRole(a_labyrinth->GetFormID(), static_cast<std::uint32_t>(a_roleMask)));
    }

    // Tracked actors whose GLOBAL role mask matches ANY bit in a_roleMask (e.g. all
    // Wanderers). Global roles aren't tied to a labyrinth, so this takes none.
    std::vector<RE::Actor*> GetActorsByGlobalRole(RE::StaticFunctionTag*, std::int32_t a_roleMask) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return ResolveActors(state->Actors().GetByGlobalRole(static_cast<std::uint32_t>(a_roleMask)));
    }

    // Labyrinths (anchor refs) in which the actor holds ANY scoped role.
    std::vector<RE::TESObjectREFR*> GetActorLabyrinths(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return ResolveRefs(state->Actors().GetLabyrinths(a_actor->GetFormID()));
    }

    // Labyrinths (anchor refs) where the actor's role mask intersects a_roleMask.
    std::vector<RE::TESObjectREFR*> GetLabyrinthsByActorRole(RE::StaticFunctionTag*, RE::Actor* a_actor,
                                                             std::int32_t a_roleMask) {
        if (!a_actor) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return ResolveRefs(
            state->Actors().GetLabyrinthsByRole(a_actor->GetFormID(), static_cast<std::uint32_t>(a_roleMask)));
    }

    // True if the actor holds any role intersecting a_roleMask in ANY labyrinth.
    bool HasRoleAnywhere(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_roleMask) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Actors().HasRoleAnywhere(a_actor->GetFormID(), static_cast<std::uint32_t>(a_roleMask));
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
        const auto id = a_actor->GetFormID();
        // Snapshot the scoped roles, then forget, then sync factions: with the
        // records gone, no remaining role can keep a shared faction alive.
        std::vector<std::pair<RE::FormID, std::uint32_t>> held;
        for (const auto lab : state->Actors().GetLabyrinths(id)) {
            held.emplace_back(lab, state->Actors().GetRoles(id, lab));
        }
        state->Actors().Forget(id);
        for (const auto& [lab, roles] : held) {
            GK::RoleFactions::Apply(*state, *a_actor, lab, 0, roles);
        }
        // Release the actor's pool alias (if it holds one) so the slot frees up;
        // this also fires the OnGKRelease destructor hook on the driver script.
        GK::AliasPool::Release(*state, *a_actor);
    }

    // --- config / lifecycle ---------------------------------------------------

    void ConfigureKeywords(RE::StaticFunctionTag*, RE::BGSKeyword* a_cellDoor, RE::BGSKeyword* a_patrolMarker,
                           RE::BGSKeyword* a_furniture, RE::BGSKeyword* a_inMarker, RE::BGSKeyword* a_outMarker,
                           RE::BGSKeyword* a_warden, RE::BGSKeyword* a_wanderer) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        auto& kw = state->Keywords();
        kw.cellDoor = a_cellDoor;
        kw.patrolMarker = a_patrolMarker;
        kw.furniture = a_furniture;
        kw.inMarker = a_inMarker;
        kw.outMarker = a_outMarker;
        kw.warden = a_warden;
        kw.wanderer = a_wanderer;
        if (kw.Valid()) {
            logger::info("GKNative: resource keywords configured.");
        } else {
            logger::warn("GKNative: ConfigureKeywords received one or more null keywords.");
        }
    }

    void RegisterLabyrinth(RE::StaticFunctionTag*, RE::TESObjectREFR* a_anchor, RE::TESFaction* a_wardenFaction,
                           RE::TESFaction* a_prisonerFaction) {
        if (!a_anchor) {
            logger::warn("GKNative: RegisterLabyrinth ignored (null anchor).");
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Labyrinths().Register(a_anchor->GetFormID(), a_wardenFaction, a_prisonerFaction);
        logger::info("GKNative: registered labyrinth (anchor {:08X}, wardenFaction {:08X}, prisonerFaction {:08X}).",
                     a_anchor->GetFormID(), a_wardenFaction ? a_wardenFaction->GetFormID() : 0,
                     a_prisonerFaction ? a_prisonerFaction->GetFormID() : 0);
    }

    std::int32_t ScanAllLabyrinths(RE::StaticFunctionTag*) {
        // Global one-shot sweep across all registered labyrinths; finds persistent
        // resources without their cells being loaded. GK::ScanAllForms locks itself.
        return GK::ScanAllForms();
    }

    // True if akRef is a persistent reference (kPersistent form flag). Persistent refs
    // survive cell reset, are simulated off-screen, and are the only ones ScanAllForms
    // finds while their cell is unloaded. Diagnostic: no game state, no lock needed.
    bool IsPersistent(RE::StaticFunctionTag*, RE::TESObjectREFR* a_ref) {
        return a_ref && (a_ref->GetFormFlags() & RE::TESObjectREFR::RecordFlags::kPersistent) != 0;
    }

    // --- alias pool (Papyrus-facing; the allocation itself is EnsureTrackable) ---

    // Supply the quest carrying the GkNpc pool aliases. Session config like
    // ConfigureKeywords: a live pointer, re-supplied by Papyrus after each load.
    void ConfigureAliasQuest(RE::StaticFunctionTag*, RE::TESQuest* a_quest) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->AliasQuest() = a_quest;
        if (a_quest) {
            logger::info("GKNative: alias pool quest configured ({:08X}).", a_quest->GetFormID());
        } else {
            logger::warn("GKNative: ConfigureAliasQuest received None; pool disabled.");
        }
    }

    std::int32_t FindAliasHolding(RE::StaticFunctionTag*, RE::TESQuest* a_quest, RE::Actor* a_actor) {
        if (!a_quest || !a_actor) {
            return -1;
        }
        const auto* alias = GK::AliasPool::FindHolding(*a_quest, *a_actor);
        return alias ? static_cast<std::int32_t>(alias->aliasID) : -1;
    }

    std::int32_t CountFreeAliases(RE::StaticFunctionTag*, RE::TESQuest* a_quest) {
        if (!a_quest) {
            return 0;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return GK::AliasPool::CountFree(*state, *a_quest);
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

        a_vm->RegisterFunction("AddActor", kClass, AddActor);
        a_vm->RegisterFunction("SetActorRoles", kClass, SetActorRoles);
        a_vm->RegisterFunction("AddActorRole", kClass, AddActorRole);
        a_vm->RegisterFunction("RemoveActorRole", kClass, RemoveActorRole);
        a_vm->RegisterFunction("GetActorRoles", kClass, GetActorRoles);
        a_vm->RegisterFunction("GetActorGlobalRoles", kClass, GetActorGlobalRoles);
        a_vm->RegisterFunction("IsWanderer", kClass, IsWanderer);
        a_vm->RegisterFunction("IsWarden", kClass, IsWarden);
        a_vm->RegisterFunction("IsPrisoner", kClass, IsPrisoner);

        a_vm->RegisterFunction("SetWanderer", kClass, SetWanderer);
        a_vm->RegisterFunction("ClearWanderer", kClass, ClearWanderer);
        a_vm->RegisterFunction("SetWarden", kClass, SetWarden);
        a_vm->RegisterFunction("ClearWarden", kClass, ClearWarden);
        a_vm->RegisterFunction("SetPrisoner", kClass, SetPrisoner);
        a_vm->RegisterFunction("ClearPrisoner", kClass, ClearPrisoner);
        a_vm->RegisterFunction("Capture", kClass, Capture);

        a_vm->RegisterFunction("RoleWanderer", kClass, RoleWanderer);
        a_vm->RegisterFunction("RoleWarden", kClass, RoleWarden);
        a_vm->RegisterFunction("RolePrisoner", kClass, RolePrisoner);

        a_vm->RegisterFunction("SetActorStatus", kClass, SetActorStatus);
        a_vm->RegisterFunction("GetActorStatus", kClass, GetActorStatus);

        a_vm->RegisterFunction("GetActorsByRole", kClass, GetActorsByRole);
        a_vm->RegisterFunction("GetActorsByGlobalRole", kClass, GetActorsByGlobalRole);
        a_vm->RegisterFunction("GetActorLabyrinths", kClass, GetActorLabyrinths);
        a_vm->RegisterFunction("GetLabyrinthsByActorRole", kClass, GetLabyrinthsByActorRole);
        a_vm->RegisterFunction("HasRoleAnywhere", kClass, HasRoleAnywhere);
        a_vm->RegisterFunction("GetActorsByStatus", kClass, GetActorsByStatus);
        a_vm->RegisterFunction("ForgetActor", kClass, ForgetActor);

        a_vm->RegisterFunction("ConfigureKeywords", kClass, ConfigureKeywords);
        a_vm->RegisterFunction("RegisterLabyrinth", kClass, RegisterLabyrinth);
        a_vm->RegisterFunction("ScanAllLabyrinths", kClass, ScanAllLabyrinths);
        a_vm->RegisterFunction("IsPersistent", kClass, IsPersistent);

        a_vm->RegisterFunction("ConfigureAliasQuest", kClass, ConfigureAliasQuest);
        a_vm->RegisterFunction("FindAliasHolding", kClass, FindAliasHolding);
        a_vm->RegisterFunction("CountFreeAliases", kClass, CountFreeAliases);

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
