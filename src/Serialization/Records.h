#pragma once

#include <cstdint>

// =============================================================================
// SKSE co-save record codes + schema versions. See docs/PLAN-V1.md §7.
//
// ADDITIVE-ONLY: never reuse or repurpose a record code, and never change the
// meaning/order of existing fields. To evolve a record, append new fields and
// bump its Version::, then branch on the version in the loader.
// =============================================================================

namespace GK::Serialization {
    // Packs a 4-char tag into a uint32. Avoids implementation-defined multi-char
    // char literals ('ABCD') so the codes are stable and warning-free.
    inline constexpr std::uint32_t FourCC(char a_a, char a_b, char a_c, char a_d) {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(a_a)) << 24) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(a_b)) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(a_c)) << 8) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(a_d)));
    }

    // SKSE co-save unique ID for this plugin's records.
    inline constexpr std::uint32_t kUniqueID = FourCC('G', 'K', 'N', 'T');

    namespace Record {
        inline constexpr std::uint32_t kActor = FourCC('A', 'C', 'T', 'R');  // Phase 2
        inline constexpr std::uint32_t kQueue = FourCC('Q', 'U', 'E', 'U');  // named actor FIFO queues
        // Phase 5 (reserved): 'NEXT' handle counter, 'LABY', 'CELL', 'MARK',
        //                     'FURN', 'ORPH'.
    }

    namespace Version {
        // v1: single role mask per actor. v2: roles scoped per labyrinth (map
        // anchor-FormID -> mask). v3: adds a per-actor global role mask (Wanderer).
        // Older saves load best-effort: v1 drops roles (keeps status); v2 loads
        // scoped roles with an empty global mask.
        inline constexpr std::uint32_t kActor = 3;
        // v1: queueCount, then per queue: nameLen, name bytes (case-folded, no
        // terminator), entryCount, actor FormIDs front-to-back.
        inline constexpr std::uint32_t kQueue = 1;
    }
}
