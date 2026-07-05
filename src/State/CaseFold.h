#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace GK {
    // Lowercased copy of a_text, used as the map key / compare operand wherever a
    // Papyrus-facing string vocabulary (queue names, attribute keys, statuses) must
    // match Papyrus's case-insensitive string semantics.
    [[nodiscard]] inline std::string FoldCase(std::string_view a_text) {
        std::string folded(a_text);
        std::ranges::transform(folded, folded.begin(),
                               [](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });
        return folded;
    }
}
