#pragma once

#include <string_view>

namespace minikv {

/// Returns true if `text` matches the glob `pattern`.
///
/// `*` matches any run of characters (including none), `?` matches exactly one
/// character, and every other character matches itself. There are no character
/// classes and no escaping, so no pattern is ever invalid.
///
/// Runs in O(pattern * text) worst case with no recursion, so hostile patterns
/// such as `a*a*a*a*b` cannot cause exponential backtracking.
bool glob_match(std::string_view pattern, std::string_view text);

} // namespace minikv
