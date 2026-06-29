#pragma once

#include "State/Resource.h"

namespace GK {
    inline constexpr std::uint32_t kDefaultMarkerMaxOccupants = 1;

    // A patrol marker (an XMarker the actor walks to). The marker reference is the
    // discovery key.
    struct Marker : OccupiableResource {
        RE::FormID ref = 0;

        [[nodiscard]] RE::FormID Key() const { return ref; }
    };
}
