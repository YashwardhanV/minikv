#include <gtest/gtest.h>

#include <string>

#include "minikv/clock.hpp"
#include "minikv/commands.hpp"
#include "minikv/store.hpp"
#include "minikv/undo.hpp"

namespace minikv {
namespace {

UndoRecord record_for(const std::string &key) {
    UndoRecord record;
    record.previous.emplace_back(key, std::nullopt);
    return record;
}

TEST(UndoStack, StartsEmpty) {
    UndoStack stack;
    EXPECT_TRUE(stack.empty());
    EXPECT_EQ(stack.size(), 0u);
    EXPECT_EQ(stack.capacity(), 100u);
    EXPECT_FALSE(stack.pop().has_value());
}

TEST(UndoStack, PopIsLastInFirstOut) {
    UndoStack stack;
    stack.push(record_for("first"));
    stack.push(record_for("second"));
    EXPECT_EQ(stack.pop()->previous[0].first, "second");
    EXPECT_EQ(stack.pop()->previous[0].first, "first");
    EXPECT_FALSE(stack.pop().has_value());
}

TEST(UndoStack, CapDropsOldest) {
    UndoStack stack(3);
    for (const char *key : {"a", "b", "c", "d"}) {
        stack.push(record_for(key));
    }
    EXPECT_EQ(stack.size(), 3u);
    EXPECT_EQ(stack.pop()->previous[0].first, "d");
    EXPECT_EQ(stack.pop()->previous[0].first, "c");
    EXPECT_EQ(stack.pop()->previous[0].first, "b"); // "a" was dropped
    EXPECT_TRUE(stack.empty());
}

class UndoTest : public ::testing::Test {
  protected:
    FakeClock clock{1'000'000};
    Store store{clock};
    CommandProcessor proc{store, std::nullopt};
    std::string run(const std::string &line) { return proc.execute(line); }
};

TEST_F(UndoTest, EmptyHistoryIsAnError) { EXPECT_EQ(run("UNDO"), "(error) ERR nothing to undo\n"); }

TEST_F(UndoTest, UndoSetOfNewKeyRemovesIt) {
    run("SET k v");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("GET k"), "(nil)\n");
    EXPECT_EQ(run("DBSIZE"), "(integer) 0\n");
}

TEST_F(UndoTest, UndoSetOverwriteRestoresOldValueAndTtl) {
    run("SET k old EX 100");
    run("SET k new");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("GET k"), "\"old\"\n");
    EXPECT_EQ(run("TTL k"), "(integer) 100\n");
}

TEST_F(UndoTest, UndoDelOfMultipleKeys) {
    run("SET a 1");
    run("SET b 2 EX 50");
    EXPECT_EQ(run("DEL a b missing"), "(integer) 2\n");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("GET a"), "\"1\"\n");
    EXPECT_EQ(run("GET b"), "\"2\"\n");
    EXPECT_EQ(run("TTL b"), "(integer) 50\n");
    EXPECT_EQ(run("EXISTS missing"), "(integer) 0\n");
}

TEST_F(UndoTest, UndoDelWithRepeatedKey) {
    run("SET a 1");
    EXPECT_EQ(run("DEL a a"), "(integer) 1\n");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("GET a"), "\"1\"\n");
}

TEST_F(UndoTest, UndoIncr) {
    run("SET n 5");
    run("INCR n");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("GET n"), "\"5\"\n");
}

TEST_F(UndoTest, UndoIncrOfMissingKeyRemovesIt) {
    run("INCR n");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("EXISTS n"), "(integer) 0\n");
}

TEST_F(UndoTest, UndoDecrAndIncrBy) {
    run("SET n 5");
    run("DECR n");
    run("INCRBY n 100");
    run("UNDO");
    EXPECT_EQ(run("GET n"), "\"4\"\n");
    run("UNDO");
    EXPECT_EQ(run("GET n"), "\"5\"\n");
}

TEST_F(UndoTest, UndoExpire) {
    run("SET k v");
    run("EXPIRE k 30");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("TTL k"), "(integer) -1\n");
}

TEST_F(UndoTest, UndoPersist) {
    run("SET k v EX 30");
    run("PERSIST k");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("TTL k"), "(integer) 30\n");
}

TEST_F(UndoTest, UndoFlushAll) {
    run("SET a 1");
    run("SET b 2 EX 60");
    run("SET c 3");
    EXPECT_EQ(run("FLUSHALL"), "OK\n");
    EXPECT_EQ(run("DBSIZE"), "(integer) 0\n");
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("DBSIZE"), "(integer) 3\n");
    EXPECT_EQ(run("GET b"), "\"2\"\n");
    EXPECT_EQ(run("TTL b"), "(integer) 60\n");
}

TEST_F(UndoTest, NoOpCommandsDoNotPushRecords) {
    run("SET a 1");           // the only command below that changes anything
    store.set("word", "abc"); // set directly, so it creates no undo record

    // Mutating commands that change nothing.
    EXPECT_EQ(run("DEL missing"), "(integer) 0\n");
    EXPECT_EQ(run("EXPIRE missing 10"), "(integer) 0\n");
    EXPECT_EQ(run("PERSIST a"), "(integer) 0\n"); // a has no TTL
    EXPECT_EQ(run("PERSIST missing"), "(integer) 0\n");
    EXPECT_EQ(run("SET a 1"), "OK\n"); // same value, same (no) TTL

    // Commands that fail.
    EXPECT_EQ(run("INCR word"), "(error) ERR value is not an integer or out of range\n");
    EXPECT_EQ(run("INCRBY a x"), "(error) ERR value is not an integer or out of range\n");
    EXPECT_EQ(run("EXPIRE a 0"), "(error) ERR invalid expire time\n");
    EXPECT_EQ(run("SET a 2 PX 5"), "(error) ERR syntax error\n");

    // Read-only commands.
    run("GET a");
    run("KEYS *");
    run("TTL a");
    run("DBSIZE");
    run("EXISTS a");

    EXPECT_EQ(run("UNDO"), "OK\n"); // undoes the very first SET a 1
    EXPECT_EQ(run("EXISTS a"), "(integer) 0\n");
    EXPECT_EQ(run("UNDO"), "(error) ERR nothing to undo\n");
}

TEST_F(UndoTest, FlushAllOnEmptyStoreDoesNotPush) {
    EXPECT_EQ(run("FLUSHALL"), "OK\n");
    EXPECT_EQ(run("UNDO"), "(error) ERR nothing to undo\n");
}

TEST_F(UndoTest, UndoIsNotUndoable) {
    run("SET a 1");
    run("SET a 2");
    run("UNDO");
    EXPECT_EQ(run("GET a"), "\"1\"\n");
    run("UNDO");
    EXPECT_EQ(run("GET a"), "(nil)\n");
    EXPECT_EQ(run("UNDO"), "(error) ERR nothing to undo\n");
}

TEST_F(UndoTest, HundredEntryCapDropsOldest) {
    for (int i = 0; i < 105; ++i) {
        run("SET k" + std::to_string(i) + " v");
    }
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(run("UNDO"), "OK\n") << "undo #" << i;
    }
    EXPECT_EQ(run("UNDO"), "(error) ERR nothing to undo\n");
    // The 5 oldest SETs fell off the history, so their keys survive.
    EXPECT_EQ(run("DBSIZE"), "(integer) 5\n");
    EXPECT_EQ(run("KEYS *"), "1) \"k0\"\n2) \"k1\"\n3) \"k2\"\n4) \"k3\"\n5) \"k4\"\n");
}

TEST_F(UndoTest, RestoredEntryWithPastExpiryIsSweptAway) {
    run("SET k v EX 1");
    run("DEL k");
    clock.advance(2000);
    EXPECT_EQ(run("UNDO"), "OK\n");
    EXPECT_EQ(run("GET k"), "(nil)\n");
}

} // namespace
} // namespace minikv
