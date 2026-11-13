#include "minikv/store.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <system_error>

#include "minikv/glob.hpp"

namespace minikv {

std::optional<std::int64_t> parse_int64(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char *first = text.data();
    const char *last = text.data() + text.size();
    // from_chars already rejects '+', leading whitespace and out-of-range values;
    // checking `ptr == last` rejects trailing junk such as "4a".
    const auto [ptr, error] = std::from_chars(first, last, value);
    if (error != std::errc() || ptr != last) {
        return std::nullopt;
    }
    return value;
}

Store::Store(const Clock &clock) : clock_(clock) {}

std::int64_t Store::now_ms() const { return clock_.now_ms(); }

bool Store::is_expired(const Entry &entry) const {
    return entry.expires_at_ms.has_value() && now_ms() >= *entry.expires_at_ms;
}

Entry *Store::find_live(const std::string &key) {
    auto it = data_.find(key);
    if (it == data_.end()) {
        return nullptr;
    }
    if (is_expired(it->second)) {
        data_.erase(it); // lazy expiry
        return nullptr;
    }
    return &it->second;
}

std::optional<Entry> Store::get_entry(const std::string &key) {
    if (const Entry *entry = find_live(key)) {
        return *entry;
    }
    return std::nullopt;
}

std::optional<std::string> Store::get(const std::string &key) {
    if (const Entry *entry = find_live(key)) {
        return entry->value;
    }
    return std::nullopt;
}

std::optional<Entry> Store::peek(const std::string &key) const {
    const auto it = data_.find(key);
    if (it == data_.end() || is_expired(it->second)) {
        return std::nullopt;
    }
    return it->second;
}

void Store::set(const std::string &key, std::string value,
                std::optional<std::int64_t> expires_at_ms) {
    data_.insert_or_assign(key, Entry{std::move(value), expires_at_ms});
}

void Store::put_entry(const std::string &key, Entry entry) {
    data_.insert_or_assign(key, std::move(entry));
}

bool Store::erase(const std::string &key) {
    if (find_live(key) == nullptr) {
        return false;
    }
    data_.erase(key);
    return true;
}

bool Store::exists(const std::string &key) { return find_live(key) != nullptr; }

std::optional<std::int64_t> Store::incr_by(const std::string &key, std::int64_t delta) {
    Entry *entry = find_live(key);

    std::int64_t current = 0; // a missing key counts as 0
    if (entry != nullptr) {
        const auto parsed = parse_int64(entry->value);
        if (!parsed) {
            return std::nullopt;
        }
        current = *parsed;
    }

    // Check for overflow before adding: signed overflow is undefined behaviour.
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
    if ((delta > 0 && current > max - delta) || (delta < 0 && current < min - delta)) {
        return std::nullopt;
    }
    const std::int64_t result = current + delta;

    if (entry != nullptr) {
        entry->value = std::to_string(result); // keeps the existing expiry
    } else {
        data_.insert_or_assign(key, Entry{std::to_string(result), std::nullopt});
    }
    return result;
}

bool Store::expire_at(const std::string &key, std::int64_t expires_at_ms) {
    Entry *entry = find_live(key);
    if (entry == nullptr) {
        return false;
    }
    entry->expires_at_ms = expires_at_ms;
    return true;
}

std::int64_t Store::ttl_seconds(const std::string &key) {
    const Entry *entry = find_live(key);
    if (entry == nullptr) {
        return -2;
    }
    if (!entry->expires_at_ms) {
        return -1;
    }
    // Live key, so remaining > 0. Round up: 1500 ms left reports 2 seconds.
    // Written as divide-then-adjust so it cannot overflow near INT64_MAX.
    const std::int64_t remaining = *entry->expires_at_ms - now_ms();
    return remaining / 1000 + (remaining % 1000 != 0 ? 1 : 0);
}

bool Store::persist(const std::string &key) {
    Entry *entry = find_live(key);
    if (entry == nullptr || !entry->expires_at_ms) {
        return false;
    }
    entry->expires_at_ms.reset();
    return true;
}

std::vector<std::string> Store::keys(std::string_view pattern) {
    sweep_expired();
    std::vector<std::string> result;
    for (const auto &item : data_) {
        if (glob_match(pattern, item.first)) {
            result.push_back(item.first);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::size_t Store::size() {
    sweep_expired();
    return data_.size();
}

void Store::clear() { data_.clear(); }

std::size_t Store::sweep_expired() {
    std::size_t removed = 0;
    for (auto it = data_.begin(); it != data_.end();) {
        if (is_expired(it->second)) {
            it = data_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::vector<std::pair<std::string, Entry>> Store::snapshot() const {
    std::vector<std::pair<std::string, Entry>> result;
    result.reserve(data_.size());
    for (const auto &item : data_) {
        if (!is_expired(item.second)) {
            result.emplace_back(item.first, item.second);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
    return result;
}

} // namespace minikv
