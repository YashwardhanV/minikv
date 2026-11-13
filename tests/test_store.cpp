#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include "minikv/clock.hpp"
#include "minikv/store.hpp"

namespace minikv {
namespace {

class StoreTest : public ::testing::Test {
  protected:
    FakeClock clock{1'000'000};
    Store store{clock};
};

TEST_F(StoreTest, SetAndGet) {
    store.set("name", "Alex");
    ASSERT_TRUE(store.get("name").has_value());
    EXPECT_EQ(*store.get("name"), "Alex");
}

TEST_F(StoreTest, GetMissingIsNullopt) { EXPECT_FALSE(store.get("nope").has_value()); }

TEST_F(StoreTest, KeysAndValuesAreCaseSensitive) {
    store.set("Key", "Value");
    EXPECT_FALSE(store.exists("key"));
    EXPECT_EQ(*store.get("Key"), "Value");
}

TEST_F(StoreTest, Overwrite) {
    store.set("k", "one");
    store.set("k", "two");
    EXPECT_EQ(*store.get("k"), "two");
    EXPECT_EQ(store.size(), 1u);
}

TEST_F(StoreTest, EmptyValueIsStored) {
    store.set("k", "");
    ASSERT_TRUE(store.get("k").has_value());
    EXPECT_EQ(*store.get("k"), "");
}

TEST_F(StoreTest, EraseAndExists) {
    store.set("k", "v");
    EXPECT_TRUE(store.exists("k"));
    EXPECT_TRUE(store.erase("k"));
    EXPECT_FALSE(store.exists("k"));
    EXPECT_FALSE(store.erase("k"));
}

TEST_F(StoreTest, SizeCountsKeys) {
    EXPECT_EQ(store.size(), 0u);
    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");
    EXPECT_EQ(store.size(), 3u);
    store.erase("b");
    EXPECT_EQ(store.size(), 2u);
}

TEST_F(StoreTest, KeysAreSorted) {
    for (const char *key : {"zeta", "alpha", "mid", "Beta", "alpha2"}) {
        store.set(key, "x");
    }
    EXPECT_EQ(store.keys(), (std::vector<std::string>{"Beta", "alpha", "alpha2", "mid", "zeta"}));
}

TEST_F(StoreTest, KeysWithPattern) {
    store.set("user:1", "a");
    store.set("user:2", "b");
    store.set("order:1", "c");
    EXPECT_EQ(store.keys("user:*"), (std::vector<std::string>{"user:1", "user:2"}));
    EXPECT_EQ(store.keys("*:1"), (std::vector<std::string>{"order:1", "user:1"}));
    EXPECT_TRUE(store.keys("nothing*").empty());
}

TEST_F(StoreTest, ClearRemovesEverything) {
    store.set("a", "1");
    store.set("b", "2");
    store.clear();
    EXPECT_EQ(store.size(), 0u);
}

TEST_F(StoreTest, PutEntryRestoresValueAndExpiry) {
    store.put_entry("k", Entry{"v", clock.now_ms() + 5000});
    const auto entry = store.get_entry("k");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->value, "v");
    EXPECT_EQ(entry->expires_at_ms, clock.now_ms() + 5000);
}

TEST_F(StoreTest, PeekDoesNotModify) {
    store.set("k", "v");
    EXPECT_EQ(store.peek("k"), (Entry{"v", std::nullopt}));
    EXPECT_FALSE(store.peek("missing").has_value());
}

TEST_F(StoreTest, IncrByOnMissingKeyStartsAtZero) {
    EXPECT_EQ(store.incr_by("n", 5), 5);
    EXPECT_EQ(*store.get("n"), "5");
    EXPECT_EQ(store.incr_by("m", -3), -3);
}

TEST_F(StoreTest, IncrByNonIntegerFailsAndLeavesValue) {
    store.set("n", "abc");
    EXPECT_FALSE(store.incr_by("n", 1).has_value());
    EXPECT_EQ(*store.get("n"), "abc");
}

TEST_F(StoreTest, IncrByOverflowFailsAndLeavesValue) {
    const auto max = std::numeric_limits<std::int64_t>::max();
    const auto min = std::numeric_limits<std::int64_t>::min();
    store.set("hi", std::to_string(max));
    store.set("lo", std::to_string(min));
    EXPECT_FALSE(store.incr_by("hi", 1).has_value());
    EXPECT_FALSE(store.incr_by("lo", -1).has_value());
    EXPECT_EQ(*store.get("hi"), std::to_string(max));
    EXPECT_EQ(*store.get("lo"), std::to_string(min));
    EXPECT_EQ(store.incr_by("hi", -1), max - 1);
    EXPECT_EQ(store.incr_by("lo", 1), min + 1);
}

TEST(ParseInt64, AcceptsPlainIntegers) {
    EXPECT_EQ(parse_int64("12"), 12);
    EXPECT_EQ(parse_int64("-7"), -7);
    EXPECT_EQ(parse_int64("0"), 0);
    EXPECT_EQ(parse_int64("9223372036854775807"), std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(parse_int64("-9223372036854775808"), std::numeric_limits<std::int64_t>::min());
}

TEST(ParseInt64, RejectsAnythingElse) {
    for (const char *bad : {"+3", " 4", "4 ", "4a", "", "-", "1.5", "0x10", "abc",
                            "9223372036854775808", "-9223372036854775809"}) {
        EXPECT_FALSE(parse_int64(bad).has_value()) << "input: '" << bad << "'";
    }
}

} // namespace
} // namespace minikv
