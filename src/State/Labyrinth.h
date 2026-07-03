#pragma once

#include <unordered_map>

// =============================================================================
// Labyrinths + keyword-driven resource discovery (Encoding B).
//
// A labyrinth is identified directly by its anchor XMarker reference: the anchor
// both names the labyrinth and gives it a position. Each resource reference links
// to that anchor via a type keyword (GK_CellDoor / GK_PatrolMarker /
// GK_Furniture); a cell door also links to its in/out markers (GK_InMarker /
// GK_OutMarker). Discovery probes those linked refs and matches the target
// against the registered anchors. See docs/PLAN-V1.md §2/§6.
// =============================================================================

namespace GK {
    // Per-labyrinth role factions, supplied by RegisterLabyrinth. Live pointers
    // (session config, like ResourceKeywords -- re-supplied by Papyrus each load,
    // never serialized). The same faction may back many labyrinths; either may be
    // null, which disables faction sync for that role.
    struct LabyrinthFactions {
        RE::TESFaction* warden = nullptr;
        RE::TESFaction* prisoner = nullptr;
    };

    // The registered labyrinths, keyed by anchor REFR FormID. A resource belongs to
    // a labyrinth iff its type linked-ref targets one of these anchors.
    // NOT thread-safe; used under the GameState lock.
    class LabyrinthRegistry {
    public:
        void Register(RE::FormID a_anchor, RE::TESFaction* a_wardenFaction, RE::TESFaction* a_prisonerFaction) {
            _labyrinths[a_anchor] = LabyrinthFactions{a_wardenFaction, a_prisonerFaction};
        }

        // Is this FormID a registered labyrinth anchor?
        [[nodiscard]] bool Contains(RE::FormID a_anchor) const { return _labyrinths.contains(a_anchor); }

        // The labyrinth's role factions (nullptr if the anchor isn't registered).
        [[nodiscard]] const LabyrinthFactions* Find(RE::FormID a_anchor) const {
            const auto it = _labyrinths.find(a_anchor);
            return it != _labyrinths.end() ? &it->second : nullptr;
        }

        [[nodiscard]] bool Empty() const { return _labyrinths.empty(); }

        void Clear() { _labyrinths.clear(); }

        [[nodiscard]] const std::unordered_map<RE::FormID, LabyrinthFactions>& All() const { return _labyrinths; }

    private:
        std::unordered_map<RE::FormID, LabyrinthFactions> _labyrinths;
    };

    // The type keywords used during discovery. Passed in each session via
    // ConfigureKeywords (live BGSKeyword* pointers, not persisted — re-supplied by
    // Papyrus on load). cellDoor/patrolMarker/furniture/in/out identify resource
    // references; warden identifies an ACTOR reference placed as a labyrinth's warden
    // (its linked-ref target is the labyrinth anchor); wanderer identifies an ACTOR
    // reference with the global Wanderer role — its linked-ref TARGET is ignored
    // (wanderers belong to no labyrinth; the linked ref just marks the role and, as a
    // side effect, makes the ref ESP-persistent so the scan can find it unloaded).
    struct ResourceKeywords {
        RE::BGSKeyword* cellDoor = nullptr;
        RE::BGSKeyword* patrolMarker = nullptr;
        RE::BGSKeyword* furniture = nullptr;
        RE::BGSKeyword* inMarker = nullptr;
        RE::BGSKeyword* outMarker = nullptr;
        RE::BGSKeyword* warden = nullptr;
        RE::BGSKeyword* wanderer = nullptr;

        [[nodiscard]] bool Valid() const {
            return cellDoor && patrolMarker && furniture && inMarker && outMarker && warden && wanderer;
        }
    };

    // One-shot global discovery: sweeps the entire instantiated-form table for
    // resources belonging to any registered labyrinth, WITHOUT requiring their cells
    // to be loaded. Finds only PERSISTENT references (see notes in the .cpp). Call
    // once after registration to bring the whole (persisted) dungeon online up front.
    // Idempotent (re-discovering a resource preserves its handle/occupancy). Acquires
    // the GameState lock internally. Returns total resources matched.
    int ScanAllForms();
}
