#pragma once

#include "State/Resource.h"

namespace GK {
    // A placed furniture reference an actor can use. Single occupant (maxOccupants
    // stays 1). The furniture reference is the discovery key.
    struct Furniture : OccupiableResource {
        RE::FormID ref = 0;

        [[nodiscard]] RE::FormID Key() const { return ref; }
    };
}
