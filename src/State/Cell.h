#pragma once

#include "State/CaseFold.h"
#include "State/Resource.h"

#include <string>
#include <string_view>

namespace GK {
    // Default cell capacity on first discovery; Papyrus can override per cell via
    // SetCellMaxOccupants.
    inline constexpr std::uint32_t kDefaultCellMaxOccupants = 1;

    // A holding cell: a door (the discovery key) plus the markers a prisoner is
    // moved to (in) and a warden stands at to work the cell (out).
    struct Cell : OccupiableResource {
        RE::FormID door = 0;
        RE::FormID inMarker = 0;
        RE::FormID outMarker = 0;
        std::string flags;  // one flag per character; "" = no flags

        [[nodiscard]] RE::FormID Key() const { return door; }

        // Flag filter used by the cell getters/assigners: true when the cell has
        // at least ONE of a_anyOfFlags' characters, or when the filter is empty
        // (no filter). Case-insensitive, like every Papyrus string vocabulary.
        [[nodiscard]] bool HasAnyFlagOf(std::string_view a_anyOfFlags) const {
            if (a_anyOfFlags.empty()) {
                return true;
            }
            const auto have = FoldCase(flags);
            const auto want = FoldCase(a_anyOfFlags);
            return have.find_first_of(want) != std::string::npos;
        }
    };
}
