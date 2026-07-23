#include "Papyrus/GKNative.h"

#include "State/AliasPool.h"
#include "State/CaseFold.h"
#include "State/GameState.h"
#include "State/Labyrinth.h"
#include "State/RoleFactions.h"

#include <random>

#include <Windows.h>  // GetModuleHandle/GetProcAddress: DD NG's optional exported API

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

    // Shared session RNG for the random picks (assigners, animation registries).
    // Seeded once; callers hold the GameState lock, which also serializes it.
    std::mt19937& Rng() {
        static std::mt19937 rng{std::random_device{}()};
        return rng;
    }

    // Uniform random index into a non-empty range.
    std::size_t RandomIndex(std::size_t a_count) {
        return std::uniform_int_distribution<std::size_t>{0, a_count - 1}(Rng());
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

    // Shared body for the scoped Is<Role> tests (Warden/Prisoner). a_labyrinth =
    // nullptr means "in ANY labyrinth" (same convention as GetActorsByRole).
    bool HasRoleIn(RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth, std::uint32_t a_flag) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (!a_labyrinth) {
            return state->Actors().HasRoleAnywhere(a_actor->GetFormID(), a_flag);
        }
        return (state->Actors().GetRoles(a_actor->GetFormID(), a_labyrinth->GetFormID()) & a_flag) != 0;
    }

    // True when a_rec holds any bit of a_mask "for" labyrinth a_labID: global
    // bits (Wanderer) match regardless of the labyrinth; scoped bits match in
    // a_labID, or in ANY labyrinth when a_labID is 0 (the None convention, as in
    // IsWarden/IsPrisoner). Pure record test -- caller holds the GameState lock.
    bool RecordHasRoleForLab(const GK::ActorRecord& a_rec, RE::FormID a_labID, std::uint32_t a_mask) {
        if ((a_rec.globalRoles & a_mask) != 0) {
            return true;
        }
        if ((a_mask & GK::Role::kScopedMask) == 0) {
            return false;
        }
        if (a_labID) {
            const auto it = a_rec.rolesByLab.find(a_labID);
            return it != a_rec.rolesByLab.end() && (it->second & a_mask) != 0;
        }
        for (const auto& [lab, roles] : a_rec.rolesByLab) {
            if ((roles & a_mask) != 0) {
                return true;
            }
        }
        return false;
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
    // on it (pool gate + faction sync). Fails without effect if a_actor is already
    // a prisoner ANYWHERE (a prisoner belongs to one labyrinth; two wardens racing
    // for the same actor -> exactly one wins). One atomic native call, so the
    // lookups and the mutation can't interleave with another thread.
    bool Capture(RE::StaticFunctionTag*, RE::Actor* a_warden, RE::Actor* a_actor) {
        if (!a_warden || !a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (state->Actors().HasRoleAnywhere(a_actor->GetFormID(), GK::Role::kPrisoner)) {
            logger::info("GKNative: Capture rejected (actor {:08X} is already a prisoner).", a_actor->GetFormID());
            return false;
        }
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
    // Status is an opaque, Papyrus-owned String vocabulary, global to the actor.
    // "idle" is the default for newly tracked actors; an empty String is treated
    // as "idle" everywhere. Compared case-insensitively (like Papyrus string
    // compares) but stored as set, so GetActorStatus reads back with the caller's
    // casing.

    bool SetActorStatus(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_status) {
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

    std::string GetActorStatus(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return std::string(GK::Status::kIdle);
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Actors().GetStatus(a_actor->GetFormID());
    }

    void ClearActorStatus(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Actors().ClearStatus(a_actor->GetFormID());
    }

    bool IsActorStatus(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_status) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return GK::FoldCase(state->Actors().GetStatus(a_actor->GetFormID())) ==
               GK::FoldCase(GK::Status::Normalize(a_status));
    }

    bool IsActorIdle(RE::StaticFunctionTag* a_tag, RE::Actor* a_actor) {
        return IsActorStatus(a_tag, a_actor, GK::Status::kIdle);
    }

    // Shared claim gate of the atomic idle-claim functions: the tracked record
    // (a_id, a_rec) is claimable when the status is idle, it isn't the player,
    // and it resolves to a live actor that is calm -- alive, not in combat, and
    // not searching for an enemy (suspicious). Returns the actor, or nullptr.
    // The combat gate is why this lives natively: IsInCombat covers active
    // combat; the kSearchingInCombat bool bit covers the "searching for an
    // enemy" (suspicious) sub-state, which IsInCombat alone can miss.
    RE::Actor* AsClaimableIdle(RE::FormID a_id, const GK::ActorRecord& a_rec) {
        if (a_id == 0x14 || GK::FoldCase(a_rec.status) != GK::Status::kIdle) {
            return nullptr;
        }
        auto* form = RE::TESForm::LookupByID(a_id);
        auto* actor = form ? form->As<RE::Actor>() : nullptr;
        if (!actor || actor->IsDead()) {
            return nullptr;
        }
        if (actor->IsInCombat() ||
            actor->GetActorRuntimeData().boolBits.all(RE::Actor::BOOL_BITS::kSearchingInCombat)) {
            return nullptr;
        }
        return actor;
    }

    // Atomically claim an idle actor: find a tracked actor whose status is idle
    // and who is calm (see AsClaimableIdle), set its status to a_newStatus, and
    // return it. Find + check + transition all happen under the one GameState
    // lock, so two Papyrus threads can never claim the same actor. The player is
    // never returned.
    RE::Actor* GetIdleActorAndTransitionTo(RE::StaticFunctionTag*, std::string_view a_newStatus) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        for (const auto& [id, rec] : state->Actors().Records()) {
            if (auto* actor = AsClaimableIdle(id, rec)) {
                state->Actors().SetStatus(id, a_newStatus);
                return actor;
            }
        }
        return nullptr;
    }

    // --- actor queues -----------------------------------------------------------
    // Named FIFO queues: any String mints a queue on first use, so plugins define
    // their own without enums or int mappings (names are case-insensitive, like
    // Papyrus string compares). Queues are an independent utility: they don't
    // track the actor, touch roles, or take a pool alias, and they persist in the
    // co-save (see Serialization.cpp, QUEU).

    bool EnqueueActor(RE::StaticFunctionTag*, std::string_view a_queue, RE::Actor* a_actor, float a_delaySeconds) {
        if (!a_actor || a_queue.empty()) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto now = GK::NowSeconds();
        state->Queues().PromoteDue(now);
        if (a_delaySeconds > 0.0f) {
            return state->Queues().EnqueueAfter(a_queue, a_actor->GetFormID(), a_delaySeconds, now);
        }
        return state->Queues().Enqueue(a_queue, a_actor->GetFormID());
    }

    // Pop the front of a_queue, dropping entries that no longer resolve to an
    // Actor (deleted / plugin removed mid-queue) so the next live actor comes
    // out. Promotes due delayed enqueues first, so a ripe delayed actor can be
    // the one popped. Caller must hold the GameState lock.
    RE::Actor* DequeueLiveActor(GK::GameState& a_state, std::string_view a_queue) {
        a_state.Queues().PromoteDue(GK::NowSeconds());
        while (const auto id = a_state.Queues().Dequeue(a_queue)) {
            auto* form = RE::TESForm::LookupByID(id);
            if (auto* actor = form ? form->As<RE::Actor>() : nullptr) {
                return actor;
            }
        }
        return nullptr;
    }

    // No actor given: pop the front live entry. Actor given: remove THAT actor
    // from a_queue -- including a still-scheduled delayed entry, like ClearQueue --
    // and return it if it was there, None otherwise.
    RE::Actor* DequeueActor(RE::StaticFunctionTag*, std::string_view a_queue, RE::Actor* a_actor) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (a_actor) {
            state->Queues().PromoteDue(GK::NowSeconds());
            return state->Queues().Remove(a_queue, a_actor->GetFormID()) ? a_actor : nullptr;
        }
        return DequeueLiveActor(*state, a_queue);
    }

    // Front live actor of a_queue WITHOUT removing it (None if empty). Stale
    // entries in front of it are dropped, exactly as a dequeue would, so what
    // Peek returns is what the next DequeueActor pops.
    RE::Actor* PeekActor(RE::StaticFunctionTag*, std::string_view a_queue) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        auto* actor = DequeueLiveActor(*state, a_queue);
        if (actor) {
            state->Queues().PushFront(a_queue, actor->GetFormID());  // put the peeked entry back
        }
        return actor;
    }

    // Last live actor of a_queue WITHOUT removing it (None if empty): the back
    // mirror of PeekActor. Stale entries behind it are dropped the same way a
    // dequeue drops stale front entries. Promotes due delayed enqueues first,
    // so a ripe delayed actor can be the one peeked.
    RE::Actor* PeekLastActor(RE::StaticFunctionTag*, std::string_view a_queue) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Queues().PromoteDue(GK::NowSeconds());
        while (const auto id = state->Queues().PopBack(a_queue)) {
            auto* form = RE::TESForm::LookupByID(id);
            if (auto* actor = form ? form->As<RE::Actor>() : nullptr) {
                state->Queues().Enqueue(a_queue, id);  // put the peeked entry back at the back
                return actor;
            }
        }
        return nullptr;
    }

    // The a_index-th LIVE actor of a_queue counting from the front, dropping
    // stale entries as they are encountered (nullptr once past the end), so
    // indices always range over live actors. Caller must hold the GameState
    // lock and have promoted due enqueues.
    RE::Actor* NthLiveActor(GK::GameState& a_state, std::string_view a_queue, std::int32_t a_index) {
        if (a_index < 0) {
            return nullptr;
        }
        std::int32_t live = 0;
        std::size_t pos = 0;
        while (const auto id = a_state.Queues().At(a_queue, pos)) {
            auto* form = RE::TESForm::LookupByID(id);
            auto* actor = form ? form->As<RE::Actor>() : nullptr;
            if (!actor) {
                a_state.Queues().Remove(a_queue, id);  // drop the stale entry; re-test the same slot
                continue;
            }
            if (live == a_index) {
                return actor;
            }
            ++live;
            ++pos;
        }
        return nullptr;
    }

    // The a_index-th live actor of a_queue (0 = front) WITHOUT removing it;
    // None once a_index runs past the end. Stale entries are dropped as they
    // are encountered, so indices always range over live actors and a browse
    // loop (0, 1, 2, ... until None) enumerates exactly the queue's contents.
    RE::Actor* PeekActorAt(RE::StaticFunctionTag*, std::string_view a_queue, std::int32_t a_index) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Queues().PromoteDue(GK::NowSeconds());
        return NthLiveActor(*state, a_queue, a_index);
    }

    // The neighbors of a_queue's a_index-th live actor: [0] the one just
    // before it (towards the FRONT; None when a_index is 0), [1] the one just
    // after it (towards the BACK; None when a_index is the last). Always two
    // elements; both None when a_index is negative. Same live-index space as
    // PeekActorAt, both lookups under the one lock.
    std::vector<RE::Actor*> PeekAdjacentActorsAt(RE::StaticFunctionTag*, std::string_view a_queue,
                                                 std::int32_t a_index) {
        std::vector<RE::Actor*> out{nullptr, nullptr};
        if (a_index < 0) {
            return out;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Queues().PromoteDue(GK::NowSeconds());
        out[0] = NthLiveActor(*state, a_queue, a_index - 1);  // nullptr when a_index == 0
        out[1] = NthLiveActor(*state, a_queue, a_index + 1);
        return out;
    }

    // PeekAdjacentActorsAt keyed by the actor instead of its index: the
    // neighbors of a_actor in a_queue ([0] towards the front, [1] towards the
    // back, None past either end). Both None when a_actor is None or not in
    // the queue. Single front-to-back walk under the one lock, dropping stale
    // entries like the other peeks.
    std::vector<RE::Actor*> PeekAdjacentActors(RE::StaticFunctionTag*, std::string_view a_queue, RE::Actor* a_actor) {
        std::vector<RE::Actor*> out{nullptr, nullptr};
        if (!a_actor) {
            return out;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Queues().PromoteDue(GK::NowSeconds());
        const auto target = a_actor->GetFormID();
        RE::Actor* prev = nullptr;
        bool found = false;
        std::size_t pos = 0;
        while (const auto id = state->Queues().At(a_queue, pos)) {
            auto* form = RE::TESForm::LookupByID(id);
            auto* actor = form ? form->As<RE::Actor>() : nullptr;
            if (!actor) {
                state->Queues().Remove(a_queue, id);  // drop the stale entry; re-test the same slot
                continue;
            }
            if (found) {
                out[1] = actor;
                break;
            }
            if (id == target) {
                found = true;
                out[0] = prev;  // nullptr when a_actor is at the front
            } else {
                prev = actor;
            }
            ++pos;
        }
        return out;
    }

    // Snapshot of a_queue's live actors, front to back, taken under one lock.
    // Stale entries are dropped as encountered (like PeekActorAt), so the array
    // is exactly what successive dequeues would hand out, in order.
    std::vector<RE::Actor*> GetQueueActors(RE::StaticFunctionTag*, std::string_view a_queue) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Queues().PromoteDue(GK::NowSeconds());
        std::vector<RE::Actor*> out;
        std::size_t i = 0;
        while (const auto id = state->Queues().At(a_queue, i)) {
            auto* form = RE::TESForm::LookupByID(id);
            if (auto* actor = form ? form->As<RE::Actor>() : nullptr) {
                out.push_back(actor);
                ++i;
            } else {
                state->Queues().Remove(a_queue, id);  // drop the stale entry; the slot now holds the next one
            }
        }
        return out;
    }

    std::int32_t GetQueueSize(RE::StaticFunctionTag*, std::string_view a_queue) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Queues().PromoteDue(GK::NowSeconds());
        return static_cast<std::int32_t>(state->Queues().Size(a_queue));
    }

    void ClearQueue(RE::StaticFunctionTag*, std::string_view a_queue) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Queues().ClearQueue(a_queue);
    }

    // Claim from a queue for a SPECIFIC idle actor: if a_actor's status is idle
    // AND a_queue yields a live actor, transition a_actor to a_newStatus and
    // return the dequeued actor -- all under the one GameState lock. On ANY
    // failure (a_actor missing or not idle, queue empty or drained to stale
    // entries, a_actor untrackable) nothing changes and None is returned.
    RE::Actor* TransitionIdleActorToAndDequeue(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_queue,
                                               std::string_view a_newStatus) {
        if (!a_actor) {
            return nullptr;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (GK::FoldCase(state->Actors().GetStatus(a_actor->GetFormID())) != GK::Status::kIdle) {
            return nullptr;
        }
        auto* dequeued = DequeueLiveActor(*state, a_queue);
        if (!dequeued) {
            return nullptr;
        }
        if (!GK::AliasPool::EnsureTrackable(*state, *a_actor)) {  // transitioning is an adder (see SetActorStatus)
            state->Queues().PushFront(a_queue, dequeued->GetFormID());  // undo the pop: nothing must change
            return nullptr;
        }
        state->Actors().SetStatus(a_actor->GetFormID(), a_newStatus);
        return dequeued;
    }

    // Like TransitionIdleActorToAndDequeue, but the idle actor is CHOSEN, not
    // given: among the claimable idle actors (see AsClaimableIdle) holding
    // a_role for a_labyrinth (RecordHasRoleForLab: Wanderer matches regardless
    // of the labyrinth; None = any labyrinth), pick the one closest in 3D to
    // the next live actor of a_queue, transition it to a_newStatus, pop that
    // queue entry, and return [claimed, dequeued]. All of it happens under the
    // one GameState lock, so two Papyrus threads can never claim the same actor
    // or queue entry. On ANY failure (queue empty or drained to stale entries,
    // no matching idle actor) nothing changes and an empty array is returned.
    // The queued actor never claims itself.
    std::vector<RE::Actor*> TransitionClosestIdleActorToAndDequeue(RE::StaticFunctionTag*,
                                                                   RE::TESObjectREFR* a_labyrinth,
                                                                   std::int32_t a_role, std::string_view a_queue,
                                                                   std::string_view a_newStatus) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        auto* dequeued = DequeueLiveActor(*state, a_queue);
        if (!dequeued) {
            return {};
        }
        const auto mask = static_cast<std::uint32_t>(a_role);
        const auto labID = a_labyrinth ? a_labyrinth->GetFormID() : 0;
        const auto targetPos = dequeued->GetPosition();

        RE::Actor* best = nullptr;
        float bestDistSq = 0.0f;
        for (const auto& [id, rec] : state->Actors().Records()) {
            if (id == dequeued->GetFormID() || !RecordHasRoleForLab(rec, labID, mask)) {
                continue;
            }
            auto* actor = AsClaimableIdle(id, rec);
            if (!actor) {
                continue;
            }
            const float distSq = actor->GetPosition().GetSquaredDistance(targetPos);
            if (!best || distSq < bestDistSq) {
                best = actor;
                bestDistSq = distSq;
            }
        }
        if (!best) {
            state->Queues().PushFront(a_queue, dequeued->GetFormID());  // undo the pop: nothing must change
            return {};
        }
        state->Actors().SetStatus(best->GetFormID(), a_newStatus);
        return {best, dequeued};
    }

    // --- actor attributes -------------------------------------------------------
    // Per-actor key/value store (String key -> ObjectReference value): any String
    // mints an attribute on first set, so plugins define their own without enums
    // or int mappings (keys are case-insensitive, like Papyrus string compares).
    // Attributes are an independent utility: they don't track the actor, touch
    // roles, or take a pool alias, and they persist in the co-save (see
    // Serialization.cpp, ATTR).

    void SetActorAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key,
                           RE::TESObjectREFR* a_value) {
        if (!a_actor || a_key.empty()) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Attributes().Set(a_actor->GetFormID(), a_key, a_value ? a_value->GetFormID() : 0);
    }

    RE::TESObjectREFR* GetActorAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key) {
        if (!a_actor) {
            return nullptr;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return AsRef(state->Attributes().Get(a_actor->GetFormID(), a_key));
    }

    // Equivalent to SetActorAttribute(actor, key, None); paired with
    // ClearActorIntAttribute for a symmetric surface.
    void ClearActorAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Attributes().Set(a_actor->GetFormID(), a_key, 0);
    }

    // Int attributes are a store of their own (a key names different attributes
    // here and in the ObjectReference store). Every value is stored -- 0 is a
    // legitimate int, so removal goes through ClearActorIntAttribute, not a
    // sentinel value.
    void SetActorIntAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key,
                              std::int32_t a_value) {
        if (!a_actor || a_key.empty()) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Attributes().SetInt(a_actor->GetFormID(), a_key, a_value);
    }

    std::int32_t GetActorIntAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key,
                                      std::int32_t a_default) {
        if (!a_actor) {
            return a_default;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Attributes().GetInt(a_actor->GetFormID(), a_key, a_default);
    }

    void ClearActorIntAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Attributes().ClearInt(a_actor->GetFormID(), a_key);
    }

    // Array attributes are a third store (a key names different attributes in
    // each store). Values are held as FormIDs, so any form types may mix in one
    // array; Papyrus declares Form[] and casts per element on read (array types
    // are invariant in Papyrus -- an Actor[] variable must be copied into a
    // Form[] to be passed here). POSITIONAL: None entries are stored as real
    // slots and read back as None, so indices are stable (see the *Index
    // bindings). An EMPTY array clears the attribute.
    void SetActorArrayAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key,
                                std::vector<RE::TESForm*> a_values) {
        if (!a_actor || a_key.empty()) {
            return;
        }
        std::vector<RE::FormID> ids;
        ids.reserve(a_values.size());
        for (const auto* form : a_values) {
            ids.push_back(form ? form->GetFormID() : 0);  // None -> placeholder slot
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Attributes().SetArray(a_actor->GetFormID(), a_key, std::move(ids));
    }

    // Empty array if never set / cleared; otherwise the SAME LENGTH as stored,
    // with None at every slot that is a None placeholder or no longer resolves
    // (unresolvable entries stay stored -- the co-save load zeroes them for
    // good on the next save/load cycle).
    std::vector<RE::TESForm*> GetActorArrayAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor,
                                                     std::string_view a_key) {
        if (!a_actor) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* ids = state->Attributes().GetArray(a_actor->GetFormID(), a_key);
        if (!ids) {
            return {};
        }
        std::vector<RE::TESForm*> out;
        out.reserve(ids->size());
        for (const auto id : *ids) {
            out.push_back(id ? RE::TESForm::LookupByID(id) : nullptr);
        }
        return out;
    }

    // Safety cap for indexed writes: a script bug passing a huge index must not
    // allocate an arbitrarily large array.
    constexpr std::int32_t kMaxArrayAttributeIndex = 1023;

    // Set one position of the array attribute (0-indexed). An index past the
    // current end EXTENDS the array, filling the gap with None slots; setting
    // on a nonexistent attribute creates it. Out-of-range indices (< 0 or >
    // 1023) are ignored.
    void SetActorArrayAttributeIndex(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key,
                                     std::int32_t a_index, RE::TESForm* a_value) {
        if (!a_actor || a_key.empty()) {
            return;
        }
        if (a_index < 0 || a_index > kMaxArrayAttributeIndex) {
            logger::warn("SetActorArrayAttributeIndex: index {} out of range [0, {}]; ignored.", a_index,
                         kMaxArrayAttributeIndex);
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Attributes().SetArrayIndex(a_actor->GetFormID(), a_key, static_cast<std::size_t>(a_index),
                                          a_value ? a_value->GetFormID() : 0);
    }

    // The form at one position of the array attribute; None when the index is
    // out of bounds, the slot is a None placeholder, or the entry no longer
    // resolves.
    RE::TESForm* GetActorArrayAttributeIndex(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key,
                                             std::int32_t a_index) {
        if (!a_actor || a_index < 0) {
            return nullptr;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* ids = state->Attributes().GetArray(a_actor->GetFormID(), a_key);
        if (!ids || static_cast<std::size_t>(a_index) >= ids->size()) {
            return nullptr;
        }
        const auto id = (*ids)[static_cast<std::size_t>(a_index)];
        return id ? RE::TESForm::LookupByID(id) : nullptr;
    }

    void ClearActorArrayAttribute(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_key) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Attributes().SetArray(a_actor->GetFormID(), a_key, {});
    }

    // --- armor scans ------------------------------------------------------------

    // Minimal ABI mirror of Devious Devices NG's exported API object (its
    // include/API.h, DD_APIVERSION 2; the DLL exports a single GetAPI()
    // returning this). Only the vtable PREFIX we call is declared -- the slot
    // order must match the real class exactly, which is why GetVersion is
    // checked before anything else is trusted.
    struct DDNGApi {
        virtual std::size_t GetVersion() const = 0;                                                // 00
        virtual const void* GetDatabase() const = 0;                                               // 01 (unused here)
        virtual RE::TESObjectARMO* GetDeviceRender(RE::TESObjectARMO* a_invDevice) const = 0;      // 02
        virtual RE::TESObjectARMO* GetDeviceInventory(RE::TESObjectARMO* a_rendDevice) const = 0;  // 03
    };
    constexpr std::size_t kDDNGApiVersion = 2;

    // DD NG's device database, an OPTIONAL runtime dependency resolved lazily.
    // DD NG parses device relationships out of the plugins' VMAD data, which
    // the Papyrus VM cannot see for un-instantiated base forms (script
    // instances only exist once an item is instantiated).
    const DDNGApi* GetDDNGApi() {
        static const auto api = []() -> const DDNGApi* {
            const auto dd = GetModuleHandleA("DeviousDevices.dll");
            const auto proc = dd ? GetProcAddress(dd, "GetAPI") : nullptr;
            if (!proc) {
                logger::warn("DeviousDevices.dll not loaded (or no GetAPI export); DD device lookups "
                             "will yield nothing.");
                return nullptr;
            }
            const auto* result = reinterpret_cast<DDNGApi* (*)()>(proc)();
            if (!result || result->GetVersion() != kDDNGApiVersion) {
                logger::warn("DD NG API version {} != expected {}; DD device lookups will yield nothing.",
                             result ? result->GetVersion() : 0, kDDNGApiVersion);
                return nullptr;
            }
            logger::info("DD NG API v{} connected.", kDDNGApiVersion);
            return result;
        }();
        return api;
    }

    // Inventory device -> rendered armor (nullptr if not a DD device / no DD NG).
    RE::TESObjectARMO* DDGetDeviceRender(RE::TESObjectARMO* a_invDevice) {
        const auto* api = GetDDNGApi();
        return api && a_invDevice ? api->GetDeviceRender(a_invDevice) : nullptr;
    }

    // Rendered armor -> inventory device (nullptr if not a DD rendered device / no DD NG).
    RE::TESObjectARMO* DDGetDeviceInventory(RE::TESObjectARMO* a_rendDevice) {
        const auto* api = GetDDNGApi();
        return api && a_rendDevice ? api->GetDeviceInventory(a_rendDevice) : nullptr;
    }

    // Comma-separated editor IDs of a_keywords for the log ("<none>" if empty).
    std::string KeywordListLabel(const std::vector<RE::BGSKeyword*>& a_keywords) {
        if (a_keywords.empty()) {
            return "<none>";
        }
        std::string out;
        for (const auto* kw : a_keywords) {
            if (!out.empty()) {
                out += ", ";
            }
            out += kw ? kw->GetFormEditorID() : "<None>";
        }
        return out;
    }

    // Every loaded Armor form bearing a_keyword, optionally filtered to those
    // whose display name contains a_searchText (case-insensitive; "" = no name
    // filter, and nameless armors never match a non-empty search).
    //
    // The two keyword arrays filter on the Devious Devices RENDERED armor of
    // each candidate (looked up through DD NG's database, see
    // DDGetDeviceRender): the rendered armor must bear ALL of
    // a_renderedKeywords AND NONE of a_renderedExcludeKeywords. When either
    // array is non-empty, armors with no rendered device (not a DD inventory
    // device, or DeviousDevices.dll absent) are excluded. Both arrays empty
    // (Papyrus None) = no rendered filtering, no DD lookup at all. None
    // entries INSIDE an array are ignored (a warning is logged -- they usually
    // mean a typo'd Keyword.GetKeyword).
    std::vector<RE::TESObjectARMO*> GetArmorsWithKeyword(RE::StaticFunctionTag*, RE::BGSKeyword* a_keyword,
                                                         std::string_view a_searchText,
                                                         std::vector<RE::BGSKeyword*> a_renderedKeywords,
                                                         std::vector<RE::BGSKeyword*> a_renderedExcludeKeywords) {
        std::vector<RE::TESObjectARMO*> out;
        logger::info("GetArmorsWithKeyword: keyword='{}' search='{}' rendered=[{}] renderedExclude=[{}].",
                     a_keyword ? a_keyword->GetFormEditorID() : "<None>", a_searchText,
                     KeywordListLabel(a_renderedKeywords), KeywordListLabel(a_renderedExcludeKeywords));
        if (!a_keyword) {
            logger::warn("GetArmorsWithKeyword: keyword is None (typo in Keyword.GetKeyword?); returning empty.");
            return out;
        }
        const auto dropNones = [](std::vector<RE::BGSKeyword*>& a_list, const char* a_which) {
            const auto before = a_list.size();
            std::erase(a_list, nullptr);
            if (a_list.size() != before) {
                logger::warn("GetArmorsWithKeyword: {} None entr(ies) in the {} list ignored (typo in "
                             "Keyword.GetKeyword?).",
                             before - a_list.size(), a_which);
            }
        };
        dropNones(a_renderedKeywords, "rendered");
        dropNones(a_renderedExcludeKeywords, "rendered-exclude");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return out;
        }
        const std::string foldedSearch = GK::FoldCase(a_searchText);
        const bool renderedFilter = !a_renderedKeywords.empty() || !a_renderedExcludeKeywords.empty();
        for (auto* armor : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
            if (!armor || !armor->HasKeyword(a_keyword)) {
                continue;
            }
            if (!foldedSearch.empty()) {
                const char* name = armor->GetName();
                if (!name || GK::FoldCase(name).find(foldedSearch) == std::string::npos) {
                    continue;
                }
            }
            if (renderedFilter) {
                auto* rendered = DDGetDeviceRender(armor);
                if (!rendered) {
                    continue;  // not a DD inventory device (or DD NG absent)
                }
                const bool allRequired = std::ranges::all_of(
                    a_renderedKeywords, [&](auto* kw) { return rendered->HasKeyword(kw); });
                const bool anyExcluded = std::ranges::any_of(
                    a_renderedExcludeKeywords, [&](auto* kw) { return rendered->HasKeyword(kw); });
                if (!allRequired || anyExcluded) {
                    continue;
                }
                logger::info("GetArmorsWithKeyword: {:08X} '{}' -> rendered {:08X} '{}' MATCH.", armor->GetFormID(),
                             armor->GetName() ? armor->GetName() : "", rendered->GetFormID(),
                             rendered->GetName() ? rendered->GetName() : "");
            }
            out.push_back(armor);
        }
        logger::info("GetArmorsWithKeyword: {} armor(s) matched.", out.size());
        return out;
    }

    // Session-only form patch: OR the given biped slot into the slot mask of
    // every loaded Armor bearing a_keyword. Touches the ARMO occupancy mask
    // only (equip conflicts, GetWornArmor); armor addons / visuals untouched.
    // Runtime form edits are NOT saved and vanish on game restart: re-run on
    // every game load (idempotent -- armors already holding the slot are
    // skipped). Returns how many armors were patched by this call.
    std::int32_t PatchArmorSlotByKeyword(RE::StaticFunctionTag*, RE::BGSKeyword* a_keyword, std::int32_t a_slot) {
        logger::info("PatchArmorSlotByKeyword: keyword='{}' slot={}.",
                     a_keyword ? a_keyword->GetFormEditorID() : "<None>", a_slot);
        if (!a_keyword || a_slot < static_cast<std::int32_t>(GK::kBipedSlotFirst) ||
            a_slot > static_cast<std::int32_t>(GK::kBipedSlotLast)) {
            logger::warn("PatchArmorSlotByKeyword: None keyword or slot outside 30..61; nothing patched.");
            return 0;
        }
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return 0;
        }
        const auto slotBit = 1u << (a_slot - GK::kBipedSlotFirst);
        std::int32_t patched = 0;
        for (auto* armor : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
            if (!armor || !armor->HasKeyword(a_keyword)) {
                continue;
            }
            if (static_cast<std::uint32_t>(armor->GetSlotMask()) & slotBit) {
                continue;  // already occupies the slot
            }
            armor->AddSlotToMask(static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(slotBit));
            logger::info("PatchArmorSlotByKeyword: {:08X} '{}' += slot {}.", armor->GetFormID(),
                         armor->GetName() ? armor->GetName() : "", a_slot);
            ++patched;
        }
        logger::info("PatchArmorSlotByKeyword: {} armor(s) patched.", patched);
        return patched;
    }

    // --- actor outfits ----------------------------------------------------------
    // Per-actor named slot maps of INVENTORY devices; see State/OutfitRegistry.h.
    // Which biped slots a device claims comes from its RENDERED armor's slot
    // mask when it is a DD device (see DDGetDeviceRender), else its own mask.
    // Slot numbers are the standard biped slots 30..61; the Armor[] surface is
    // indexed directly by slot number (index 30 = head, ...), length 62.
    //
    // A None actor addresses the global TEMPLATE outfits: same storage, keyed
    // under FormID 0 (never a real actor). Templates belong to no actor and are
    // copied onto one with CopyTemplateOutfitToActor. Only the convergence
    // functions (NextDeviceToRemove/Add) require a real actor -- they compare
    // against worn equipment.

    // The registry key a_actor addresses (0 = the template namespace).
    [[nodiscard]] RE::FormID OutfitOwnerID(RE::Actor* a_actor) { return a_actor ? a_actor->GetFormID() : 0; }

    // The EFFECTIVE outfit for reading: the owner's own outfit when it exists
    // (a defined-but-empty one shadows), else the TEMPLATE of the same name --
    // actors inherit the defaults until an outfit is customized for them --
    // else nullptr (deemed all-empty). Used by the convergence functions and
    // by ActorOutfitMerge's inputs; a template owner just resolves twice.
    [[nodiscard]] const GK::OutfitRegistry::Slots* FindOutfitOrTemplate(GK::GameState& a_state, RE::FormID a_actor,
                                                                        std::string_view a_outfit) {
        const auto* slots = a_state.Outfits().Find(a_actor, a_outfit);
        return slots ? slots : a_state.Outfits().Find(0, a_outfit);
    }

    // The biped-slot mask the device occupies when worn.
    [[nodiscard]] std::uint32_t DeviceSlotMask(RE::TESObjectARMO* a_invDevice) {
        auto* rendered = DDGetDeviceRender(a_invDevice);
        return static_cast<std::uint32_t>((rendered ? rendered : a_invDevice)->GetSlotMask());
    }

    [[nodiscard]] bool MaskHasSlot(std::uint32_t a_mask, std::size_t a_slot) {
        return (a_mask >> (a_slot - GK::kBipedSlotFirst)) & 1u;
    }

    [[nodiscard]] RE::TESObjectARMO* AsArmor(RE::FormID a_id) {
        auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
        return form ? form->As<RE::TESObjectARMO>() : nullptr;
    }

    // Every loaded Armor bearing a_keyword (the inventory-device filter, like
    // GetArmorsWithKeyword) whose RENDERED device occupies biped slot a_slot
    // when worn (a non-DD armor's own slots are used; see DeviceSlotMask),
    // optionally filtered to display names containing a_searchText
    // (case-insensitive; "" = no name filter).
    std::vector<RE::TESObjectARMO*> GetArmorsWithSlot(RE::StaticFunctionTag*, RE::BGSKeyword* a_keyword,
                                                      std::int32_t a_slot, std::string_view a_searchText) {
        std::vector<RE::TESObjectARMO*> out;
        logger::info("GetArmorsWithSlot: keyword='{}' slot={} search='{}'.",
                     a_keyword ? a_keyword->GetFormEditorID() : "<None>", a_slot, a_searchText);
        if (!a_keyword || a_slot < static_cast<std::int32_t>(GK::kBipedSlotFirst) ||
            a_slot > static_cast<std::int32_t>(GK::kBipedSlotLast)) {
            logger::warn("GetArmorsWithSlot: None keyword or slot outside 30..61; returning empty.");
            return out;
        }
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return out;
        }
        const std::string foldedSearch = GK::FoldCase(a_searchText);
        for (auto* armor : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
            if (!armor || !armor->HasKeyword(a_keyword)) {
                continue;
            }
            if (!foldedSearch.empty()) {
                const char* name = armor->GetName();
                if (!name || GK::FoldCase(name).find(foldedSearch) == std::string::npos) {
                    continue;
                }
            }
            if (MaskHasSlot(DeviceSlotMask(armor), static_cast<std::size_t>(a_slot))) {
                out.push_back(armor);
            }
        }
        logger::info("GetArmorsWithSlot: {} armor(s) matched.", out.size());
        return out;
    }

    // Add a_device to the outfit: false (no change) if any slot the device
    // occupies is already taken in the outfit -- or on bad arguments / a device
    // occupying no slots at all. Creates the outfit on first use.
    bool ActorOutfitAddDevice(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit,
                              RE::TESObjectARMO* a_device) {
        if (a_outfit.empty() || !a_device) {
            return false;
        }
        const auto mask = DeviceSlotMask(a_device);
        if (mask == 0) {
            return false;
        }
        const auto ownerID = OutfitOwnerID(a_actor);
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (const auto* slots = state->Outfits().Find(ownerID, a_outfit)) {
            for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
                if (MaskHasSlot(mask, slot) && (*slots)[slot] != 0) {
                    return false;  // intersects an occupied slot: nothing changes
                }
            }
        }
        auto& slots = state->Outfits().GetOrCreate(ownerID, a_outfit);
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            if (MaskHasSlot(mask, slot)) {
                slots[slot] = a_device->GetFormID();
            }
        }
        return true;
    }

    // Like the add, but intersecting devices are DISPLACED: any device holding
    // one of the new device's slots is removed from ALL of its slots first (a
    // device is always fully in the outfit or absent -- no partial leftovers).
    bool ActorOutfitSwapDevice(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit,
                               RE::TESObjectARMO* a_device) {
        if (a_outfit.empty() || !a_device) {
            return false;
        }
        const auto mask = DeviceSlotMask(a_device);
        if (mask == 0) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        auto& slots = state->Outfits().GetOrCreate(OutfitOwnerID(a_actor), a_outfit);
        std::vector<RE::FormID> displaced;
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            if (MaskHasSlot(mask, slot) && slots[slot] != 0) {
                displaced.push_back(slots[slot]);
            }
        }
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            if (std::ranges::find(displaced, slots[slot]) != displaced.end()) {
                slots[slot] = 0;
            }
        }
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            if (MaskHasSlot(mask, slot)) {
                slots[slot] = a_device->GetFormID();
            }
        }
        return true;
    }

    // Remove a_device from every slot holding it (no-op if it isn't in the
    // outfit; matches by the device itself, not by its mask, so it also cleans
    // up after a mask change in the source plugin).
    void ActorOutfitRemoveDevice(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit,
                                 RE::TESObjectARMO* a_device) {
        if (a_outfit.empty() || !a_device) {
            return;
        }
        const auto ownerID = OutfitOwnerID(a_actor);
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        auto* slots = const_cast<GK::OutfitRegistry::Slots*>(state->Outfits().Find(ownerID, a_outfit));
        if (!slots) {
            return;
        }
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            if ((*slots)[slot] == a_device->GetFormID()) {
                (*slots)[slot] = 0;
            }
        }
        // Removing the last device leaves an existing-but-EMPTY outfit; only
        // DeleteActorOutfit removes one.
    }

    // Rebuild a_result from a_inputs, FULLY replacing it. Inputs are visited
    // in array order, earlier outfits taking priority: each UNIQUE device of
    // an input is copied with ALL the slots it holds there -- but only when
    // every one of those slots is still free in the result and the device is
    // not already in it; otherwise the device is skipped entirely (never
    // partially placed). Unknown input outfits are deemed empty; empty names
    // are skipped. The result is composed locally before storage is touched,
    // so a_result may itself appear among the inputs (its OLD content is
    // read). A result with no devices is erased.
    void ActorOutfitMerge(RE::StaticFunctionTag*, RE::Actor* a_actor, std::vector<std::string> a_inputs,
                          std::string_view a_result) {
        if (a_result.empty()) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto actorID = OutfitOwnerID(a_actor);

        GK::OutfitRegistry::Slots result{};
        for (const auto& name : a_inputs) {
            if (name.empty()) {
                continue;
            }
            const auto* in = FindOutfitOrTemplate(*state, actorID, name);
            if (!in) {
                continue;  // neither own nor template: contributes nothing
            }
            for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
                const auto id = (*in)[slot];
                if (id == 0) {
                    continue;
                }
                bool firstSlot = true;  // process each unique device once, at its lowest slot
                for (std::size_t s = GK::kBipedSlotFirst; s < slot; ++s) {
                    if ((*in)[s] == id) {
                        firstSlot = false;
                        break;
                    }
                }
                if (!firstSlot) {
                    continue;
                }
                bool already = false;
                bool fits = true;
                for (std::size_t s = GK::kBipedSlotFirst; s <= GK::kBipedSlotLast; ++s) {
                    already |= result[s] == id;
                    fits &= (*in)[s] != id || result[s] == 0;
                }
                if (already || !fits) {
                    continue;  // all-or-nothing: a blocked device is skipped entirely
                }
                for (std::size_t s = GK::kBipedSlotFirst; s <= GK::kBipedSlotLast; ++s) {
                    if ((*in)[s] == id) {
                        result[s] = id;
                    }
                }
            }
        }

        state->Outfits().GetOrCreate(actorID, a_result) = result;  // result EXISTS afterwards, even if empty
    }

    // Community/CK names for the biped slots (indexed by slot - 30).
    constexpr std::array<const char*, GK::kOutfitSlotCount - GK::kBipedSlotFirst> kBipedSlotNames{
        "head",             // 30
        "hair",             // 31
        "body",             // 32
        "hands",            // 33
        "forearms",         // 34
        "amulet",           // 35
        "ring",             // 36
        "feet",             // 37
        "calves",           // 38
        "shield",           // 39
        "tail",             // 40
        "long hair",        // 41
        "circlet",          // 42
        "ears",             // 43
        "face/mouth",       // 44
        "neck",             // 45
        "chest primary",    // 46
        "back",             // 47
        "misc 48",          // 48
        "pelvis primary",   // 49
        "decapitated head", // 50
        "decapitate",       // 51
        "pelvis secondary", // 52
        "leg primary",      // 53
        "leg secondary",    // 54
        "face alternate",   // 55
        "chest secondary",  // 56
        "shoulder",         // 57
        "arm secondary",    // 58
        "arm primary",      // 59
        "misc 60",          // 60
        "FX01",             // 61
    };

    // Devious Devices' slot-usage names (from the DD device construction kit /
    // MCM), indexed by slot - 30. "" for slots DD assigns no meaning to.
    constexpr std::array<const char*, GK::kOutfitSlotCount - GK::kBipedSlotFirst> kDDSlotNames{
        "Head",                        // 30
        "Hair",                        // 31
        "Body - Full",                 // 32
        "Hands",                       // 33
        "Forearms",                    // 34
        "Amulet",                      // 35
        "Ring",                        // 36
        "Feet",                        // 37
        "Calves",                      // 38
        "Shield",                      // 39
        "Tail",                        // 40
        "Device Hider + Long Hair",    // 41
        "Circlet",                     // 42
        "Ears",                        // 43
        "Gags",                        // 44
        "Collar",                      // 45
        "HeavyBondage",                // 46
        "Backpacks",                   // 47
        "Plugs (Anal)",                // 48
        "Chastity Belt",               // 49
        "Vag. Piercings",              // 50
        "Nipple Piercings",            // 51
        "SoS",                         // 52
        "Cuffs (Legs)",                // 53
        "",                            // 54 (no DD meaning)
        "Blindfold",                   // 55
        "Chastity Bra",                // 56
        "Plug Vaginal",                // 57
        "Harnesses / Corsets",         // 58
        "ArmCuff / Armbinder",          // 59
        "",                            // 60 (no DD meaning)
        "",                            // 61 (no DD meaning)
    };

    // Devious Devices' name for a biped slot, UPCASED for direct menu display
    // ("CHASTITY BELT" for 49, ...); "" for slots DD assigns no meaning to and
    // anything outside 30..61.
    std::string GetDDSlotName(RE::StaticFunctionTag*, std::int32_t a_slot) {
        if (a_slot < static_cast<std::int32_t>(GK::kBipedSlotFirst) ||
            a_slot > static_cast<std::int32_t>(GK::kBipedSlotLast)) {
            return {};
        }
        std::string out = kDDSlotNames[static_cast<std::size_t>(a_slot) - GK::kBipedSlotFirst];
        for (auto& ch : out) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return out;
    }

    // ASCII-uppercased copy of a_text (non-ASCII bytes pass through untouched).
    std::string UpCase(RE::StaticFunctionTag*, std::string_view a_text) {
        std::string out(a_text);
        for (auto& ch : out) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return out;
    }

    // a_text upcased and framed as a menu section header: rails of a_railChar
    // (first character; '=' when empty) sized so the result approaches a_width
    // characters ("== HARNESS / CORSET =="). Names at or beyond a_width get no
    // rails, just the upcased text.
    std::string MakeHeader(RE::StaticFunctionTag*, std::string_view a_text, std::int32_t a_width,
                           std::string_view a_railChar) {
        std::string name(a_text);
        for (auto& ch : name) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        const auto pad = (a_width - static_cast<std::int32_t>(name.size())) / 2;
        if (pad < 1) {
            return name;
        }
        const std::string rail(static_cast<std::size_t>(pad), a_railChar.empty() ? '=' : a_railChar.front());
        return rail + " " + name + " " + rail;
    }

    // The human-readable name of a biped slot (same table LogActorOutfit
    // prints); "" for anything outside 30..61.
    std::string GetSlotName(RE::StaticFunctionTag*, std::int32_t a_slot) {
        if (a_slot < static_cast<std::int32_t>(GK::kBipedSlotFirst) ||
            a_slot > static_cast<std::int32_t>(GK::kBipedSlotLast)) {
            return {};
        }
        return kBipedSlotNames[static_cast<std::size_t>(a_slot) - GK::kBipedSlotFirst];
    }

    // Dump the outfit's occupied slots to GordianKnot.log (empty slots are not
    // logged; an empty/unknown outfit logs a single "(empty)" line).
    void LogActorOutfit(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit) {
        if (a_outfit.empty()) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (a_actor) {
            const char* actorName = a_actor->GetName();
            logger::info("Outfit '{}' of {:08X} '{}':", a_outfit, a_actor->GetFormID(), actorName ? actorName : "");
        } else {
            logger::info("Template outfit '{}':", a_outfit);
        }
        const auto* slots = state->Outfits().Find(OutfitOwnerID(a_actor), a_outfit);
        if (!slots) {
            logger::info("  (empty)");
            return;
        }
        std::size_t logged = 0;
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            const auto id = (*slots)[slot];
            if (id == 0) {
                continue;
            }
            auto* armor = AsArmor(id);
            const char* armorName = armor ? armor->GetName() : nullptr;
            logger::info("  slot {} ({}): '{}' (0x{:08X}{})", slot, kBipedSlotNames[slot - GK::kBipedSlotFirst],
                         armorName ? armorName : "", id, armor ? "" : ", unresolvable");
            ++logged;
        }
        if (logged == 0) {
            logger::info("  (empty)");
        }
    }

    // The outfit as a 62-element Armor array indexed by slot number (indices
    // 0..29 and empty/unresolvable slots are None). Empty array only when the
    // arguments are bad; an unknown outfit yields all-None.
    std::vector<RE::TESObjectARMO*> GetOutfitDevices(RE::StaticFunctionTag*, RE::Actor* a_actor,
                                                     std::string_view a_outfit) {
        if (a_outfit.empty()) {
            return {};
        }
        std::vector<RE::TESObjectARMO*> out(GK::kOutfitSlotCount, nullptr);
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (const auto* slots = state->Outfits().Find(OutfitOwnerID(a_actor), a_outfit)) {
            for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
                out[slot] = AsArmor((*slots)[slot]);
            }
        }
        return out;
    }

    // Wholesale write in the same slot-indexed format (entries below index 30
    // and past index 61 are ignored; a shorter array leaves the tail empty).
    // Slots are stored EXACTLY as given -- no mask computation. An all-None
    // array erases the outfit.
    void SetOutfitDevices(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit,
                          std::vector<RE::TESObjectARMO*> a_devices) {
        if (a_outfit.empty()) {
            return;
        }
        const auto ownerID = OutfitOwnerID(a_actor);
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        auto& slots = state->Outfits().GetOrCreate(ownerID, a_outfit);
        slots.fill(0);
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast && slot < a_devices.size(); ++slot) {
            slots[slot] = a_devices[slot] ? a_devices[slot]->GetFormID() : 0;
        }
        // An all-None write leaves an existing-but-EMPTY outfit (defined, no
        // devices); only DeleteActorOutfit removes one.
    }

    // The device in ONE slot (standard biped slot number, 30..61); None for an
    // empty slot, an out-of-range number, or an unknown outfit.
    RE::TESObjectARMO* GetOutfitDevice(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit,
                                       std::int32_t a_slot) {
        if (a_outfit.empty() || a_slot < static_cast<std::int32_t>(GK::kBipedSlotFirst) ||
            a_slot > static_cast<std::int32_t>(GK::kBipedSlotLast)) {
            return nullptr;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* slots = state->Outfits().Find(OutfitOwnerID(a_actor), a_outfit);
        return slots ? AsArmor((*slots)[static_cast<std::size_t>(a_slot)]) : nullptr;
    }

    // True when the outfit is DEFINED for the actor (None = template
    // namespace) -- including defined-but-empty ones. Reads never create.
    bool ActorOutfitExists(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit) {
        if (a_outfit.empty()) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Outfits().Find(OutfitOwnerID(a_actor), a_outfit) != nullptr;
    }

    // Define the outfit as existing and EMPTY when it doesn't exist yet (true
    // = created now). An outfit that already exists is left untouched (false)
    // -- use SetOutfitDevices with None to reset one to empty.
    bool CreateEmptyOutfit(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit) {
        if (a_outfit.empty()) {
            return false;
        }
        const auto ownerID = OutfitOwnerID(a_actor);
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (state->Outfits().Find(ownerID, a_outfit)) {
            return false;
        }
        static_cast<void>(state->Outfits().GetOrCreate(ownerID, a_outfit));  // creation is the side effect we want
        return true;
    }

    // Un-define the outfit outright (no-op if it doesn't exist). For an
    // actor's outfit this re-exposes the same-named template to the
    // convergence functions.
    void DeleteActorOutfit(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit) {
        if (a_outfit.empty()) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        state->Outfits().Erase(OutfitOwnerID(a_actor), a_outfit);
    }

    // Copy the TEMPLATE outfit a_source onto a_targetActor as a_target, fully
    // replacing it. An unknown/empty template erases the target outfit. A None
    // target actor copies template-to-template (rename/duplicate).
    void CopyTemplateOutfitToActor(RE::StaticFunctionTag*, RE::Actor* a_targetActor, std::string_view a_source,
                                   std::string_view a_target) {
        if (a_source.empty() || a_target.empty()) {
            return;
        }
        const auto targetID = OutfitOwnerID(a_targetActor);
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* source = state->Outfits().Find(0, a_source);  // 0 = template namespace
        const GK::OutfitRegistry::Slots copy = source ? *source : GK::OutfitRegistry::Slots{};
        state->Outfits().GetOrCreate(targetID, a_target) = copy;  // target EXISTS afterwards, even if empty
    }

    // Convergence, removal phase: scanning slots 30..61 in order, the first
    // WORN armor that differs from what the outfit wants in that slot (the
    // outfit device is compared via its RENDERED armor, since that is what is
    // actually worn). Returns the worn armor's INVENTORY device when it is a
    // DD rendered device (that is what removal APIs want), else the worn armor
    // itself; None when nothing is left to remove. Aims for the actor's outfit,
    // falling back to the same-named TEMPLATE (see FindOutfitOrTemplate); when
    // neither exists the outfit is deemed all-EMPTY, so converging removes
    // everything worn.
    RE::TESObjectARMO* NextDeviceToRemove(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit) {
        if (!a_actor || a_outfit.empty()) {
            return nullptr;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        static constexpr GK::OutfitRegistry::Slots kEmptySlots{};
        const auto* found = FindOutfitOrTemplate(*state, a_actor->GetFormID(), a_outfit);
        const auto* slots = found ? found : &kEmptySlots;
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            auto* worn = a_actor->GetWornArmor(
                static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << (slot - GK::kBipedSlotFirst)));
            if (!worn) {
                continue;
            }
            auto* wantedInv = AsArmor((*slots)[slot]);
            auto* wantedWorn = wantedInv ? DDGetDeviceRender(wantedInv) : nullptr;
            if (worn == (wantedWorn ? wantedWorn : wantedInv)) {
                continue;  // this slot already matches the outfit
            }
            auto* wornInv = DDGetDeviceInventory(worn);
            return wornInv ? wornInv : worn;
        }
        return nullptr;
    }

    // Convergence, add phase (run after the removal loop returns None):
    // scanning slots 30..61 in order, the first outfit device whose slot the
    // actor currently wears NOTHING in; None when nothing is left to add.
    // Returns the INVENTORY device exactly as stored. Aims for the actor's
    // outfit, falling back to the same-named TEMPLATE (see
    // FindOutfitOrTemplate); with neither there is nothing to add.
    RE::TESObjectARMO* NextDeviceToAdd(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_outfit) {
        if (!a_actor || a_outfit.empty()) {
            return nullptr;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* slots = FindOutfitOrTemplate(*state, a_actor->GetFormID(), a_outfit);
        if (!slots) {
            return nullptr;  // deemed empty: an empty outfit has nothing to add
        }
        for (std::size_t slot = GK::kBipedSlotFirst; slot <= GK::kBipedSlotLast; ++slot) {
            auto* wantedInv = AsArmor((*slots)[slot]);
            if (!wantedInv) {
                continue;
            }
            auto* worn = a_actor->GetWornArmor(
                static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << (slot - GK::kBipedSlotFirst)));
            if (worn) {
                continue;  // slot busy (matching or not: removal phase owns mismatches)
            }
            return wantedInv;
        }
        return nullptr;
    }

    // --- animation registries ---------------------------------------------------
    // Named, weighted pools of animation names: any String mints a registry on
    // first use (names case-insensitive, like Papyrus). SESSION config like the
    // keyword config -- never serialized, so scripts re-Add on every game load
    // (Add is idempotent; re-adding just updates the weight).

    bool AddAnimation(RE::StaticFunctionTag*, std::string_view a_registry, std::string_view a_anim, float a_weight) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        return state->Animations().Add(a_registry, a_anim, a_weight);
    }

    // Weighted-random draw: an entry's chance is weight / (sum of the registry's
    // weights). "" when the registry was never used (or all Adds were rejected).
    std::string GetAnimation(RE::StaticFunctionTag*, std::string_view a_registry) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* entries = state->Animations().Find(a_registry);
        if (!entries || entries->empty()) {
            return {};
        }
        double total = 0.0;
        for (const auto& entry : *entries) {
            total += entry.weight;
        }
        double roll = std::uniform_real_distribution<double>{0.0, total}(Rng());
        for (const auto& entry : *entries) {
            roll -= entry.weight;
            if (roll < 0.0) {
                return entry.name;
            }
        }
        return entries->back().name;  // floating-point edge: roll landed exactly on total
    }

    // --- queries --------------------------------------------------------------

    std::vector<RE::Actor*> GetActorsByRole(RE::StaticFunctionTag*, RE::TESObjectREFR* a_labyrinth,
                                            std::int32_t a_roleMask) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto mask = static_cast<std::uint32_t>(a_roleMask);
        // None -> match the scoped role in ANY labyrinth.
        return ResolveActors(a_labyrinth ? state->Actors().GetByRole(a_labyrinth->GetFormID(), mask)
                                         : state->Actors().GetByRoleAnywhere(mask));
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

    std::vector<RE::Actor*> GetActorsByStatus(RE::StaticFunctionTag*, std::string_view a_status) {
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

    std::vector<RE::TESObjectREFR*> GetLabyrinths(RE::StaticFunctionTag*) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        std::vector<RE::FormID> anchors;
        anchors.reserve(state->Labyrinths().All().size());
        for (const auto& [anchor, factions] : state->Labyrinths().All()) {
            anchors.push_back(anchor);
        }
        return ResolveRefs(anchors);
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

    // Debug.SendAnimationEvent with the engine's verdict surfaced: the actor's
    // behavior graph resolves the event name against its event table, so the
    // return is false when the event does not exist there (animation pack
    // missing, or its FNIS/Nemesis/Pandora patch was never generated) and true
    // when the event was accepted (and plays). Requires the actor's 3D/graph to
    // be loaded -- an unloaded actor reports false for events it does have. No
    // game state, no lock needed.
    bool TrySendAnimationEvent(RE::StaticFunctionTag*, RE::Actor* a_actor, std::string_view a_event) {
        if (!a_actor || a_event.empty()) {
            return false;
        }
        return a_actor->NotifyAnimationGraph(RE::BSFixedString{a_event});
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

    // The handle of the cell whose door is a_door (0 if a_door is None or not
    // a discovered cell door). Inverse of GetCellDoor; pairs with the door ref
    // delivered by the GK_OnActivateCellDoor mod event.
    std::int32_t GetCellByDoor(RE::StaticFunctionTag*, RE::TESObjectREFR* a_door) {
        if (!a_door) {
            return 0;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByKey(a_door->GetFormID());
        return cell ? cell->handle : GK::kInvalidHandle;
    }

    std::int32_t GetCellOccupantCount(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? static_cast<std::int32_t>(cell->occupants.size()) : 0;
    }

    // Free spots left in a_cell (maxOccupants - occupants); 0 = full or the
    // handle is unknown. Clamped to 0 if occupancy exceeds a lowered max.
    std::int32_t GetCellVacancy(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        if (!cell || cell->occupants.size() >= cell->maxOccupants) {
            return 0;
        }
        return static_cast<std::int32_t>(cell->maxOccupants - cell->occupants.size());
    }

    // The a_index-th occupant (prisoner) of a_cell, in assignment order. None
    // if the handle is unknown, a_index is out of range, or that occupant no
    // longer resolves to an Actor. Read-only: a stale occupant is reported as
    // None, not dropped -- occupancy only changes via the assign/clear paths.
    RE::Actor* GetCellOccupantAt(RE::StaticFunctionTag*, std::int32_t a_cell, std::int32_t a_index) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        if (!cell || a_index < 0 || static_cast<std::size_t>(a_index) >= cell->occupants.size()) {
            return nullptr;
        }
        auto* form = RE::TESForm::LookupByID(cell->occupants[static_cast<std::size_t>(a_index)]);
        return form ? form->As<RE::Actor>() : nullptr;
    }

    // Snapshot of a_cell's occupants (prisoners) in assignment order. Read-only
    // like GetCellOccupantAt: an occupant that no longer resolves is skipped in
    // the result but left in the cell.
    std::vector<RE::Actor*> GetCellOccupants(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? ResolveActors(cell->occupants) : std::vector<RE::Actor*>{};
    }

    std::string GetCellFlags(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? cell->flags : std::string{};
    }

    void SetCellFlags(RE::StaticFunctionTag*, std::int32_t a_cell, std::string_view a_flags) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (auto* cell = state->Resources().CellPool().FindByHandle(a_cell)) {
            cell->flags = a_flags;
        }
    }

    RE::TESObjectREFR* GetCellLabyrinth(RE::StaticFunctionTag*, std::int32_t a_cell) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* cell = state->Resources().CellPool().FindByHandle(a_cell);
        return cell ? AsRef(cell->labyrinth) : nullptr;
    }

    std::vector<std::int32_t> GetCells(RE::StaticFunctionTag*, RE::TESObjectREFR* a_labyrinth,
                                       std::string_view a_filterString) {
        if (!a_labyrinth) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        std::vector<std::int32_t> out;
        for (const auto& [handle, cell] : state->Resources().CellPool().All()) {
            if (cell.labyrinth == a_labyrinth->GetFormID() && cell.MatchesFilter(a_filterString)) {
                out.push_back(handle);
            }
        }
        return out;
    }

    // Claim a free cell for an actor; returns the claimed cell's handle (0 = none).
    // Candidates are the cells of a_labyrinth ONLY (null labyrinth -> 0, no
    // effect) that have space and pass the flag filter; one is picked at RANDOM
    // (uniform among the candidates). If the actor already occupies a cell -- in
    // ANY labyrinth -- it is MOVED to the picked one (its current cell is not a
    // candidate); with no candidate it stays put, and the current cell's handle
    // is returned only when that cell belongs to a_labyrinth (otherwise 0:
    // nothing was assigned there). Assigning a NEW actor is an adder (see
    // SetActorStatus): if the actor can't be tracked, nothing changes and 0 is
    // returned.
    std::int32_t AssignFreeCellToPrisoner(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_labyrinth,
                                          std::string_view a_filterString) {
        if (!a_actor || !a_labyrinth) {
            return GK::kInvalidHandle;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto actorID = a_actor->GetFormID();
        const auto labID = a_labyrinth->GetFormID();

        GK::Handle currentHandle = GK::kInvalidHandle;
        GK::Cell* current = nullptr;
        std::vector<std::pair<GK::Handle, GK::Cell*>> candidates;
        for (auto& [handle, cell] : state->Resources().CellPool().All()) {
            if (cell.Contains(actorID)) {
                currentHandle = handle;
                current = &cell;
            } else if (cell.labyrinth == labID && cell.HasSpace() && cell.MatchesFilter(a_filterString)) {
                candidates.emplace_back(handle, &cell);
            }
        }
        if (candidates.empty()) {
            return (current && current->labyrinth == labID) ? currentHandle : GK::kInvalidHandle;
        }
        if (!GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return GK::kInvalidHandle;
        }
        const auto [targetHandle, target] = candidates[RandomIndex(candidates.size())];
        if (current) {
            std::erase(current->occupants, actorID);
        }
        target->occupants.push_back(actorID);
        return targetHandle;
    }

    // Assign the SPECIFIC cell a_cell to the actor (handles are unique across
    // all labyrinths, so the cell alone identifies the target): false if the
    // cell is unknown or has no space left; true (no change) if the actor
    // already occupies it. Otherwise the same contract as the free-cell
    // assigner: an actor occupying another cell -- in ANY labyrinth -- is
    // MOVED, and assigning a NEW actor is an adder (untrackable -> false,
    // nothing changes).
    bool AssignPrisonerToCell(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_cell) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto actorID = a_actor->GetFormID();

        auto* target = state->Resources().CellPool().FindByHandle(a_cell);
        if (!target) {
            return false;
        }
        if (target->Contains(actorID)) {
            return true;
        }
        if (!target->HasSpace() || !GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return false;
        }
        for (auto& [handle, cell] : state->Resources().CellPool().All()) {
            std::erase(cell.occupants, actorID);  // moved: leave the current cell (normally at most one)
        }
        target->occupants.push_back(actorID);
        return true;
    }

    std::int32_t GetActorCell(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return GK::kInvalidHandle;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        for (const auto& [handle, cell] : state->Resources().CellPool().All()) {
            if (cell.Contains(a_actor->GetFormID())) {
                return handle;
            }
        }
        return GK::kInvalidHandle;
    }

    // Remove a_actor from every cell holding it (normally at most one). Clearing
    // mutator: never tracks; unassigned actor is a no-op.
    void ClearActorCell(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        for (auto& [handle, cell] : state->Resources().CellPool().All()) {
            std::erase(cell.occupants, a_actor->GetFormID());
        }
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

    std::string GetFurnitureFlags(RE::StaticFunctionTag*, std::int32_t a_furniture) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* furniture = state->Resources().FurniturePool().FindByHandle(a_furniture);
        return furniture ? furniture->flags : std::string{};
    }

    void SetFurnitureFlags(RE::StaticFunctionTag*, std::int32_t a_furniture, std::string_view a_flags) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        if (auto* furniture = state->Resources().FurniturePool().FindByHandle(a_furniture)) {
            furniture->flags = a_flags;
        }
    }

    std::vector<std::int32_t> GetFurnitures(RE::StaticFunctionTag*, RE::TESObjectREFR* a_labyrinth,
                                            std::string_view a_filterString) {
        if (!a_labyrinth) {
            return {};
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        std::vector<std::int32_t> out;
        for (const auto& [handle, furniture] : state->Resources().FurniturePool().All()) {
            if (furniture.labyrinth == a_labyrinth->GetFormID() && furniture.MatchesFilter(a_filterString)) {
                out.push_back(handle);
            }
        }
        return out;
    }

    // Claim a free furniture for an actor; returns the claimed furniture's handle
    // (0 = none). Same contract as AssignFreeCellToPrisoner (furniture is
    // single-occupant): candidates are the furniture of a_labyrinth ONLY (null
    // labyrinth -> 0, no effect) that have space and pass the flag filter; one is
    // picked at RANDOM (uniform among the candidates). If the actor already
    // occupies a furniture -- in ANY labyrinth -- it is MOVED to the picked one
    // (its current furniture is not a candidate); with no candidate it stays put,
    // and the current handle is returned only when that furniture belongs to
    // a_labyrinth (otherwise 0: nothing was assigned there). Assigning a NEW
    // actor is an adder (see SetActorStatus): if the actor can't be tracked,
    // nothing changes and 0 is returned.
    std::int32_t AssignFreeFurnitureToPrisoner(RE::StaticFunctionTag*, RE::Actor* a_actor,
                                               RE::TESObjectREFR* a_labyrinth, std::string_view a_filterString) {
        if (!a_actor || !a_labyrinth) {
            return GK::kInvalidHandle;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto actorID = a_actor->GetFormID();
        const auto labID = a_labyrinth->GetFormID();

        GK::Handle currentHandle = GK::kInvalidHandle;
        GK::Furniture* current = nullptr;
        std::vector<std::pair<GK::Handle, GK::Furniture*>> candidates;
        for (auto& [handle, furniture] : state->Resources().FurniturePool().All()) {
            if (furniture.Contains(actorID)) {
                currentHandle = handle;
                current = &furniture;
            } else if (furniture.labyrinth == labID && furniture.HasSpace() && furniture.MatchesFilter(a_filterString)) {
                candidates.emplace_back(handle, &furniture);
            }
        }
        if (candidates.empty()) {
            return (current && current->labyrinth == labID) ? currentHandle : GK::kInvalidHandle;
        }
        if (!GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return GK::kInvalidHandle;
        }
        const auto [targetHandle, target] = candidates[RandomIndex(candidates.size())];
        if (current) {
            std::erase(current->occupants, actorID);
        }
        target->occupants.push_back(actorID);
        return targetHandle;
    }

    // The handle of the furniture whose reference is a_ref (0 if a_ref is None
    // or not discovered furniture). Inverse of GetFurnitureRef; pairs with the
    // ref delivered by the GK_OnActivateFurniture mod event.
    std::int32_t GetFurnitureByRef(RE::StaticFunctionTag*, RE::TESObjectREFR* a_ref) {
        if (!a_ref) {
            return GK::kInvalidHandle;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* furniture = state->Resources().FurniturePool().FindByKey(a_ref->GetFormID());
        return furniture ? furniture->handle : GK::kInvalidHandle;
    }

    // Free spots left in a_furniture: 1 = free, 0 = occupied or the handle is
    // unknown (furniture is single-occupant; computed like GetCellVacancy).
    std::int32_t GetFurnitureVacancy(RE::StaticFunctionTag*, std::int32_t a_furniture) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* furniture = state->Resources().FurniturePool().FindByHandle(a_furniture);
        if (!furniture || furniture->occupants.size() >= furniture->maxOccupants) {
            return 0;
        }
        return static_cast<std::int32_t>(furniture->maxOccupants - furniture->occupants.size());
    }

    // The actor occupying a_furniture (single-occupant), or None if it is
    // free, the handle is unknown, or the occupant no longer resolves.
    // Read-only, like GetCellOccupantAt.
    RE::Actor* GetFurnitureOccupant(RE::StaticFunctionTag*, std::int32_t a_furniture) {
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto* furniture = state->Resources().FurniturePool().FindByHandle(a_furniture);
        if (!furniture || furniture->occupants.empty()) {
            return nullptr;
        }
        auto* form = RE::TESForm::LookupByID(furniture->occupants.front());
        return form ? form->As<RE::Actor>() : nullptr;
    }

    // Assign the SPECIFIC furniture a_furniture to the actor (handles are
    // unique across all labyrinths, so the furniture alone identifies the
    // target): false if the furniture is unknown or has no space left; true
    // (no change) if the actor already occupies it. Otherwise the same
    // contract as the free-furniture assigner: an actor occupying another
    // furniture -- in ANY labyrinth -- is MOVED, and assigning a NEW actor is
    // an adder (untrackable -> false, nothing changes).
    bool AssignPrisonerToFurniture(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_furniture) {
        if (!a_actor) {
            return false;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        const auto actorID = a_actor->GetFormID();

        auto* target = state->Resources().FurniturePool().FindByHandle(a_furniture);
        if (!target) {
            return false;
        }
        if (target->Contains(actorID)) {
            return true;
        }
        if (!target->HasSpace() || !GK::AliasPool::EnsureTrackable(*state, *a_actor)) {
            return false;
        }
        for (auto& [handle, furniture] : state->Resources().FurniturePool().All()) {
            std::erase(furniture.occupants, actorID);  // moved: leave the current furniture (normally at most one)
        }
        target->occupants.push_back(actorID);
        return true;
    }

    std::int32_t GetActorFurniture(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return GK::kInvalidHandle;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        for (const auto& [handle, furniture] : state->Resources().FurniturePool().All()) {
            if (furniture.Contains(a_actor->GetFormID())) {
                return handle;
            }
        }
        return GK::kInvalidHandle;
    }

    // Remove a_actor from every furniture holding it (normally at most one).
    // Clearing mutator: never tracks; unassigned actor is a no-op.
    void ClearActorFurniture(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }
        auto* state = GK::GameState::GetSingleton();
        auto lock = state->Lock();
        for (auto& [handle, furniture] : state->Resources().FurniturePool().All()) {
            std::erase(furniture.occupants, a_actor->GetFormID());
        }
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
        a_vm->RegisterFunction("ClearActorStatus", kClass, ClearActorStatus);
        a_vm->RegisterFunction("IsActorStatus", kClass, IsActorStatus);
        a_vm->RegisterFunction("IsActorIdle", kClass, IsActorIdle);
        a_vm->RegisterFunction("GetIdleActorAndTransitionTo", kClass, GetIdleActorAndTransitionTo);
        a_vm->RegisterFunction("TransitionIdleActorToAndDequeue", kClass, TransitionIdleActorToAndDequeue);
        a_vm->RegisterFunction("TransitionClosestIdleActorToAndDequeue", kClass, TransitionClosestIdleActorToAndDequeue);

        a_vm->RegisterFunction("EnqueueActor", kClass, EnqueueActor);
        a_vm->RegisterFunction("DequeueActor", kClass, DequeueActor);
        a_vm->RegisterFunction("PeekActor", kClass, PeekActor);
        a_vm->RegisterFunction("PeekLastActor", kClass, PeekLastActor);
        a_vm->RegisterFunction("PeekActorAt", kClass, PeekActorAt);
        a_vm->RegisterFunction("PeekAdjacentActorsAt", kClass, PeekAdjacentActorsAt);
        a_vm->RegisterFunction("PeekAdjacentActors", kClass, PeekAdjacentActors);
        a_vm->RegisterFunction("GetQueueActors", kClass, GetQueueActors);
        a_vm->RegisterFunction("GetQueueSize", kClass, GetQueueSize);
        a_vm->RegisterFunction("ClearQueue", kClass, ClearQueue);

        a_vm->RegisterFunction("SetActorAttribute", kClass, SetActorAttribute);
        a_vm->RegisterFunction("GetActorAttribute", kClass, GetActorAttribute);
        a_vm->RegisterFunction("ClearActorAttribute", kClass, ClearActorAttribute);
        a_vm->RegisterFunction("SetActorIntAttribute", kClass, SetActorIntAttribute);
        a_vm->RegisterFunction("GetActorIntAttribute", kClass, GetActorIntAttribute);
        a_vm->RegisterFunction("ClearActorIntAttribute", kClass, ClearActorIntAttribute);
        a_vm->RegisterFunction("SetActorArrayAttribute", kClass, SetActorArrayAttribute);
        a_vm->RegisterFunction("GetActorArrayAttribute", kClass, GetActorArrayAttribute);
        a_vm->RegisterFunction("ClearActorArrayAttribute", kClass, ClearActorArrayAttribute);
        a_vm->RegisterFunction("SetActorArrayAttributeIndex", kClass, SetActorArrayAttributeIndex);
        a_vm->RegisterFunction("GetActorArrayAttributeIndex", kClass, GetActorArrayAttributeIndex);
        a_vm->RegisterFunction("GetArmorsWithKeyword", kClass, GetArmorsWithKeyword);
        a_vm->RegisterFunction("PatchArmorSlotByKeyword", kClass, PatchArmorSlotByKeyword);
        a_vm->RegisterFunction("GetArmorsWithSlot", kClass, GetArmorsWithSlot);
        a_vm->RegisterFunction("ActorOutfitAddDevice", kClass, ActorOutfitAddDevice);
        a_vm->RegisterFunction("ActorOutfitSwapDevice", kClass, ActorOutfitSwapDevice);
        a_vm->RegisterFunction("ActorOutfitRemoveDevice", kClass, ActorOutfitRemoveDevice);
        a_vm->RegisterFunction("ActorOutfitMerge", kClass, ActorOutfitMerge);
        a_vm->RegisterFunction("CopyTemplateOutfitToActor", kClass, CopyTemplateOutfitToActor);
        a_vm->RegisterFunction("ActorOutfitExists", kClass, ActorOutfitExists);
        a_vm->RegisterFunction("CreateEmptyOutfit", kClass, CreateEmptyOutfit);
        a_vm->RegisterFunction("DeleteActorOutfit", kClass, DeleteActorOutfit);
        a_vm->RegisterFunction("LogActorOutfit", kClass, LogActorOutfit);
        a_vm->RegisterFunction("GetSlotName", kClass, GetSlotName);
        a_vm->RegisterFunction("GetDDSlotName", kClass, GetDDSlotName);
        a_vm->RegisterFunction("UpCase", kClass, UpCase);
        a_vm->RegisterFunction("MakeHeader", kClass, MakeHeader);
        a_vm->RegisterFunction("GetOutfitDevices", kClass, GetOutfitDevices);
        a_vm->RegisterFunction("SetOutfitDevices", kClass, SetOutfitDevices);
        a_vm->RegisterFunction("GetOutfitDevice", kClass, GetOutfitDevice);
        a_vm->RegisterFunction("NextDeviceToRemove", kClass, NextDeviceToRemove);
        a_vm->RegisterFunction("NextDeviceToAdd", kClass, NextDeviceToAdd);

        a_vm->RegisterFunction("AddAnimation", kClass, AddAnimation);
        a_vm->RegisterFunction("GetAnimation", kClass, GetAnimation);

        a_vm->RegisterFunction("GetActorsByRole", kClass, GetActorsByRole);
        a_vm->RegisterFunction("GetActorsByGlobalRole", kClass, GetActorsByGlobalRole);
        a_vm->RegisterFunction("GetActorLabyrinths", kClass, GetActorLabyrinths);
        a_vm->RegisterFunction("GetLabyrinthsByActorRole", kClass, GetLabyrinthsByActorRole);
        a_vm->RegisterFunction("HasRoleAnywhere", kClass, HasRoleAnywhere);
        a_vm->RegisterFunction("GetActorsByStatus", kClass, GetActorsByStatus);
        a_vm->RegisterFunction("ForgetActor", kClass, ForgetActor);

        a_vm->RegisterFunction("ConfigureKeywords", kClass, ConfigureKeywords);
        a_vm->RegisterFunction("RegisterLabyrinth", kClass, RegisterLabyrinth);
        a_vm->RegisterFunction("GetLabyrinths", kClass, GetLabyrinths);
        a_vm->RegisterFunction("TrySendAnimationEvent", kClass, TrySendAnimationEvent);
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
        a_vm->RegisterFunction("GetCellByDoor", kClass, GetCellByDoor);
        a_vm->RegisterFunction("GetCellOccupantCount", kClass, GetCellOccupantCount);
        a_vm->RegisterFunction("GetCellVacancy", kClass, GetCellVacancy);
        a_vm->RegisterFunction("GetCellOccupantAt", kClass, GetCellOccupantAt);
        a_vm->RegisterFunction("GetCellOccupants", kClass, GetCellOccupants);
        a_vm->RegisterFunction("GetCellFlags", kClass, GetCellFlags);
        a_vm->RegisterFunction("SetCellFlags", kClass, SetCellFlags);
        a_vm->RegisterFunction("GetCellLabyrinth", kClass, GetCellLabyrinth);
        a_vm->RegisterFunction("GetCells", kClass, GetCells);
        a_vm->RegisterFunction("AssignFreeCellToPrisoner", kClass, AssignFreeCellToPrisoner);
        a_vm->RegisterFunction("AssignPrisonerToCell", kClass, AssignPrisonerToCell);
        a_vm->RegisterFunction("GetActorCell", kClass, GetActorCell);
        a_vm->RegisterFunction("ClearActorCell", kClass, ClearActorCell);

        a_vm->RegisterFunction("GetMarkerRef", kClass, GetMarkerRef);
        a_vm->RegisterFunction("GetMarkerMaxOccupants", kClass, GetMarkerMaxOccupants);
        a_vm->RegisterFunction("SetMarkerMaxOccupants", kClass, SetMarkerMaxOccupants);
        a_vm->RegisterFunction("GetMarkerLabyrinth", kClass, GetMarkerLabyrinth);
        a_vm->RegisterFunction("GetMarkers", kClass, GetMarkers);

        a_vm->RegisterFunction("GetFurnitureRef", kClass, GetFurnitureRef);
        a_vm->RegisterFunction("GetFurnitureLabyrinth", kClass, GetFurnitureLabyrinth);
        a_vm->RegisterFunction("GetFurnitureFlags", kClass, GetFurnitureFlags);
        a_vm->RegisterFunction("SetFurnitureFlags", kClass, SetFurnitureFlags);
        a_vm->RegisterFunction("GetFurnitures", kClass, GetFurnitures);
        a_vm->RegisterFunction("GetFurnitureByRef", kClass, GetFurnitureByRef);
        a_vm->RegisterFunction("GetFurnitureVacancy", kClass, GetFurnitureVacancy);
        a_vm->RegisterFunction("GetFurnitureOccupant", kClass, GetFurnitureOccupant);
        a_vm->RegisterFunction("AssignFreeFurnitureToPrisoner", kClass, AssignFreeFurnitureToPrisoner);
        a_vm->RegisterFunction("AssignPrisonerToFurniture", kClass, AssignPrisonerToFurniture);
        a_vm->RegisterFunction("GetActorFurniture", kClass, GetActorFurniture);
        a_vm->RegisterFunction("ClearActorFurniture", kClass, ClearActorFurniture);

        logger::info("GKNative: registered actor + labyrinth/resource Papyrus functions.");
        return true;
    }
}
