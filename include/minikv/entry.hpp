#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace minikv {

/// One stored value plus its optional expiry time.
struct Entry {
    std::string value;
    /// Absolute Unix time in milliseconds; nullopt means the key never expires.
    std::optional<std::int64_t> expires_at_ms;
};

/// Two entries are equal when both value and expiry match. Used by the undo
/// system to tell whether a command actually changed anything.
inline bool operator==(const Entry &lhs, const Entry &rhs) {
    return lhs.value == rhs.value && lhs.expires_at_ms == rhs.expires_at_ms;
}

inline bool operator!=(const Entry &lhs, const Entry &rhs) { return !(lhs == rhs); }

} // namespace minikv
