#pragma once

#include <string_view>

namespace GK::FlagFilter {
    // Evaluates the resource-flag filter language against a flag string (one
    // flag per character):
    //   a       has flag a                 !a      does NOT have a
    //   (ab!c)  ALL terms hold (and)       [ab!c]  ANY term holds (or)
    //   X & Y   both sides hold            X | Y   either side holds
    // '&' binds tighter than '|'. Groups contain only flag terms (no nesting,
    // no operators inside). A bare term run outside a group acts like [...]
    // (any of), so "abc" keeps the legacy any-of meaning. Whitespace is
    // ignored; matching is case-insensitive (like every Papyrus string
    // vocabulary). An empty/blank filter matches everything; a MALFORMED one
    // matches nothing (a warning is logged).
    [[nodiscard]] bool Matches(std::string_view a_flags, std::string_view a_filter);
}
