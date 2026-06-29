#pragma once

#include <unordered_map>

// =============================================================================
// Labyrinths + keyword-driven resource discovery (Encoding B).
//
// A labyrinth is identified by a Keyword and anchored by an XMarker reference
// (its position). Each resource reference links to its labyrinth's anchor via a
// type keyword (GK_CellDoor / GK_PatrolMarker / GK_Furniture); a cell door also
// links to its in/out markers (GK_InMarker / GK_OutMarker). Discovery enumerates
// loaded cell references and probes those linked refs. See docs/PLAN-V1.md §2/§6.
// =============================================================================

namespace GK {
    struct Labyrinth {
        RE::FormID keyword = 0;  // labyrinth keyword FormID
        RE::FormID anchor = 0;   // anchor XMarker REFR FormID
    };

    // keyword <-> anchor registry. NOT thread-safe; used under the GameState lock.
    class LabyrinthRegistry {
    public:
        void Register(RE::FormID a_keyword, RE::FormID a_anchor) {
            _byKeyword[a_keyword] = Labyrinth{a_keyword, a_anchor};
        }

        [[nodiscard]] const Labyrinth* Find(RE::FormID a_keyword) const {
            const auto it = _byKeyword.find(a_keyword);
            return it != _byKeyword.end() ? &it->second : nullptr;
        }

        [[nodiscard]] RE::FormID AnchorOf(RE::FormID a_keyword) const {
            const auto* lab = Find(a_keyword);
            return lab ? lab->anchor : 0;
        }

        // Reverse lookup: which labyrinth keyword owns this anchor (0 if none).
        // Linear over a handful of labyrinths — used per discovered reference.
        [[nodiscard]] RE::FormID KeywordForAnchor(RE::FormID a_anchor) const {
            for (const auto& [kw, lab] : _byKeyword) {
                if (lab.anchor == a_anchor) {
                    return kw;
                }
            }
            return 0;
        }

        [[nodiscard]] bool Empty() const { return _byKeyword.empty(); }

        void Clear() { _byKeyword.clear(); }

        [[nodiscard]] std::unordered_map<RE::FormID, Labyrinth>& All() { return _byKeyword; }
        [[nodiscard]] const std::unordered_map<RE::FormID, Labyrinth>& All() const { return _byKeyword; }

    private:
        std::unordered_map<RE::FormID, Labyrinth> _byKeyword;
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
