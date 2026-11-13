#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "minikv/persistence.hpp"
#include "minikv/store.hpp"
#include "minikv/undo.hpp"

namespace minikv {

/// Parses and executes one command line at a time and returns the exact
/// response text. It never prints anything itself, which is what makes every
/// command unit-testable without capturing stdout.
class CommandProcessor {
  public:
    /// `store` is borrowed and must outlive the processor. Pass a snapshot
    /// file to enable autosave/SAVE/EXIT saving, or nullopt for --no-save.
    CommandProcessor(Store &store, std::optional<SnapshotFile> snapshot);

    /// Executes one input line and returns the full response, including the
    /// trailing newline. A blank or whitespace-only line returns "" (no output).
    std::string execute(const std::string &line);

    /// True once EXIT or QUIT has succeeded. The REPL checks this after each
    /// command instead of the processor calling exit() itself.
    bool exit_requested() const;

    /// Saves the snapshot when input ends (EOF). Returns the error response if
    /// the save failed, or nullopt on success or when persistence is disabled.
    std::optional<std::string> save_on_shutdown();

  private:
    /// Result of one command handler: the response text and whether the
    /// command succeeded (only successful commands trigger autosave).
    struct Reply {
        std::string text;
        bool ok;
    };

    using Handler = Reply (CommandProcessor::*)(const std::vector<std::string> &args);

    /// Static description of one command: name, how many arguments it takes
    /// (not counting the command name), and whether success triggers autosave.
    struct CommandSpec {
        std::string_view name;
        std::size_t min_args;
        std::size_t max_args;
        bool autosave;
        Handler handler;
    };

    static const CommandSpec *find_command(std::string_view lowercase_name);

    // One handler per command. `args` is the full token list, command included.
    Reply cmd_set(const std::vector<std::string> &args);
    Reply cmd_get(const std::vector<std::string> &args);
    Reply cmd_del(const std::vector<std::string> &args);
    Reply cmd_exists(const std::vector<std::string> &args);
    Reply cmd_keys(const std::vector<std::string> &args);
    Reply cmd_dbsize(const std::vector<std::string> &args);
    Reply cmd_incr(const std::vector<std::string> &args);
    Reply cmd_decr(const std::vector<std::string> &args);
    Reply cmd_incrby(const std::vector<std::string> &args);
    Reply cmd_expire(const std::vector<std::string> &args);
    Reply cmd_ttl(const std::vector<std::string> &args);
    Reply cmd_persist(const std::vector<std::string> &args);
    Reply cmd_undo(const std::vector<std::string> &args);
    Reply cmd_flushall(const std::vector<std::string> &args);
    Reply cmd_save(const std::vector<std::string> &args);
    Reply cmd_help(const std::vector<std::string> &args);
    Reply cmd_exit(const std::vector<std::string> &args);

    /// Shared body of INCR, DECR and INCRBY.
    Reply increment(const std::string &key, std::int64_t delta);

    /// Converts a "seconds" argument into an absolute expiry time. Returns
    /// nullopt if it is not a positive integer or would overflow.
    std::optional<std::int64_t> expiry_from_seconds(std::string_view seconds) const;

    /// Captures the current state of each distinct key, before a change.
    UndoRecord capture(const std::vector<std::string> &keys);

    /// Pushes `record` only if at least one key now differs from it, so
    /// commands that changed nothing never create an undo entry.
    void commit_if_changed(UndoRecord record);

    /// Saves the snapshot if persistence is enabled. Returns the error
    /// response on failure, nullopt otherwise.
    std::optional<std::string> save_snapshot();

    Store &store_;
    std::optional<SnapshotFile> snapshot_;
    UndoStack undo_;
    bool exit_requested_ = false;
};

} // namespace minikv
