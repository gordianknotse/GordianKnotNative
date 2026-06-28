#pragma once

#include <cstdint>

// =============================================================================
// Per-actor tracked state: roles + status. The player (FormID 0x14) is tracked
// the same as any NPC. See docs/PLAN-V1.md §4.
// =============================================================================

namespace GK {
    // Actor roles are a native-defined bitmask; an actor can hold any combination
    // (e.g. Wanderer | Warden), and the set is dynamic. Mirrored as Papyrus int
    // constants (GK_ROLE_*) in the Papyrus repo's GKNative.psc.
    namespace Role {
        inline constexpr std::uint32_t kNone = 0;
        inline constexpr std::uint32_t kWanderer = 1u << 0;  // 0x1
        inline constexpr std::uint32_t kWarden = 1u << 1;    // 0x2
    }

    // Status is an opaque, Papyrus-owned vocabulary describing what the actor is
    // busy with. Native reserves only 0 = Idle; Papyrus defines the rest.
    namespace Status {
        inline constexpr std::int32_t kIdle = 0;
    }

    struct ActorRecord {
        std::uint32_t roles = Role::kNone;
        std::int32_t status = Status::kIdle;
    };
}
