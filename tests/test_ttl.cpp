#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "minikv/clock.hpp"
#include "minikv/commands.hpp"
#include "minikv/store.hpp"

namespace minikv {
namespace {

class TtlTest : public ::testing::Test {
  protected:
    FakeClock clock{1'000'000};
    Store store{clock};
};

TEST_F(TtlTest, KeyExpiresExactlyAtBoundary) {
    store.set("k", "v", clock.now_ms() + 1000);
    clock.advance(999);
    EXPECT_TRUE(store.exists("k")); // 1 ms left
    clock.advance(1);
    EXPECT_FALSE(store.exists("k")); // now == expires_at counts as expired
    EXPECT_FALSE(store.get("k").has_value());
}

TEST_F(TtlTest, TtlRoundsUp) {
    store.set("k", "v", clock.now_ms() + 3000);
    EXPECT_EQ(store.ttl_seconds("k"), 3);
    clock.advance(1500); // 1500 ms left
    EXPECT_EQ(store.ttl_seconds("k"), 2);
    clock.advance(500); // exactly 1000 ms left
    EXPECT_EQ(store.ttl_seconds("k"), 1);
    clock.advance(999); // 1 ms left
    EXPECT_EQ(store.ttl_seconds("k"), 1);
    clock.advance(1);
    EXPECT_EQ(store.ttl_seconds("k"), -2);
}

TEST_F(TtlTest, TtlSpecialValues) {
    EXPECT_EQ(store.ttl_seconds("missing"), -2);
    store.set("forever", "v");
    EXPECT_EQ(store.ttl_seconds("forever"), -1);
}

TEST_F(TtlTest, SetClearsTtl) {
    store.set("k", "v", clock.now_ms() + 5000);
    store.set("k", "v2");
    EXPECT_EQ(store.ttl_seconds("k"), -1);
    clock.advance(10'000);
    EXPECT_TRUE(store.exists("k"));
}

TEST_F(TtlTest, IncrKeepsTtl) {
    store.set("n", "10", clock.now_ms() + 5000);
    EXPECT_EQ(store.incr_by("n", 1), 11);
    EXPECT_EQ(store.ttl_seconds("n"), 5);
    clock.advance(5000);
    EXPECT_FALSE(store.exists("n"));
}

TEST_F(TtlTest, IncrOnExpiredKeyStartsFresh) {
    store.set("n", "10", clock.now_ms() + 1000);
    clock.advance(1000);
    EXPECT_EQ(store.incr_by("n", 1), 1);
    EXPECT_EQ(store.ttl_seconds("n"), -1);
}

TEST_F(TtlTest, Persist) {
    store.set("k", "v", clock.now_ms() + 5000);
    EXPECT_TRUE(store.persist("k"));
    EXPECT_EQ(store.ttl_seconds("k"), -1);
    EXPECT_FALSE(store.persist("k"));       // no TTL left to remove
    EXPECT_FALSE(store.persist("missing")); // missing key
    clock.advance(10'000);
    EXPECT_TRUE(store.exists("k"));
}

TEST_F(TtlTest, ExpireOnMissingKey) {
    EXPECT_FALSE(store.expire_at("missing", clock.now_ms() + 1000));
    EXPECT_FALSE(store.exists("missing"));
}

TEST_F(TtlTest, ExpireOnExistingKey) {
    store.set("k", "v");
    EXPECT_TRUE(store.expire_at("k", clock.now_ms() + 2000));
    EXPECT_EQ(store.ttl_seconds("k"), 2);
}

TEST_F(TtlTest, ExpiredKeysHiddenFromKeysAndSize) {
    store.set("live", "1");
    store.set("dying", "2", clock.now_ms() + 1000);
    EXPECT_EQ(store.size(), 2u);
    clock.advance(1000);
    EXPECT_EQ(store.keys(), (std::vector<std::string>{"live"}));
    EXPECT_EQ(store.size(), 1u);
}

TEST_F(TtlTest, SweepRemovesAllExpired) {
    store.set("a", "1", clock.now_ms() + 1000);
    store.set("b", "2", clock.now_ms() + 2000);
    store.set("c", "3");
    clock.advance(2000);
    EXPECT_EQ(store.sweep_expired(), 2u);
    EXPECT_EQ(store.sweep_expired(), 0u);
}

TEST_F(TtlTest, SnapshotExcludesExpired) {
    store.set("a", "1", clock.now_ms() + 1000);
    store.set("b", "2");
    clock.advance(1000);
    const auto snap = store.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].first, "b");
}

TEST_F(TtlTest, PeekTreatsExpiredAsMissing) {
    store.set("k", "v", clock.now_ms() + 1000);
    clock.advance(1000);
    EXPECT_FALSE(store.peek("k").has_value());
}

// The same rules seen through the command layer.
class TtlCommandTest : public ::testing::Test {
  protected:
    FakeClock clock{1'000'000};
    Store store{clock};
    CommandProcessor proc{store, std::nullopt};
};

TEST_F(TtlCommandTest, ExpiredKeysHiddenFromAllReadCommands) {
    EXPECT_EQ(proc.execute("SET temp v EX 1"), "OK\n");
    EXPECT_EQ(proc.execute("SET keep v"), "OK\n");
    clock.advance(1000);
    EXPECT_EQ(proc.execute("GET temp"), "(nil)\n");
    EXPECT_EQ(proc.execute("EXISTS temp"), "(integer) 0\n");
    EXPECT_EQ(proc.execute("DBSIZE"), "(integer) 1\n");
    EXPECT_EQ(proc.execute("KEYS *"), "1) \"keep\"\n");
    EXPECT_EQ(proc.execute("TTL temp"), "(integer) -2\n");
}

TEST_F(TtlCommandTest, TtlCommandRoundsUp) {
    proc.execute("SET k v EX 10");
    clock.advance(8500);
    EXPECT_EQ(proc.execute("TTL k"), "(integer) 2\n");
}

TEST_F(TtlCommandTest, IncrCommandKeepsTtl) {
    proc.execute("SET n 1 EX 10");
    EXPECT_EQ(proc.execute("INCR n"), "(integer) 2\n");
    EXPECT_EQ(proc.execute("TTL n"), "(integer) 10\n");
}

} // namespace
} // namespace minikv
