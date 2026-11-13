#include "minikv/tokenizer.hpp"

namespace minikv {

std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_token = false;  // true once the current token has started (even if empty: "")
    bool in_quotes = false; // true between an opening and closing double quote

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (in_quotes) {
            const bool has_next = i + 1 < line.size();
            if (c == '\\' && has_next && (line[i + 1] == '"' || line[i + 1] == '\\')) {
                // Only \" and \\ are escapes; anything else is kept as typed.
                current += line[i + 1];
                ++i;
            } else if (c == '"') {
                in_quotes = false;
            } else {
                current += c;
            }
            continue;
        }

        if (c == ' ' || c == '\t') {
            if (in_token) {
                tokens.push_back(current);
                current.clear();
                in_token = false;
            }
        } else if (c == '"') {
            // A quote starts quoted text, even in the middle of a word.
            in_quotes = true;
            in_token = true;
        } else {
            current += c;
            in_token = true;
        }
    }

    if (in_quotes) {
        throw ParseError("unterminated quote");
    }
    if (in_token) {
        tokens.push_back(current);
    }
    return tokens;
}

} // namespace minikv
