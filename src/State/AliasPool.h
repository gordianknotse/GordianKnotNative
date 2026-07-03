#pragma once

#include <cstdint>

namespace GK {
    class GameState;
}

// =============================================================================
// The GkNpc alias pool: pre-authored ReferenceAliases (GkNpcAlias000, 001, ...)
// on the quest supplied via ConfigureAliasQuest. Filling an actor into one both
// makes it persistent AND binds its per-NPC driver script -- the two things a
// tracked actor needs. Used by every tracking entry point: the Papyrus adding
// mutators AND scan discovery (ScanAllForms) push new actors through
// EnsureTrackable, so no CK-side script attachment is ever required.
//
// The fill itself is the engine's own ForceRefTo, dispatched through the Papyrus
// VM; a reservation (in GameState) covers the async window so two threads can't
// be handed the same slot. It is consumed once the alias is seen filled, and
// expires if the fill never lands.
// =============================================================================

namespace GK::AliasPool {
    // Pool membership is by alias-name prefix (case-insensitive).
    inline constexpr std::string_view kPrefix = "GkNpc"sv;

    [[nodiscard]] bool IsPoolAlias(const RE::BGSBaseAlias& a_alias);

    // Reservation-map key for one alias slot on one quest (see GameState::AliasReservations).
    [[nodiscard]] std::uint64_t Key(const RE::TESQuest& a_quest, std::uint32_t a_aliasID);

    // The pool alias currently holding a_actor, or nullptr.
    [[nodiscard]] RE::BGSRefAlias* FindHolding(RE::TESQuest& a_quest, const RE::Actor& a_actor);

    // Gate for every tracking entry point: an already-tracked actor passes through;
    // a new one is reserved a free pool alias and filled into it (ForceRefTo via the
    // VM), then the constructor hook `OnGKAssign(Actor)` is dispatched on the
    // alias's driver script. False -> do NOT track (no alias quest configured /
    // pool exhausted / dispatch failed). Caller holds the GameState lock.
    [[nodiscard]] bool EnsureTrackable(GameState& a_state, RE::Actor& a_actor);

    // Release the pool alias holding a_actor, if any: first dispatches the optional
    // destructor hook `OnGKRelease(Actor)` on the alias's driver script, then Clear.
    // Caller holds the GameState lock.
    void Release(GameState& a_state, RE::Actor& a_actor);

    // Diagnostic: pool aliases on a_quest that are empty and not actively reserved.
    // Caller holds the GameState lock.
    [[nodiscard]] std::int32_t CountFree(GameState& a_state, RE::TESQuest& a_quest);
}