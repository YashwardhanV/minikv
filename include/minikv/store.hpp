#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "minikv/clock.hpp"
#include "minikv/entry.hpp"

namespace minikv {

/// Strictly parses a signed 64-bit decimal integer.
///
/// Accepts only an optional leading '-' followed by digits, with nothing else:
/// "12" and "-7" pass; "+3", " 4", "4a", "" and out-of-range values fail.
/// Shared by INCR/DECR/INCRBY and by every command that takes a number.
std::optional<std::int64_t> parse_int64(std::string_view text);

/// The in-memory key-value data, plus all TTL (expiry) rules.
///
/// Every public operation applies lazy expiry: a key whose expiry time has
/// passed is deleted the moment it is touched and then treated as missing.
/// sweep_expired() additionally removes every expired key in one pass.
class Store {
  public:
    /// The clock is borrowed, not owned; it must outlive the Store.
    explicit Store(const Clock &clock);

    /// Current time from the injected clock, in Unix milliseconds.
    std::int64_t now_ms() const;

    /// Returns the live entry (value + expiry) for `key`, or nullopt if the key
    /// is missing or expired.
    std::optional<Entry> get_entry(const std::string &key);

    /// Returns the live value for `key`, or nullopt if missing or expired.
    std::optional<std::string> get(const std::string &key);

    /// Like get_entry() but never modifies the map. Expired keys read as
    /// missing. Used to compare state before and after a command.
    std::optional<Entry> peek(const std::string &key) const;

    /// Sets `key` to `value`, replacing any previous value AND expiry.
    void set(const std::string &key, std::string value,
             std::optional<std::int64_t> expires_at_ms = std::nullopt);

    /// Stores `entry` exactly as given (value and expiry), with no expiry
    /// check. Used by UNDO and by snapshot loading to restore saved state.
    void put_entry(const std::string &key, Entry entry);

    /// Deletes `key`. Returns true if a live key was actually removed.
    bool erase(const std::string &key);

    /// True if `key` exists and has not expired.
    bool exists(const std::string &key);

    /// Adds `delta` to the integer stored at `key` and keeps its expiry.
    /// A missing key counts as 0. Returns the new value, or nullopt (with the
    /// store unchanged) if the current value is not an integer or the result
    /// would overflow a signed 64-bit integer.
    std::optional<std::int64_t> incr_by(const std::string &key, std::int64_t delta);

    /// Sets an absolute expiry time on an existing key.
    /// Returns true if the key exists, false if it is missing.
    bool expire_at(const std::string &key, std::int64_t expires_at_ms);

    /// Remaining time to live in whole seconds, rounded up.
    /// Returns -2 if the key is missing and -1 if it has no expiry.
    std::int64_t ttl_seconds(const std::string &key);

    /// Removes the expiry from `key`. Returns true only if an expiry existed.
    bool persist(const std::string &key);

    /// All live keys matching the glob `pattern`, sorted alphabetically.
    std::vector<std::string> keys(std::string_view pattern = "*");

    /// Number of live keys.
    std::size_t size();

    /// Removes every key.
    void clear();

    /// Removes every expired key and returns how many were removed.
    std::size_t sweep_expired();

    /// Copy of every live entry, sorted by key. This is what gets written to
    /// disk, so the order is deterministic and expired entries are excluded.
    std::vector<std::pair<std::string, Entry>> snapshot() const;

  private:
    bool is_expired(const Entry &entry) const;

    /// Finds `key`, deleting it first if it has expired. Returns nullptr if
    /// the key is missing or was just expired. The pointer is valid until the
    /// next modification of the map.
    Entry *find_live(const std::string &key);

    const Clock &clock_;
    std::unordered_map<std::string, Entry> data_;
};

} // namespace minikv
