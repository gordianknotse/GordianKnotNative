#pragma once

#include <cstdint>

namespace GK {
    class GameState;
}

// =============================================================================
// Scoped-role <-> faction sync. Each labyrinth registers a warden and a prisoner
// faction (RegisterLabyrinth); when an actor gains a scoped role there -- via a
// Papyrus adding mutator OR scan discovery -- it joins the matching faction, and
// when the role is cleared it leaves it again, UNLESS another labyrinth where the
// actor still holds a role grants that same faction (factions may be shared).
//
// Membership changes are queued through the VM (Actor.AddToFaction /
// RemoveFromFaction): idempotent, safe from any thread, and persisted by the
// game's own save -- so roles restored from the co-save need no resync.
// =============================================================================

namespace GK::RoleFactions {
    // Sync faction membership after a scoped-role change on a_actor for
    // a_labyrinth. Call AFTER the ActorRegistry mutation, with the bits that were
    // actually added/removed (the removal check inspects the actor's remaining
    // roles). Unregistered labyrinth / null faction -> no-op for that bit. Caller
    // holds the GameState lock.
    void Apply(GameState& a_state, RE::Actor& a_actor, RE::FormID a_labyrinth, std::uint32_t a_addedRoles,
               std::uint32_t a_removedRoles);
}
