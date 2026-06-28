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
        void SetRoles(RE::FormID a_actor, std::uint32_t a_roles);
        void AddRole(RE::FormID a_actor, std::uint32_t a_role);
        void RemoveRole(RE::FormID a_actor, std::uint32_t a_role);
        [[nodiscard]] std::uint32_t GetRoles(RE::FormID a_actor) const;

        void SetStatus(RE::FormID a_actor, std::int32_t a_status);
        [[nodiscard]] std::int32_t GetStatus(RE::FormID a_actor) const;

        // FormIDs of every tracked actor whose roles intersect a_roleMask.
        [[nodiscard]] std::vector<RE::FormID> GetByRole(std::uint32_t a_roleMask) const;
        // FormIDs of every tracked actor whose status equals a_status.
        [[nodiscard]] std::vector<RE::FormID> GetByStatus(std::int32_t a_status) const;

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
