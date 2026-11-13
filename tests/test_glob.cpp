#include <gtest/gtest.h>

#include <string>

#include "minikv/glob.hpp"

namespace minikv {
namespace {

TEST(Glob, StarMatchesAnything) {
    EXPECT_TRUE(glob_match("*", ""));
    EXPECT_TRUE(glob_match("*", "anything"));
    EXPECT_TRUE(glob_match("user:*", "user:42"));
    EXPECT_TRUE(glob_match("user:*", "user:"));
    EXPECT_TRUE(glob_match("*:name", "user:name"));
    EXPECT_TRUE(glob_match("**", "abc"));
}

TEST(Glob, QuestionMatchesExactlyOne) {
    EXPECT_TRUE(glob_match("?", "a"));
    EXPECT_FALSE(glob_match("?", ""));
    EXPECT_FALSE(glob_match("?", "ab"));
    EXPECT_TRUE(glob_match("h?llo", "hello"));
    EXPECT_TRUE(glob_match("h?llo", "hallo"));
    EXPECT_FALSE(glob_match("h?llo", "hllo"));
}

TEST(Glob, Literal) {
    EXPECT_TRUE(glob_match("name", "name"));
    EXPECT_FALSE(glob_match("name", "Name"));
    EXPECT_FALSE(glob_match("name", "names"));
    EXPECT_FALSE(glob_match("names", "name"));
}

TEST(Glob, EmptyPattern) {
    EXPECT_TRUE(glob_match("", ""));
    EXPECT_FALSE(glob_match("", "a"));
}

TEST(Glob, EmptyText) {
    EXPECT_TRUE(glob_match("*", ""));
    EXPECT_TRUE(glob_match("***", ""));
    EXPECT_FALSE(glob_match("a", ""));
    EXPECT_FALSE(glob_match("*a", ""));
}

TEST(Glob, StarAStarBStar) {
    EXPECT_TRUE(glob_match("*a*b*", "ab"));
    EXPECT_TRUE(glob_match("*a*b*", "xxaxxbxx"));
    EXPECT_TRUE(glob_match("*a*b*", "aaabbb"));
    EXPECT_FALSE(glob_match("*a*b*", "ba"));
    EXPECT_FALSE(glob_match("*a*b*", "aaaa"));
}

TEST(Glob, NoMatchCases) {
    EXPECT_FALSE(glob_match("a*", "ba"));
    EXPECT_FALSE(glob_match("*a", "ab"));
    EXPECT_FALSE(glob_match("a?c", "ac"));
    EXPECT_FALSE(glob_match("abc", "abd"));
}

TEST(Glob, BacktracksToLatestStar) {
    EXPECT_TRUE(glob_match("*abc", "ababc"));
    EXPECT_TRUE(glob_match("a*b?d", "axxbxbcd"));
}

TEST(Glob, SpecialCharactersInTextAreLiteral) {
    // A '*' or '?' in the key is just a character; the pattern '*' stays a wildcard.
    EXPECT_TRUE(glob_match("*a", "*ba"));
    EXPECT_TRUE(glob_match("x*", "x*y"));
    EXPECT_TRUE(glob_match("[a]", "[a]"));
    EXPECT_FALSE(glob_match("[a]", "a"));
}

TEST(Glob, PathologicalInputFinishesInstantly) {
    // A naive recursive matcher explores a huge number of splits here.
    EXPECT_FALSE(glob_match("a*a*a*a*a*b", std::string(30, 'a')));
    EXPECT_FALSE(glob_match("a*a*a*a*a*a*a*a*a*a*b", std::string(5000, 'a')));
    EXPECT_TRUE(glob_match("a*a*a*a*a*a*a*a*a*a*b", std::string(5000, 'a') + "b"));
}

} // namespace
} // namespace minikv
