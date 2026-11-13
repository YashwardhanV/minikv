#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace minikv {

/// Thrown by tokenize() when a command line cannot be split into tokens.
class ParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Splits one command line into tokens.
///
/// Tokens are separated by spaces or tabs. Double quotes group text that may
/// contain spaces; inside quotes `\"` is a literal quote and `\\` a literal
/// backslash, while any other backslash sequence is kept as typed. `""` is a
/// valid empty token, and a quote in the middle of a word starts a quoted part
/// (`ab"c d"` becomes `abc d`).
///
/// Throws ParseError("unterminated quote") if a quote is never closed.
std::vector<std::string> tokenize(const std::string &line);

} // namespace minikv
