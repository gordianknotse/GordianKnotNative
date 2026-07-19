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
        inline constexpr std::uint32_t kActor = FourCC('A', 'C', 'T', 'R');      // Phase 2
        inline constexpr std::uint32_t kAttribute = FourCC('A', 'T', 'T', 'R');     // per-actor keyed attributes
        inline constexpr std::uint32_t kIntAttribute = FourCC('A', 'T', 'T', 'I');    // per-actor keyed int attributes
        inline constexpr std::uint32_t kArrayAttribute = FourCC('A', 'T', 'T', 'A');  // per-actor keyed Form arrays
        inline constexpr std::uint32_t kQueue = FourCC('Q', 'U', 'E', 'U');      // named actor FIFO queues
        // Phase 5 (reserved): 'NEXT' handle counter, 'LABY', 'CELL', 'MARK',
        //                     'FURN', 'ORPH'.
    }

    namespace Version {
        // v1: single role mask per actor. v2: roles scoped per labyrinth (map
        // anchor-FormID -> mask). v3: adds a per-actor global role mask (Wanderer).
        // v4: status is a String (len + bytes) instead of an int32 code.
        // Older saves load best-effort: v1 drops roles (keeps status); v2 loads
        // scoped roles with an empty global mask; pre-v4 int statuses load as
        // their decimal spelling (0 -> "idle").
        inline constexpr std::uint32_t kActor = 4;
        // v1: actorCount, then per actor: actorID, attrCount, then per attribute:
        // keyLen, key bytes (case-folded, no terminator), value FormID.
        inline constexpr std::uint32_t kAttribute = 1;
        // v1: actorCount, then per actor: actorID, attrCount, then per attribute:
        // keyLen, key bytes (case-folded, no terminator), int32 value.
        inline constexpr std::uint32_t kIntAttribute = 1;
        // v1: actorCount, then per actor: actorID, attrCount, then per attribute:
        // keyLen, key bytes (case-folded, no terminator), valueCount, FormIDs in
        // array order.
        inline constexpr std::uint32_t kArrayAttribute = 1;
        // v1: queueCount, then per queue: nameLen, name bytes (case-folded, no
        // terminator), entryCount, actor FormIDs front-to-back.
        // v2: appends the delayed enqueues: delayedCount, then per entry: nameLen,
        // name bytes (case-folded), actor FormID, remaining seconds (double --
        // rebased onto the session clock at load). v1 saves load with none.
        inline constexpr std::uint32_t kQueue = 2;
    }
}
