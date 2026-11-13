#include "minikv/glob.hpp"

namespace minikv {

bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t p = 0; // position in pattern
    std::size_t t = 0; // position in text

    // Where the most recent '*' was, and how much text it has swallowed so far.
    // If a later character fails to match, we retry by letting that '*'
    // swallow one more character. Only the latest star needs remembering,
    // which is what keeps this linear-ish instead of exponential.
    std::size_t star = std::string_view::npos;
    std::size_t star_text = 0;

    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            // Check '*' first so a literal '*' in the text never "uses up" a wildcard.
            star = p++;
            star_text = t;
        } else if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++star_text;
        } else {
            return false;
        }
    }

    // Text is used up; only trailing stars may remain in the pattern.
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

} // namespace minikv
