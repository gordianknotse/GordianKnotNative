#include "State/FlagFilter.h"

#include "State/CaseFold.h"

#include <cctype>
#include <string>

namespace {
    // Characters with syntactic meaning; anything else (non-whitespace) is a flag.
    constexpr std::string_view kSpecials = "!()[]&|";

    bool IsFlagChar(char a_ch) {
        return a_ch != '\0' && !std::isspace(static_cast<unsigned char>(a_ch)) &&
               kSpecials.find(a_ch) == std::string_view::npos;
    }

    // Recursive-descent evaluator over the (case-folded) filter, testing terms
    // directly against the (case-folded) flag string as it parses:
    //   or      := and ('|' and)*
    //   and     := primary ('&' primary)*
    //   primary := '(' term* ')'   all must hold
    //            | '[' term* ']'   any must hold
    //            | term+           bare run: any must hold (legacy any-of)
    //   term    := '!'? flag-char
    // No short-circuiting: both sides of an operator always parse, so malformed
    // input is always detected.
    class Parser {
    public:
        Parser(std::string_view a_filter, std::string_view a_flags) : _text(a_filter), _flags(a_flags) {}

        [[nodiscard]] bool ParseOr() {
            bool value = ParseAnd();
            while (_ok && Peek() == '|') {
                ++_pos;
                const bool rhs = ParseAnd();
                value = value || rhs;
            }
            return value;
        }

        [[nodiscard]] bool Ok() const { return _ok; }
        [[nodiscard]] bool AtEnd() { return Peek() == '\0'; }
        [[nodiscard]] std::size_t Pos() const { return _pos; }

    private:
        [[nodiscard]] bool ParseAnd() {
            bool value = ParsePrimary();
            while (_ok && Peek() == '&') {
                ++_pos;
                const bool rhs = ParsePrimary();
                value = value && rhs;
            }
            return value;
        }

        [[nodiscard]] bool ParsePrimary() {
            switch (Peek()) {
            case '(':
                ++_pos;
                return ParseGroup(')', false);
            case '[':
                ++_pos;
                return ParseGroup(']', true);
            default:
                return ParseTermRun();
            }
        }

        // Term list up to a_close. a_any: [] = any must hold; else () = all must
        // hold. Empty groups are vacuous: () -> true, [] -> false.
        [[nodiscard]] bool ParseGroup(char a_close, bool a_any) {
            bool value = !a_any;
            while (_ok && Peek() != a_close) {
                if (AtEnd()) {  // unterminated group
                    _ok = false;
                    return false;
                }
                const bool term = ParseTerm();
                value = a_any ? (value || term) : (value && term);
            }
            if (_ok) {
                ++_pos;  // consume the closer
            }
            return value;
        }

        // A bare run of terms outside any group: ANY of them (legacy any-of).
        [[nodiscard]] bool ParseTermRun() {
            char next = Peek();
            if (next != '!' && !IsFlagChar(next)) {  // empty operand (e.g. "a &", "()|")
                _ok = false;
                return false;
            }
            bool value = false;
            while (_ok && (next == '!' || IsFlagChar(next))) {
                const bool term = ParseTerm();
                value = value || term;
                next = Peek();
            }
            return value;
        }

        [[nodiscard]] bool ParseTerm() {
            bool negate = false;
            if (Peek() == '!') {
                ++_pos;
                negate = true;
            }
            const char ch = Peek();
            if (!IsFlagChar(ch)) {  // '!' with no flag, or a stray special
                _ok = false;
                return false;
            }
            ++_pos;
            const bool has = _flags.find(ch) != std::string_view::npos;
            return negate ? !has : has;
        }

        // Next meaningful character ('\0' at end); skips whitespace.
        [[nodiscard]] char Peek() {
            while (_pos < _text.size() && std::isspace(static_cast<unsigned char>(_text[_pos]))) {
                ++_pos;
            }
            return _pos < _text.size() ? _text[_pos] : '\0';
        }

        std::string_view _text;
        std::string_view _flags;
        std::size_t _pos = 0;
        bool _ok = true;
    };
}

namespace GK::FlagFilter {
    bool Matches(std::string_view a_flags, std::string_view a_filter) {
        const auto filter = FoldCase(a_filter);
        const auto flags = FoldCase(a_flags);
        Parser parser(filter, flags);
        if (parser.AtEnd()) {
            return true;  // empty/blank filter: no filter
        }
        const bool value = parser.ParseOr();
        if (!parser.Ok() || !parser.AtEnd()) {
            logger::warn("FlagFilter: malformed filter \"{}\" (near position {}); matching nothing.",
                         std::string(a_filter), parser.Pos());
            return false;
        }
        return value;
    }
}
