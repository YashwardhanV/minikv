#include <gtest/gtest.h>

#include "minikv/tokenizer.hpp"

namespace minikv {
namespace {

using Tokens = std::vector<std::string>;

TEST(Tokenizer, PlainWords) {
    EXPECT_EQ(tokenize("SET name Alex"), (Tokens{"SET", "name", "Alex"}));
}

TEST(Tokenizer, EmptyAndWhitespaceOnlyLines) {
    EXPECT_TRUE(tokenize("").empty());
    EXPECT_TRUE(tokenize("   \t  ").empty());
}

TEST(Tokenizer, MultipleSpacesAndTabs) {
    EXPECT_EQ(tokenize("  SET \t\t name    Alex\t"), (Tokens{"SET", "name", "Alex"}));
}

TEST(Tokenizer, QuotedStringWithSpaces) {
    EXPECT_EQ(tokenize(R"(SET greeting "hello world")"),
              (Tokens{"SET", "greeting", "hello world"}));
}

TEST(Tokenizer, QuotedStringKeepsTabsAndRepeatedSpaces) {
    EXPECT_EQ(tokenize("\"a  \t b\""), (Tokens{"a  \t b"}));
}

TEST(Tokenizer, EscapedQuoteInsideQuotes) {
    EXPECT_EQ(tokenize(R"(SET q "say \"hi\"")"), (Tokens{"SET", "q", R"(say "hi")"}));
}

TEST(Tokenizer, EscapedBackslashInsideQuotes) {
    EXPECT_EQ(tokenize(R"("a\\b")"), (Tokens{R"(a\b)"}));
}

TEST(Tokenizer, OtherBackslashSequencesKeptAsIs) {
    EXPECT_EQ(tokenize(R"("line\nbreak")"), (Tokens{R"(line\nbreak)"}));
    EXPECT_EQ(tokenize(R"("tab\t")"), (Tokens{R"(tab\t)"}));
}

TEST(Tokenizer, BackslashOutsideQuotesIsLiteral) {
    EXPECT_EQ(tokenize(R"(a\"b c")"), (Tokens{R"(a\b c)"}));
    EXPECT_EQ(tokenize(R"(path\to)"), (Tokens{R"(path\to)"}));
}

TEST(Tokenizer, EmptyQuotedStringIsAToken) {
    EXPECT_EQ(tokenize(R"(SET k "")"), (Tokens{"SET", "k", ""}));
    EXPECT_EQ(tokenize(R"("" "")"), (Tokens{"", ""}));
}

TEST(Tokenizer, UnterminatedQuoteThrows) {
    EXPECT_THROW(tokenize(R"(SET k "oops)"), ParseError);
    EXPECT_THROW(tokenize(R"(")"), ParseError);
    // An escaped quote does not close the string.
    EXPECT_THROW(tokenize(R"("abc\")"), ParseError);
}

TEST(Tokenizer, UnterminatedQuoteMessage) {
    try {
        tokenize(R"("abc)");
        FAIL() << "expected ParseError";
    } catch (const ParseError &error) {
        EXPECT_STREQ(error.what(), "unterminated quote");
    }
}

TEST(Tokenizer, ParseErrorIsARuntimeError) { EXPECT_THROW(tokenize(R"(")"), std::runtime_error); }

TEST(Tokenizer, MidTokenQuoteStartsQuotedPart) {
    EXPECT_EQ(tokenize(R"(ab"c d")"), (Tokens{"abc d"}));
    EXPECT_EQ(tokenize(R"(x ab"c d"ef y)"), (Tokens{"x", "abc def", "y"}));
}

TEST(Tokenizer, CaseIsPreserved) {
    EXPECT_EQ(tokenize("Set KEY Value"), (Tokens{"Set", "KEY", "Value"}));
}

} // namespace
} // namespace minikv
