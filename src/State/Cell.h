#pragma once

#include "State/Resource.h"

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

        [[nodiscard]] RE::FormID Key() const { return door; }
    };
}
