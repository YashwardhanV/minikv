#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "minikv/clock.hpp"
#include "minikv/commands.hpp"
#include "minikv/store.hpp"

namespace minikv {
namespace {

const std::string kNotInteger = "(error) ERR value is not an integer or out of range\n";
const std::string kInvalidExpire = "(error) ERR invalid expire time\n";
const std::string kSyntaxError = "(error) ERR syntax error\n";

class CommandsTest : public ::testing::Test {
  protected:
    FakeClock clock{1'000'000};
    Store store{clock};
    CommandProcessor proc{store, std::nullopt};
    std::string run(const std::string &line) { return proc.execute(line); }
};

// ---- Parsing / dispatch ---------------------------------------------------

TEST_F(CommandsTest, BlankLinesProduceNoOutput) {
    EXPECT_EQ(run(""), "");
    EXPECT_EQ(run("   "), "");
    EXPECT_EQ(run("\t \t"), "");
}

TEST_F(CommandsTest, CommandNamesAreCaseInsensitive) {
    EXPECT_EQ(run("set k v"), "OK\n");
    EXPECT_EQ(run("Get k"), "\"v\"\n");
    EXPECT_EQ(run("gEt k"), "\"v\"\n");
    EXPECT_EQ(run("dbsize"), "(integer) 1\n");
}

TEST_F(CommandsTest, KeysAndValuesAreCaseSensitive) {
    run("SET Name Alex");
    EXPECT_EQ(run("GET name"), "(nil)\n");
    EXPECT_EQ(run("GET Name"), "\"Alex\"\n");
}

TEST_F(CommandsTest, UnknownCommandEchoesNameAsTyped) {
    EXPECT_EQ(run("FOO"), "(error) ERR unknown command 'FOO'\n");
    EXPECT_EQ(run("fOo bar baz"), "(error) ERR unknown command 'fOo'\n");
}

TEST_F(CommandsTest, TokenizerErrorIsReported) {
    EXPECT_EQ(run("SET k \"unterminated"), "(error) ERR unterminated quote\n");
}

TEST_F(CommandsTest, WrongNumberOfArguments) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"SET", "set"},         {"SET k", "set"},
        {"Set k", "set"},       {"GET", "get"},
        {"GET a b", "get"},     {"DEL", "del"},
        {"EXISTS", "exists"},   {"EXISTS a b", "exists"},
        {"KEYS a b", "keys"},   {"DBSIZE x", "dbsize"},
        {"INCR", "incr"},       {"INCR a b", "incr"},
        {"DECR", "decr"},       {"DECR a b", "decr"},
        {"INCRBY a", "incrby"}, {"INCRBY a 1 2", "incrby"},
        {"EXPIRE a", "expire"}, {"EXPIRE a 1 2", "expire"},
        {"TTL", "ttl"},         {"TTL a b", "ttl"},
        {"PERSIST", "persist"}, {"PERSIST a b", "persist"},
        {"UNDO x", "undo"},     {"FLUSHALL x", "flushall"},
        {"SAVE x", "save"},     {"HELP x", "help"},
        {"EXIT x", "exit"},     {"QUIT x", "quit"},
    };
    for (const auto &[line, name] : cases) {
        EXPECT_EQ(run(line), "(error) ERR wrong number of arguments for '" + name + "'\n")
            << "line: " << line;
    }
}

// ---- SET / GET ------------------------------------------------------------

TEST_F(CommandsTest, SetAndGet) {
    EXPECT_EQ(run("SET name Alex"), "OK\n");
    EXPECT_EQ(run("GET name"), "\"Alex\"\n");
    EXPECT_EQ(run("GET missing"), "(nil)\n");
}

TEST_F(CommandsTest, SetOverwrites) {
    run("SET k one");
    EXPECT_EQ(run("SET k two"), "OK\n");
    EXPECT_EQ(run("GET k"), "\"two\"\n");
}

TEST_F(CommandsTest, QuotedValuesAndEmptyValue) {
    EXPECT_EQ(run("SET greeting \"hello world\""), "OK\n");
    EXPECT_EQ(run("GET greeting"), "\"hello world\"\n");
    EXPECT_EQ(run("SET empty \"\""), "OK\n");
    EXPECT_EQ(run("GET empty"), "\"\"\n");
}

TEST_F(CommandsTest, GetEscapesQuotesAndBackslashes) {
    EXPECT_EQ(run(R"(SET q "say \"hi\" \\ bye")"), "OK\n");
    EXPECT_EQ(run("GET q"), R"("say \"hi\" \\ bye")"
                            "\n");
}

TEST_F(CommandsTest, SetWithEx) {
    EXPECT_EQ(run("SET k v EX 10"), "OK\n");
    EXPECT_EQ(run("TTL k"), "(integer) 10\n");
    EXPECT_EQ(run("SET k2 v ex 5"), "OK\n"); // EX is case-insensitive
    EXPECT_EQ(run("TTL k2"), "(integer) 5\n");
}

TEST_F(CommandsTest, SetRemovesExistingTtl) {
    run("SET k v EX 10");
    run("SET k v2");
    EXPECT_EQ(run("TTL k"), "(integer) -1\n");
}

TEST_F(CommandsTest, SetBadOptionIsSyntaxError) {
    EXPECT_EQ(run("SET k v EX"), kSyntaxError);
    EXPECT_EQ(run("SET k v PX 10"), kSyntaxError);
    EXPECT_EQ(run("SET k v NX"), kSyntaxError);
    EXPECT_EQ(run("SET k v EX 10 extra"), kSyntaxError);
    EXPECT_EQ(run("GET k"), "(nil)\n"); // nothing was stored
}

TEST_F(CommandsTest, SetInvalidExpireTime) {
    for (const char *seconds :
         {"0", "-5", "abc", "+3", "1.5", "99999999999999999999", "9223372036854775807"}) {
        EXPECT_EQ(run(std::string("SET k v EX ") + seconds), kInvalidExpire) << seconds;
    }
    EXPECT_EQ(run("GET k"), "(nil)\n");
}

// ---- DEL / EXISTS / KEYS / DBSIZE -----------------------------------------

TEST_F(CommandsTest, DelCountsOnlyKeysActuallyDeleted) {
    run("SET a 1");
    run("SET b 2");
    EXPECT_EQ(run("DEL a b nothere"), "(integer) 2\n");
    EXPECT_EQ(run("DEL a"), "(integer) 0\n");
    EXPECT_EQ(run("DBSIZE"), "(integer) 0\n");
}

TEST_F(CommandsTest, Exists) {
    run("SET a 1");
    EXPECT_EQ(run("EXISTS a"), "(integer) 1\n");
    EXPECT_EQ(run("EXISTS b"), "(integer) 0\n");
}

TEST_F(CommandsTest, KeysSortedAndFiltered) {
    run("SET visits 1");
    run("SET name Alex");
    run("SET greeting hi");
    EXPECT_EQ(run("KEYS"), "1) \"greeting\"\n2) \"name\"\n3) \"visits\"\n");
    EXPECT_EQ(run("KEYS *"), "1) \"greeting\"\n2) \"name\"\n3) \"visits\"\n");
    EXPECT_EQ(run("KEYS *e*"), "1) \"greeting\"\n2) \"name\"\n");
    EXPECT_EQ(run("KEYS n?me"), "1) \"name\"\n");
    EXPECT_EQ(run("KEYS zzz*"), "(empty list)\n");
}

TEST_F(CommandsTest, KeysOnEmptyStore) { EXPECT_EQ(run("KEYS *"), "(empty list)\n"); }

TEST_F(CommandsTest, KeysEscapesOutput) {
    run(R"(SET "a \"b\"" 1)");
    EXPECT_EQ(run("KEYS *"), R"(1) "a \"b\"")"
                             "\n");
}

TEST_F(CommandsTest, DbSize) {
    EXPECT_EQ(run("DBSIZE"), "(integer) 0\n");
    run("SET a 1");
    run("SET b 2");
    EXPECT_EQ(run("DBSIZE"), "(integer) 2\n");
}

// ---- INCR family ----------------------------------------------------------

TEST_F(CommandsTest, IncrAndDecr) {
    EXPECT_EQ(run("INCR visits"), "(integer) 1\n");
    EXPECT_EQ(run("INCR visits"), "(integer) 2\n");
    EXPECT_EQ(run("GET visits"), "\"2\"\n");
    EXPECT_EQ(run("DECR visits"), "(integer) 1\n");
    EXPECT_EQ(run("DECR fresh"), "(integer) -1\n");
}

TEST_F(CommandsTest, IncrBy) {
    EXPECT_EQ(run("INCRBY n 10"), "(integer) 10\n");
    EXPECT_EQ(run("INCRBY n -25"), "(integer) -15\n");
    EXPECT_EQ(run("INCRBY n 0"), "(integer) -15\n");
}

TEST_F(CommandsTest, IncrOnNonInteger) {
    run("SET name Alex");
    EXPECT_EQ(run("INCR name"), kNotInteger);
    EXPECT_EQ(run("DECR name"), kNotInteger);
    EXPECT_EQ(run("INCRBY name 1"), kNotInteger);
    EXPECT_EQ(run("GET name"), "\"Alex\"\n");
}

TEST_F(CommandsTest, StrictIntegerParsingOfStoredValue) {
    for (const char *bad : {"+3", " 4", "4a", "", "1.0", "9223372036854775808"}) {
        store.set("n", bad);
        EXPECT_EQ(run("INCR n"), kNotInteger) << "stored value: '" << bad << "'";
    }
    store.set("n", "-7");
    EXPECT_EQ(run("INCR n"), "(integer) -6\n");
}

TEST_F(CommandsTest, StrictIntegerParsingOfIncrByArgument) {
    for (const char *bad : {"+3", "\" 4\"", "4a", "\"\"", "x", "99999999999999999999"}) {
        EXPECT_EQ(run(std::string("INCRBY n ") + bad), kNotInteger) << "argument: " << bad;
    }
    EXPECT_EQ(run("EXISTS n"), "(integer) 0\n");
}

TEST_F(CommandsTest, IncrOverflowIsAnError) {
    const auto max = std::numeric_limits<std::int64_t>::max();
    const auto min = std::numeric_limits<std::int64_t>::min();
    run("SET n " + std::to_string(max));
    EXPECT_EQ(run("INCR n"), kNotInteger);
    EXPECT_EQ(run("INCRBY n 1"), kNotInteger);
    EXPECT_EQ(run("GET n"), "\"" + std::to_string(max) + "\"\n");

    run("SET m " + std::to_string(min));
    EXPECT_EQ(run("DECR m"), kNotInteger);
    EXPECT_EQ(run("INCRBY m -1"), kNotInteger);
    EXPECT_EQ(run("GET m"), "\"" + std::to_string(min) + "\"\n");
}

// ---- EXPIRE / TTL / PERSIST -----------------------------------------------

TEST_F(CommandsTest, Expire) {
    run("SET k v");
    EXPECT_EQ(run("EXPIRE k 30"), "(integer) 1\n");
    EXPECT_EQ(run("TTL k"), "(integer) 30\n");
    EXPECT_EQ(run("EXPIRE missing 30"), "(integer) 0\n");
}

TEST_F(CommandsTest, ExpireInvalidSeconds) {
    run("SET k v");
    for (const char *seconds : {"0", "-1", "abc", "+5", "9223372036854775807"}) {
        EXPECT_EQ(run(std::string("EXPIRE k ") + seconds), kInvalidExpire) << seconds;
    }
    EXPECT_EQ(run("EXPIRE missing 0"), kInvalidExpire);
    EXPECT_EQ(run("TTL k"), "(integer) -1\n");
}

TEST_F(CommandsTest, TtlValues) {
    EXPECT_EQ(run("TTL missing"), "(integer) -2\n");
    run("SET k v");
    EXPECT_EQ(run("TTL k"), "(integer) -1\n");
    run("EXPIRE k 2");
    clock.advance(500); // 1500 ms left rounds up to 2
    EXPECT_EQ(run("TTL k"), "(integer) 2\n");
    clock.advance(1500);
    EXPECT_EQ(run("TTL k"), "(integer) -2\n");
}

TEST_F(CommandsTest, Persist) {
    run("SET k v EX 10");
    EXPECT_EQ(run("PERSIST k"), "(integer) 1\n");
    EXPECT_EQ(run("TTL k"), "(integer) -1\n");
    EXPECT_EQ(run("PERSIST k"), "(integer) 0\n");
    EXPECT_EQ(run("PERSIST missing"), "(integer) 0\n");
}

// ---- UNDO / FLUSHALL / SAVE / HELP / EXIT ---------------------------------

TEST_F(CommandsTest, UndoEmptyIsAnError) {
    EXPECT_EQ(run("UNDO"), "(error) ERR nothing to undo\n");
}

TEST_F(CommandsTest, UndoSucceeds) {
    run("SET k v");
    EXPECT_EQ(run("UNDO"), "OK\n");
}

TEST_F(CommandsTest, FlushAll) {
    run("SET a 1");
    run("SET b 2");
    EXPECT_EQ(run("FLUSHALL"), "OK\n");
    EXPECT_EQ(run("DBSIZE"), "(integer) 0\n");
}

TEST_F(CommandsTest, SaveWithPersistenceDisabled) {
    EXPECT_EQ(run("SAVE"), "(error) ERR persistence disabled\n");
}

TEST_F(CommandsTest, HelpListsEveryCommandWithinTwentyFiveLines) {
    const std::string help = run("HELP");
    ASSERT_FALSE(help.empty());
    EXPECT_EQ(help.back(), '\n');
    const auto lines = std::count(help.begin(), help.end(), '\n');
    EXPECT_LE(lines, 25);
    for (const char *command :
         {"SET", "GET", "DEL", "EXISTS", "KEYS", "DBSIZE", "INCR", "DECR", "INCRBY", "EXPIRE",
          "TTL", "PERSIST", "UNDO", "FLUSHALL", "SAVE", "HELP", "EXIT", "QUIT"}) {
        EXPECT_NE(help.find(command), std::string::npos) << command;
    }
    EXPECT_EQ(run("help"), help);
}

TEST_F(CommandsTest, ExitAndQuit) {
    EXPECT_FALSE(proc.exit_requested());
    EXPECT_EQ(run("EXIT"), "Bye\n");
    EXPECT_TRUE(proc.exit_requested());

    CommandProcessor other(store, std::nullopt);
    EXPECT_EQ(other.execute("quit"), "Bye\n");
    EXPECT_TRUE(other.exit_requested());
}

TEST_F(CommandsTest, ShutdownSaveIsNoOpWithoutPersistence) {
    EXPECT_FALSE(proc.save_on_shutdown().has_value());
}

// ---- The exact session from the spec ---------------------------------------

TEST_F(CommandsTest, SpecSessionWalkthrough) {
    const std::vector<std::pair<std::string, std::string>> session = {
        {"SET name Alex", "OK\n"},
        {"GET name", "\"Alex\"\n"},
        {"SET greeting \"hello world\"", "OK\n"},
        {"GET greeting", "\"hello world\"\n"},
        {"INCR visits", "(integer) 1\n"},
        {"INCR visits", "(integer) 2\n"},
        {"GET visits", "\"2\"\n"},
        {"INCR name", kNotInteger},
        {"KEYS *", "1) \"greeting\"\n2) \"name\"\n3) \"visits\"\n"},
        {"DEL name nothere", "(integer) 1\n"},
        {"UNDO", "OK\n"},
        {"GET name", "\"Alex\"\n"},
        {"UNDO", "OK\n"},
        {"FOO", "(error) ERR unknown command 'FOO'\n"},
        {"EXISTS name", "(integer) 1\n"},
        {"DBSIZE", "(integer) 3\n"},
        {"FLUSHALL", "OK\n"},
        {"DBSIZE", "(integer) 0\n"},
        {"UNDO", "OK\n"},
        {"DBSIZE", "(integer) 3\n"},
    };
    for (const auto &[line, expected] : session) {
        EXPECT_EQ(run(line), expected) << "line: " << line;
    }
    EXPECT_EQ(run("GET visits"), "\"1\"\n"); // second UNDO reverted INCR 1 -> 2
}

} // namespace
} // namespace minikv
