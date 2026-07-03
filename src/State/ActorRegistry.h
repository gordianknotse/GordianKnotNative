#pragma once

#include "State/ActorState.h"

#include <unordered_map>
#include <vector>

namespace GK {
    // Tracks per-actor roles + status, keyed by FormID. The player (0x14) is just
    // another entry.
    //
    // NOT thread-safe on its own: GameState owns the instance and guards every call
    // with its mutex (see State/GameState.h). Operate on it only while holding that
    // lock.
    class ActorRegistry {
    public:
        // Ensure a_actor is tracked (no roles, idle status) if it isn't already. The
        // ADDING mutators below (Add*/Set*) also do this; the Remove* mutators never
        // do -- clearing a role on an untracked actor is a no-op. (The Papyrus binding
        // layer additionally gates adders on persistence; see GKNative.cpp.)
        void AddActor(RE::FormID a_actor) { GetOrCreate(a_actor); }

        // --- global roles (kGlobalMask; not tied to any labyrinth) ---
        void AddGlobalRole(RE::FormID a_actor, std::uint32_t a_role);
        void RemoveGlobalRole(RE::FormID a_actor, std::uint32_t a_role);
        [[nodiscard]] std::uint32_t GetGlobalRoles(RE::FormID a_actor) const;
        // FormIDs of every tracked actor whose global mask intersects a_roleMask.
        [[nodiscard]] std::vector<RE::FormID> GetByGlobalRole(std::uint32_t a_roleMask) const;

        // --- scoped roles (kScopedMask; a_lab is the anchor-REFR FormID) ---
        // Setting a labyrinth's mask to 0 (SetRoles / RemoveRole) drops that association.
        void SetRoles(RE::FormID a_actor, RE::FormID a_lab, std::uint32_t a_roles);
        void AddRole(RE::FormID a_actor, RE::FormID a_lab, std::uint32_t a_role);
        void RemoveRole(RE::FormID a_actor, RE::FormID a_lab, std::uint32_t a_role);
        [[nodiscard]] std::uint32_t GetRoles(RE::FormID a_actor, RE::FormID a_lab) const;

        void SetStatus(RE::FormID a_actor, std::int32_t a_status);
        [[nodiscard]] std::int32_t GetStatus(RE::FormID a_actor) const;

        // FormIDs of every tracked actor whose role mask in a_lab intersects a_roleMask.
        [[nodiscard]] std::vector<RE::FormID> GetByRole(RE::FormID a_lab, std::uint32_t a_roleMask) const;
        // FormIDs of every tracked actor whose scoped role mask in ANY labyrinth
        // intersects a_roleMask (each actor listed once).
        [[nodiscard]] std::vector<RE::FormID> GetByRoleAnywhere(std::uint32_t a_roleMask) const;
        // FormIDs of every tracked actor whose status equals a_status.
        [[nodiscard]] std::vector<RE::FormID> GetByStatus(std::int32_t a_status) const;

        // Anchor FormIDs of every labyrinth in which a_actor holds any (scoped) role.
        [[nodiscard]] std::vector<RE::FormID> GetLabyrinths(RE::FormID a_actor) const;
        // Anchor FormIDs of the labyrinths where a_actor's role mask intersects a_roleMask.
        [[nodiscard]] std::vector<RE::FormID> GetLabyrinthsByRole(RE::FormID a_actor, std::uint32_t a_roleMask) const;
        // True if a_actor holds any role intersecting a_roleMask either globally OR in
        // any labyrinth (so it works for both global and scoped role bits).
        [[nodiscard]] bool HasRoleAnywhere(RE::FormID a_actor, std::uint32_t a_roleMask) const;

        // Inserts or overwrites a record wholesale (used by serialization load).
        void Put(RE::FormID a_actor, const ActorRecord& a_record) { _records[a_actor] = a_record; }

        void Forget(RE::FormID a_actor);
        void Clear();

        // Read access to the backing store (used by serialization, Phase 2).
        [[nodiscard]] const std::unordered_map<RE::FormID, ActorRecord>& Records() const { return _records; }

    private:
        // Returns the record for a_actor, default-creating one if absent.
        ActorRecord& GetOrCreate(RE::FormID a_actor) { return _records[a_actor]; }

        std::unordered_map<RE::FormID, ActorRecord> _records;
    };
}
