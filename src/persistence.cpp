#include "minikv/persistence.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace minikv {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kHeader = "MINIKV 1\n";

/// Parses `text` as a whole number of type T. Fails on any extra character.
template <typename T> bool parse_whole_number(std::string_view text, T &out) {
    if (text.empty()) {
        return false;
    }
    const char *last = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), last, out);
    return error == std::errc() && ptr == last;
}

/// Splits a record header "<key_len> <value_len> <expires>" into its numbers.
bool parse_record_header(std::string_view line, std::uint64_t &key_len, std::uint64_t &value_len,
                         std::int64_t &expires) {
    const std::size_t first_space = line.find(' ');
    if (first_space == std::string_view::npos) {
        return false;
    }
    const std::size_t second_space = line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) {
        return false;
    }
    return parse_whole_number(line.substr(0, first_space), key_len) &&
           parse_whole_number(line.substr(first_space + 1, second_space - first_space - 1),
                              value_len) &&
           parse_whole_number(line.substr(second_space + 1), expires) && expires >= -1;
}

/// Parses a whole snapshot. Returns false (and leaves `out` partially filled)
/// if anything is malformed. Records expired at `now_ms` are skipped.
bool parse_snapshot(std::string_view data, std::int64_t now_ms,
                    std::vector<std::pair<std::string, Entry>> &out) {
    if (data.substr(0, kHeader.size()) != kHeader) {
        return false;
    }
    std::size_t pos = kHeader.size();

    while (pos < data.size()) {
        const std::size_t newline = data.find('\n', pos);
        if (newline == std::string_view::npos) {
            return false;
        }
        std::uint64_t key_len = 0;
        std::uint64_t value_len = 0;
        std::int64_t expires = 0;
        if (!parse_record_header(data.substr(pos, newline - pos), key_len, value_len, expires)) {
            return false;
        }
        pos = newline + 1;

        // Bounds-check before reading: key bytes + value bytes + final '\n'.
        const std::uint64_t remaining = data.size() - pos;
        if (key_len > remaining || value_len > remaining - key_len ||
            remaining - key_len - value_len < 1) {
            return false;
        }
        const auto key_size = static_cast<std::size_t>(key_len);
        const auto value_size = static_cast<std::size_t>(value_len);
        std::string key(data.substr(pos, key_size));
        std::string value(data.substr(pos + key_size, value_size));
        pos += key_size + value_size;
        if (data[pos] != '\n') {
            return false;
        }
        ++pos;

        const bool has_expiry = expires != -1;
        if (has_expiry && now_ms >= expires) {
            continue; // already expired: drop it
        }
        out.emplace_back(std::move(key),
                         Entry{std::move(value),
                               has_expiry ? std::optional<std::int64_t>(expires) : std::nullopt});
    }
    return true;
}

fs::path with_suffix(const fs::path &path, const char *suffix) {
    fs::path result = path;
    result += suffix;
    return result;
}

} // namespace

SnapshotFile::SnapshotFile(fs::path path) : path_(std::move(path)) {}

const fs::path &SnapshotFile::path() const { return path_; }

void SnapshotFile::save(const Store &store) const {
    const fs::path tmp_path = with_suffix(path_, ".tmp");
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw PersistenceError("cannot open '" + tmp_path.string() + "' for writing");
        }

        out << kHeader;
        for (const auto &[key, entry] : store.snapshot()) {
            out << key.size() << ' ' << entry.value.size() << ' '
                << entry.expires_at_ms.value_or(-1) << '\n';
            out.write(key.data(), static_cast<std::streamsize>(key.size()));
            out.write(entry.value.data(), static_cast<std::streamsize>(entry.value.size()));
            out << '\n';
        }

        out.flush();
        out.close();
        if (!out) {
            std::error_code ignored;
            fs::remove(tmp_path, ignored);
            throw PersistenceError("error while writing '" + tmp_path.string() + "'");
        }
    }

    // rename() replaces the destination atomically on POSIX, so readers see
    // either the old snapshot or the new one, never a mix.
    std::error_code ec;
    fs::rename(tmp_path, path_, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp_path, ignored);
        throw PersistenceError("cannot rename '" + tmp_path.string() + "' to '" + path_.string() +
                               "': " + ec.message());
    }
}

LoadResult SnapshotFile::load(Store &store, std::ostream &warnings) const {
    std::error_code ec;
    const bool exists = fs::exists(path_, ec);
    if (ec) {
        throw PersistenceError("cannot access '" + path_.string() + "': " + ec.message());
    }
    if (!exists) {
        store.clear();
        return LoadResult::Missing;
    }

    std::string data;
    {
        std::ifstream in(path_, std::ios::binary);
        if (!in) {
            throw PersistenceError("cannot open '" + path_.string() + "' for reading");
        }
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (in.bad()) {
            throw PersistenceError("error while reading '" + path_.string() + "'");
        }
    }

    std::vector<std::pair<std::string, Entry>> records;
    if (!parse_snapshot(data, store.now_ms(), records)) {
        const fs::path corrupt_path = with_suffix(path_, ".corrupt");
        fs::rename(path_, corrupt_path, ec);
        if (ec) {
            // Refuse to continue: carrying on would overwrite the bad file on
            // the next save and destroy whatever could still be recovered.
            throw PersistenceError("snapshot '" + path_.string() +
                                   "' is corrupt and could not be moved to '" +
                                   corrupt_path.string() + "': " + ec.message());
        }
        warnings << "warning: snapshot '" << path_.string() << "' is corrupt; moved to '"
                 << corrupt_path.string() << "', starting empty\n";
        store.clear();
        return LoadResult::Corrupt;
    }

    store.clear();
    for (auto &[key, entry] : records) {
        store.put_entry(key, std::move(entry));
    }
    return LoadResult::Loaded;
}

} // namespace minikv
