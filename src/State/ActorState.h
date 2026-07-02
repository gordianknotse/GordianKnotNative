#pragma once

#include <cstdint>
#include <unordered_map>

// =============================================================================
// Per-actor tracked state: labyrinth-scoped roles + a global status. The player
// (FormID 0x14) is tracked the same as any NPC. See docs/PLAN-V1.md §4.
// =============================================================================

namespace GK {
    // Actor roles are a native-defined bitmask; an actor can hold any combination
    // (e.g. Wanderer | Warden), and the set is dynamic. Mirrored as Papyrus int
    // constants (the Role*() getters) in the interface's GordianKnotNative.psc.
    //
    // Roles are one of two kinds:
    //  * GLOBAL   - a plain attribute of the actor, not tied to any labyrinth
    //               (Wanderer: it just wanders). Stored in ActorRecord::globalRoles.
    //  * SCOPED   - an association WITH a specific labyrinth, so the actor can hold
    //               it in one labyrinth but not another (Warden of A, Prisoner of B).
    //               Stored per anchor in ActorRecord::rolesByLab.
    // Every role bit must appear in exactly one of kGlobalMask / kScopedMask.
    namespace Role {
        inline constexpr std::uint32_t kNone = 0;
        inline constexpr std::uint32_t kWanderer = 1u << 0;  // 0x1  (global)
        inline constexpr std::uint32_t kWarden = 1u << 1;    // 0x2  (scoped)
        inline constexpr std::uint32_t kPrisoner = 1u << 2;  // 0x4  (scoped)

        inline constexpr std::uint32_t kGlobalMask = kWanderer;
        inline constexpr std::uint32_t kScopedMask = kWarden | kPrisoner;
    }

    // Status is an opaque, Papyrus-owned vocabulary describing what the actor is
    // busy with. It is global to the actor (one thing at a time), NOT scoped to a
    // labyrinth. Native reserves only 0 = Idle; Papyrus defines the rest.
    namespace Status {
        inline constexpr std::int32_t kIdle = 0;
    }

    struct ActorRecord {
        // Global roles (kGlobalMask): a plain per-actor attribute, no labyrinth.
        std::uint32_t globalRoles = Role::kNone;
        // Scoped roles (kScopedMask): an association between the actor and a labyrinth
        // (its anchor-REFR FormID), so the same actor can be a Warden of A while a
        // Prisoner of B. Keys are anchor FormIDs; values are that labyrinth's role
        // mask. An entry is dropped when its mask reaches 0 (see RemoveRole/SetRoles).
        std::unordered_map<RE::FormID, std::uint32_t> rolesByLab;
        std::int32_t status = Status::kIdle;
    };
}
