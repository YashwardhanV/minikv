#include "minikv/commands.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

#include "minikv/tokenizer.hpp"

namespace minikv {

namespace {

constexpr std::size_t kUnlimited = std::numeric_limits<std::size_t>::max();

std::string to_lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// ---- Response formatting (redis-cli style). Every response ends in '\n'. ----

std::string format_ok() { return "OK\n"; }

std::string format_nil() { return "(nil)\n"; }

std::string format_integer(std::int64_t value) {
    return "(integer) " + std::to_string(value) + "\n";
}

/// Wraps a value in double quotes, escaping `"` and `\` so output is unambiguous.
std::string quote(std::string_view value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
        }
        result += c;
    }
    result += '"';
    return result;
}

std::string format_string(std::string_view value) { return quote(value) + "\n"; }

std::string format_list(const std::vector<std::string> &items) {
    if (items.empty()) {
        return "(empty list)\n";
    }
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) {
        result += std::to_string(i + 1) + ") " + quote(items[i]) + "\n";
    }
    return result;
}

std::string format_error(std::string_view message) {
    return "(error) ERR " + std::string(message) + "\n";
}

// Exact error messages from the spec.
constexpr std::string_view kNotInteger = "value is not an integer or out of range";
constexpr std::string_view kInvalidExpire = "invalid expire time";
constexpr std::string_view kSyntaxError = "syntax error";

constexpr std::string_view kHelpText =
    "MiniKV commands (command names are case-insensitive):\n"
    "  SET key value [EX seconds]  Set a value, optionally with a TTL\n"
    "  GET key                     Get a value\n"
    "  DEL key [key ...]           Delete keys; returns how many were removed\n"
    "  EXISTS key                  1 if the key exists, else 0\n"
    "  KEYS [pattern]              Sorted keys matching a glob (* and ?)\n"
    "  DBSIZE                      Number of keys\n"
    "  INCR key / DECR key         Add or subtract 1 (missing key counts as 0)\n"
    "  INCRBY key n                Add n (n may be negative)\n"
    "  EXPIRE key seconds          Set a time to live in seconds\n"
    "  TTL key                     Seconds left (-1 = no TTL, -2 = missing)\n"
    "  PERSIST key                 Remove a key's TTL\n"
    "  UNDO                        Revert the last change\n"
    "  FLUSHALL                    Delete every key (undoable)\n"
    "  SAVE                        Write the snapshot to disk now\n"
    "  HELP                        Show this help\n"
    "  EXIT / QUIT                 Save and quit\n";

} // namespace

CommandProcessor::CommandProcessor(Store &store, std::optional<SnapshotFile> snapshot)
    : store_(store), snapshot_(std::move(snapshot)) {}

const CommandProcessor::CommandSpec *CommandProcessor::find_command(std::string_view name) {
    // min/max count arguments after the command name.
    static const CommandSpec kCommands[] = {
        {"set", 2, kUnlimited, true, &CommandProcessor::cmd_set},
        {"get", 1, 1, false, &CommandProcessor::cmd_get},
        {"del", 1, kUnlimited, true, &CommandProcessor::cmd_del},
        {"exists", 1, 1, false, &CommandProcessor::cmd_exists},
        {"keys", 0, 1, false, &CommandProcessor::cmd_keys},
        {"dbsize", 0, 0, false, &CommandProcessor::cmd_dbsize},
        {"incr", 1, 1, true, &CommandProcessor::cmd_incr},
        {"decr", 1, 1, true, &CommandProcessor::cmd_decr},
        {"incrby", 2, 2, true, &CommandProcessor::cmd_incrby},
        {"expire", 2, 2, true, &CommandProcessor::cmd_expire},
        {"ttl", 1, 1, false, &CommandProcessor::cmd_ttl},
        {"persist", 1, 1, true, &CommandProcessor::cmd_persist},
        {"undo", 0, 0, true, &CommandProcessor::cmd_undo},
        {"flushall", 0, 0, true, &CommandProcessor::cmd_flushall},
        {"save", 0, 0, false, &CommandProcessor::cmd_save},
        {"help", 0, 0, false, &CommandProcessor::cmd_help},
        {"exit", 0, 0, false, &CommandProcessor::cmd_exit},
        {"quit", 0, 0, false, &CommandProcessor::cmd_exit},
    };
    for (const CommandSpec &spec : kCommands) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

std::string CommandProcessor::execute(const std::string &line) {
    std::vector<std::string> tokens;
    try {
        tokens = tokenize(line);
    } catch (const ParseError &error) {
        return format_error(error.what());
    }
    if (tokens.empty()) {
        return {}; // blank line: no output
    }

    // Active expiry: drop every expired key before running any command.
    store_.sweep_expired();

    const std::string name = to_lower(tokens[0]);
    const CommandSpec *spec = find_command(name);
    if (spec == nullptr) {
        return format_error("unknown command '" + tokens[0] + "'");
    }

    const std::size_t arg_count = tokens.size() - 1;
    if (arg_count < spec->min_args || arg_count > spec->max_args) {
        return format_error("wrong number of arguments for '" + name + "'");
    }

    Reply reply = (this->*(spec->handler))(tokens);
    if (reply.ok && spec->autosave) {
        // If the disk write fails, the change stays in memory and the user
        // sees the save error instead of the normal reply.
        if (auto save_error = save_snapshot()) {
            return *save_error;
        }
    }
    return reply.text;
}

bool CommandProcessor::exit_requested() const { return exit_requested_; }

std::optional<std::string> CommandProcessor::save_on_shutdown() { return save_snapshot(); }

std::optional<std::string> CommandProcessor::save_snapshot() {
    if (!snapshot_) {
        return std::nullopt;
    }
    try {
        snapshot_->save(store_);
    } catch (const PersistenceError &error) {
        return format_error(std::string("failed to save: ") + error.what());
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Undo helpers
// ---------------------------------------------------------------------------

UndoRecord CommandProcessor::capture(const std::vector<std::string> &keys) {
    UndoRecord record;
    std::unordered_set<std::string> seen;
    for (const std::string &key : keys) {
        if (seen.insert(key).second) { // each key once, even for "DEL a a"
            record.previous.emplace_back(key, store_.get_entry(key));
        }
    }
    return record;
}

void CommandProcessor::commit_if_changed(UndoRecord record) {
    const bool changed =
        std::any_of(record.previous.begin(), record.previous.end(),
                    [this](const auto &item) { return store_.peek(item.first) != item.second; });
    if (changed) {
        undo_.push(std::move(record));
    }
}

std::optional<std::int64_t> CommandProcessor::expiry_from_seconds(std::string_view seconds) const {
    const auto parsed = parse_int64(seconds);
    if (!parsed || *parsed <= 0) {
        return std::nullopt;
    }
    const std::int64_t now = store_.now_ms();
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    if (now < 0 || *parsed > (max - now) / 1000) {
        return std::nullopt; // would overflow the millisecond timestamp
    }
    return now + *parsed * 1000;
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

CommandProcessor::Reply CommandProcessor::cmd_set(const std::vector<std::string> &args) {
    // Accepted forms: SET key value | SET key value EX seconds
    std::optional<std::int64_t> expires_at;
    if (args.size() == 5 && to_lower(args[3]) == "ex") {
        expires_at = expiry_from_seconds(args[4]);
        if (!expires_at) {
            return {format_error(kInvalidExpire), false};
        }
    } else if (args.size() != 3) {
        return {format_error(kSyntaxError), false};
    }

    UndoRecord before = capture({args[1]});
    store_.set(args[1], args[2], expires_at);
    commit_if_changed(std::move(before));
    return {format_ok(), true};
}

CommandProcessor::Reply CommandProcessor::cmd_get(const std::vector<std::string> &args) {
    const auto value = store_.get(args[1]);
    return {value ? format_string(*value) : format_nil(), true};
}

CommandProcessor::Reply CommandProcessor::cmd_del(const std::vector<std::string> &args) {
    const std::vector<std::string> keys(std::next(args.begin()), args.end());
    UndoRecord before = capture(keys);
    std::int64_t deleted = 0;
    for (const std::string &key : keys) {
        if (store_.erase(key)) {
            ++deleted;
        }
    }
    commit_if_changed(std::move(before));
    return {format_integer(deleted), true};
}

CommandProcessor::Reply CommandProcessor::cmd_exists(const std::vector<std::string> &args) {
    return {format_integer(store_.exists(args[1]) ? 1 : 0), true};
}

CommandProcessor::Reply CommandProcessor::cmd_keys(const std::vector<std::string> &args) {
    const std::string_view pattern = args.size() > 1 ? std::string_view(args[1]) : "*";
    return {format_list(store_.keys(pattern)), true};
}

CommandProcessor::Reply CommandProcessor::cmd_dbsize(const std::vector<std::string> &) {
    return {format_integer(static_cast<std::int64_t>(store_.size())), true};
}

CommandProcessor::Reply CommandProcessor::increment(const std::string &key, std::int64_t delta) {
    UndoRecord before = capture({key});
    const auto result = store_.incr_by(key, delta);
    if (!result) {
        return {format_error(kNotInteger), false}; // store unchanged, no undo record
    }
    commit_if_changed(std::move(before));
    return {format_integer(*result), true};
}

CommandProcessor::Reply CommandProcessor::cmd_incr(const std::vector<std::string> &args) {
    return increment(args[1], 1);
}

CommandProcessor::Reply CommandProcessor::cmd_decr(const std::vector<std::string> &args) {
    return increment(args[1], -1);
}

CommandProcessor::Reply CommandProcessor::cmd_incrby(const std::vector<std::string> &args) {
    const auto delta = parse_int64(args[2]);
    if (!delta) {
        return {format_error(kNotInteger), false};
    }
    return increment(args[1], *delta);
}

CommandProcessor::Reply CommandProcessor::cmd_expire(const std::vector<std::string> &args) {
    const auto expires_at = expiry_from_seconds(args[2]);
    if (!expires_at) {
        return {format_error(kInvalidExpire), false};
    }
    UndoRecord before = capture({args[1]});
    const bool existed = store_.expire_at(args[1], *expires_at);
    commit_if_changed(std::move(before));
    return {format_integer(existed ? 1 : 0), true};
}

CommandProcessor::Reply CommandProcessor::cmd_ttl(const std::vector<std::string> &args) {
    return {format_integer(store_.ttl_seconds(args[1])), true};
}

CommandProcessor::Reply CommandProcessor::cmd_persist(const std::vector<std::string> &args) {
    UndoRecord before = capture({args[1]});
    const bool removed = store_.persist(args[1]);
    commit_if_changed(std::move(before));
    return {format_integer(removed ? 1 : 0), true};
}

CommandProcessor::Reply CommandProcessor::cmd_undo(const std::vector<std::string> &) {
    auto record = undo_.pop();
    if (!record) {
        return {format_error("nothing to undo"), false};
    }
    // Put every touched key back exactly as it was, expiry included. An entry
    // whose old expiry has since passed is removed by the next sweep.
    for (auto &[key, previous] : record->previous) {
        if (previous) {
            store_.put_entry(key, std::move(*previous));
        } else {
            store_.erase(key);
        }
    }
    return {format_ok(), true};
}

CommandProcessor::Reply CommandProcessor::cmd_flushall(const std::vector<std::string> &) {
    UndoRecord before = capture(store_.keys("*"));
    store_.clear();
    commit_if_changed(std::move(before));
    return {format_ok(), true};
}

CommandProcessor::Reply CommandProcessor::cmd_save(const std::vector<std::string> &) {
    if (!snapshot_) {
        return {format_error("persistence disabled"), false};
    }
    if (auto save_error = save_snapshot()) {
        return {*save_error, false};
    }
    return {format_ok(), true};
}

CommandProcessor::Reply CommandProcessor::cmd_help(const std::vector<std::string> &) {
    return {std::string(kHelpText), true};
}

CommandProcessor::Reply CommandProcessor::cmd_exit(const std::vector<std::string> &) {
    // Save first so a failed save can be reported and the session kept alive
    // instead of quitting and losing data.
    if (auto save_error = save_snapshot()) {
        return {*save_error, false};
    }
    exit_requested_ = true;
    return {"Bye\n", true};
}

} // namespace minikv
