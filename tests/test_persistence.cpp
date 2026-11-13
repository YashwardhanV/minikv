#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>

#include "minikv/clock.hpp"
#include "minikv/commands.hpp"
#include "minikv/persistence.hpp"
#include "minikv/store.hpp"

namespace minikv {
namespace {

namespace fs = std::filesystem;

/// Gives every test its own empty temp directory and removes it afterwards.
class PersistenceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::random_device random;
        dir_ = fs::temp_directory_path() /
               ("minikv_test_" + std::string(info->name()) + "_" + std::to_string(random()));
        fs::create_directories(dir_);
        path_ = dir_ / "test.db";
    }

    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(dir_, ignored);
    }

    static std::string read_file(const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static void write_file(const fs::path &path, const std::string &content) {
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    std::string corrupt_warning() const {
        return "warning: snapshot '" + path_.string() + "' is corrupt; moved to '" +
               path_.string() + ".corrupt', starting empty\n";
    }

    fs::path tmp_path() const { return fs::path(path_.string() + ".tmp"); }
    fs::path corrupt_path() const { return fs::path(path_.string() + ".corrupt"); }

    fs::path dir_;
    fs::path path_;
    FakeClock clock{1'000'000};
};

TEST_F(PersistenceTest, RoundTripWithAwkwardKeysAndValues) {
    Store store(clock);
    store.set("plain", "value");
    store.set("has space", "value with  spaces");
    store.set("multi\nline\nkey", "line1\nline2\n");
    store.set("quote\"key", "say \"hi\" \\ backslash");
    store.set("empty", "");
    store.set("", "empty key");
    store.set("fake header", "\n3 4 -1\nMINIKV 1\n");
    store.set("binary", std::string("a\0b\r\n\xff", 6));
    store.set("ttl", "expires later", clock.now_ms() + 60'000);

    const SnapshotFile file(path_);
    file.save(store);

    Store loaded(clock);
    std::ostringstream warnings;
    EXPECT_EQ(file.load(loaded, warnings), LoadResult::Loaded);
    EXPECT_EQ(warnings.str(), "");
    EXPECT_EQ(loaded.snapshot(), store.snapshot());
    EXPECT_EQ(loaded.get_entry("ttl")->expires_at_ms, clock.now_ms() + 60'000);
}

TEST_F(PersistenceTest, FileFormatIsExactAndSorted) {
    Store store(clock);
    store.set("b", "two", 5000);
    store.set("a", "one");
    store.set("c", "");
    SnapshotFile(path_).save(store);

    // 5000 < now, so "b" is already expired and must not be written.
    EXPECT_EQ(read_file(path_), "MINIKV 1\n1 3 -1\naone\n1 0 -1\nc\n");

    store.set("b", "two", 2'000'000);
    SnapshotFile(path_).save(store);
    EXPECT_EQ(read_file(path_), "MINIKV 1\n1 3 -1\naone\n1 3 2000000\nbtwo\n1 0 -1\nc\n");
}

TEST_F(PersistenceTest, ExpiredEntriesAreNotSaved) {
    Store store(clock);
    store.set("live", "1");
    store.set("dying", "2", clock.now_ms() + 1000);
    clock.advance(1000);
    SnapshotFile(path_).save(store);

    Store loaded(clock);
    std::ostringstream warnings;
    SnapshotFile(path_).load(loaded, warnings);
    EXPECT_EQ(loaded.keys(), (std::vector<std::string>{"live"}));
    EXPECT_EQ(read_file(path_).find("dying"), std::string::npos);
}

TEST_F(PersistenceTest, EntriesExpiredWhileOnDiskAreSkippedOnLoad) {
    Store store(clock);
    store.set("short", "1", clock.now_ms() + 1000);
    store.set("long", "2", clock.now_ms() + 10'000);
    SnapshotFile(path_).save(store);

    clock.advance(5000); // "short" expired while the program was not running
    Store loaded(clock);
    std::ostringstream warnings;
    EXPECT_EQ(SnapshotFile(path_).load(loaded, warnings), LoadResult::Loaded);
    EXPECT_EQ(loaded.keys(), (std::vector<std::string>{"long"}));
}

TEST_F(PersistenceTest, MissingFileMeansEmptyStore) {
    Store store(clock);
    store.set("stale", "x");
    std::ostringstream warnings;
    EXPECT_EQ(SnapshotFile(path_).load(store, warnings), LoadResult::Missing);
    EXPECT_EQ(store.size(), 0u);
    EXPECT_EQ(warnings.str(), "");
    EXPECT_FALSE(fs::exists(path_)); // loading never creates a file
}

TEST_F(PersistenceTest, LoadReplacesExistingContents) {
    Store source(clock);
    source.set("from_disk", "1");
    SnapshotFile(path_).save(source);

    Store store(clock);
    store.set("in_memory", "2");
    std::ostringstream warnings;
    SnapshotFile(path_).load(store, warnings);
    EXPECT_EQ(store.keys(), (std::vector<std::string>{"from_disk"}));
}

TEST_F(PersistenceTest, NoTmpFileLeftAfterSuccessfulSave) {
    Store store(clock);
    store.set("k", "v");
    SnapshotFile(path_).save(store);
    EXPECT_TRUE(fs::exists(path_));
    EXPECT_FALSE(fs::exists(tmp_path()));
}

TEST_F(PersistenceTest, SaveReplacesPreviousSnapshot) {
    Store store(clock);
    store.set("old", "1");
    const SnapshotFile file(path_);
    file.save(store);
    store.clear();
    store.set("new", "2");
    file.save(store);

    Store loaded(clock);
    std::ostringstream warnings;
    file.load(loaded, warnings);
    EXPECT_EQ(loaded.keys(), (std::vector<std::string>{"new"}));
}

TEST_F(PersistenceTest, SaveFailureThrowsAndLeavesNothingBehind) {
    Store store(clock);
    store.set("k", "v");
    const fs::path bad = dir_ / "no_such_dir" / "test.db";
    EXPECT_THROW(SnapshotFile(bad).save(store), PersistenceError);
    EXPECT_FALSE(fs::exists(bad));
}

TEST_F(PersistenceTest, SaveFailureKeepsOldSnapshotIntact) {
    Store store(clock);
    store.set("k", "v");
    SnapshotFile(path_).save(store);
    const std::string before = read_file(path_);

    // Make "<path>.tmp" a directory so the temp file cannot be opened.
    fs::create_directory(tmp_path());
    store.set("k", "changed");
    EXPECT_THROW(SnapshotFile(path_).save(store), PersistenceError);
    EXPECT_EQ(read_file(path_), before);
}

// Every kind of corruption must be detected and handled the same way.
class CorruptSnapshotTest : public PersistenceTest,
                            public ::testing::WithParamInterface<const char *> {};

TEST_P(CorruptSnapshotTest, MovedAsideAndStoreStartsEmpty) {
    const std::string content = GetParam();
    write_file(path_, content);

    Store store(clock);
    store.set("stale", "x");
    std::ostringstream warnings;
    EXPECT_EQ(SnapshotFile(path_).load(store, warnings), LoadResult::Corrupt);

    EXPECT_EQ(store.size(), 0u);
    EXPECT_EQ(warnings.str(), corrupt_warning());
    EXPECT_FALSE(fs::exists(path_));
    ASSERT_TRUE(fs::exists(corrupt_path()));
    EXPECT_EQ(read_file(corrupt_path()), content); // original bytes preserved
}

INSTANTIATE_TEST_SUITE_P(
    Cases, CorruptSnapshotTest,
    ::testing::Values("",                                     // empty file
                      "MINIKV 2\n",                           // wrong version
                      "REDIS 1\n1 1 -1\nab\n",                // wrong magic
                      "MINIKV 1",                             // header without newline
                      "MINIKV 1\nx 1 -1\nab\n",               // non-numeric key length
                      "MINIKV 1\n1 y -1\nab\n",               // non-numeric value length
                      "MINIKV 1\n1 1 soon\nab\n",             // non-numeric expiry
                      "MINIKV 1\n-1 1 -1\nab\n",              // negative length
                      "MINIKV 1\n1 1 -5\nab\n",               // invalid negative expiry
                      "MINIKV 1\n1 1\nab\n",                  // missing field
                      "MINIKV 1\n1 1 -1 7\nab\n",             // extra field
                      "MINIKV 1\n1 1 -1\na",                  // ends mid-record
                      "MINIKV 1\n1 1 -1\nab",                 // missing record newline
                      "MINIKV 1\n1 1 -1\nabX",                // wrong byte after record
                      "MINIKV 1\n5 5 -1\nab\n",               // lengths past end of file
                      "MINIKV 1\n1 1 -1\nab\ngarbage",        // trailing junk
                      "MINIKV 1\n99999999999999999999 1 -1\n" // length overflows
                      ));

TEST_F(PersistenceTest, CorruptFileIsNeverOverwrittenBySave) {
    write_file(path_, "not a snapshot");
    Store store(clock);
    std::ostringstream warnings;
    const SnapshotFile file(path_);
    file.load(store, warnings);
    store.set("k", "v");
    file.save(store);
    EXPECT_EQ(read_file(corrupt_path()), "not a snapshot");
}

TEST_F(PersistenceTest, HeaderOnlyFileIsValidAndEmpty) {
    write_file(path_, "MINIKV 1\n");
    Store store(clock);
    std::ostringstream warnings;
    EXPECT_EQ(SnapshotFile(path_).load(store, warnings), LoadResult::Loaded);
    EXPECT_EQ(store.size(), 0u);
}

// ---- Persistence through the command layer ---------------------------------

TEST_F(PersistenceTest, AutosaveAfterMutatingCommandsAndUndo) {
    Store store(clock);
    CommandProcessor proc(store, SnapshotFile(path_));
    EXPECT_EQ(proc.execute("SET a 1"), "OK\n");
    EXPECT_EQ(read_file(path_), "MINIKV 1\n1 1 -1\na1\n");

    EXPECT_EQ(proc.execute("INCR a"), "(integer) 2\n");
    EXPECT_EQ(read_file(path_), "MINIKV 1\n1 1 -1\na2\n");

    EXPECT_EQ(proc.execute("UNDO"), "OK\n"); // disk must reflect the reverted state
    EXPECT_EQ(read_file(path_), "MINIKV 1\n1 1 -1\na1\n");

    EXPECT_EQ(proc.execute("FLUSHALL"), "OK\n");
    EXPECT_EQ(read_file(path_), "MINIKV 1\n");
}

TEST_F(PersistenceTest, ReadOnlyCommandsDoNotWrite) {
    Store store(clock);
    CommandProcessor proc(store, SnapshotFile(path_));
    proc.execute("GET a");
    proc.execute("KEYS *");
    proc.execute("DBSIZE");
    proc.execute("INCR"); // wrong argument count
    proc.execute("UNDO"); // nothing to undo
    EXPECT_FALSE(fs::exists(path_));
}

TEST_F(PersistenceTest, SaveCommandWritesNow) {
    Store store(clock);
    store.set("k", "v"); // bypasses autosave
    CommandProcessor proc(store, SnapshotFile(path_));
    EXPECT_EQ(proc.execute("SAVE"), "OK\n");
    EXPECT_EQ(read_file(path_), "MINIKV 1\n1 1 -1\nkv\n");
}

TEST_F(PersistenceTest, ExitSavesAndShutdownSaves) {
    {
        Store store(clock);
        store.set("k", "v");
        CommandProcessor proc(store, SnapshotFile(path_));
        EXPECT_EQ(proc.execute("EXIT"), "Bye\n");
        EXPECT_TRUE(proc.exit_requested());
        EXPECT_EQ(read_file(path_), "MINIKV 1\n1 1 -1\nkv\n");
    }
    {
        Store store(clock);
        store.set("x", "y");
        CommandProcessor proc(store, SnapshotFile(path_));
        EXPECT_FALSE(proc.save_on_shutdown().has_value());
        EXPECT_EQ(read_file(path_), "MINIKV 1\n1 1 -1\nxy\n");
    }
}

TEST_F(PersistenceTest, DiskWriteFailureKeepsDataInMemory) {
    const fs::path bad = dir_ / "no_such_dir" / "test.db";
    const std::string expected_error =
        "(error) ERR failed to save: cannot open '" + bad.string() + ".tmp' for writing\n";
    Store store(clock);
    CommandProcessor proc(store, SnapshotFile(bad));

    EXPECT_EQ(proc.execute("SET k v"), expected_error);
    EXPECT_EQ(proc.execute("GET k"), "\"v\"\n"); // data stays in memory
    EXPECT_EQ(proc.execute("SAVE"), expected_error);

    // EXIT refuses to quit when the final save fails, so nothing is lost.
    EXPECT_EQ(proc.execute("EXIT"), expected_error);
    EXPECT_FALSE(proc.exit_requested());
    EXPECT_EQ(proc.save_on_shutdown(), expected_error);
}

} // namespace
} // namespace minikv
