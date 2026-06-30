#pragma once

#include <unordered_set>

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
    // The set of registered labyrinth anchors (REFR FormIDs). A resource belongs to
    // a labyrinth iff its type linked-ref targets one of these anchors.
    // NOT thread-safe; used under the GameState lock.
    class LabyrinthRegistry {
    public:
        void Register(RE::FormID a_anchor) { _anchors.insert(a_anchor); }

        // Is this FormID a registered labyrinth anchor?
        [[nodiscard]] bool Contains(RE::FormID a_anchor) const { return _anchors.contains(a_anchor); }

        [[nodiscard]] bool Empty() const { return _anchors.empty(); }

        void Clear() { _anchors.clear(); }

        [[nodiscard]] std::unordered_set<RE::FormID>& All() { return _anchors; }
        [[nodiscard]] const std::unordered_set<RE::FormID>& All() const { return _anchors; }

    private:
        std::unordered_set<RE::FormID> _anchors;
    };

    // The five type keywords used during discovery. Passed in each session via
    // ConfigureKeywords (live BGSKeyword* pointers, not persisted — re-supplied by
    // Papyrus on load).
    struct ResourceKeywords {
        RE::BGSKeyword* cellDoor = nullptr;
        RE::BGSKeyword* patrolMarker = nullptr;
        RE::BGSKeyword* furniture = nullptr;
        RE::BGSKeyword* inMarker = nullptr;
        RE::BGSKeyword* outMarker = nullptr;

        [[nodiscard]] bool Valid() const {
            return cellDoor && patrolMarker && furniture && inMarker && outMarker;
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
