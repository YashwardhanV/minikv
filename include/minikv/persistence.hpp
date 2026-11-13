#pragma once

#include <filesystem>
#include <ostream>
#include <stdexcept>

#include "minikv/store.hpp"

namespace minikv {

/// Thrown when the snapshot file cannot be read or written.
class PersistenceError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// What happened when a snapshot was loaded.
enum class LoadResult {
    Loaded,  ///< File read successfully; the store now holds its contents.
    Missing, ///< No file yet; the store was left empty. Not an error.
    Corrupt, ///< File was unreadable as a snapshot; moved aside, store empty.
};

/// Reads and writes the on-disk snapshot of a Store.
///
/// File format (version 1): the line "MINIKV 1", then one record per key:
///   "<key_len> <value_len> <expires_at_ms or -1>\n" <key bytes><value bytes> "\n"
/// Length prefixes mean keys and values may contain any bytes, including
/// spaces, quotes and newlines.
class SnapshotFile {
  public:
    explicit SnapshotFile(std::filesystem::path path);

    /// The snapshot file location.
    const std::filesystem::path &path() const;

    /// Writes every live entry of `store` to disk, sorted by key.
    ///
    /// The data goes to "<path>.tmp" first and is then renamed over the real
    /// file, so a crash mid-write can never leave a half-written snapshot.
    /// Throws PersistenceError on any I/O failure; the old file stays intact.
    void save(const Store &store) const;

    /// Replaces the contents of `store` with the snapshot on disk.
    ///
    /// Expired records are skipped. If the file is corrupt it is renamed to
    /// "<path>.corrupt" (never silently overwritten), a warning is written to
    /// `warnings`, and the store is left empty. Throws PersistenceError if the
    /// file exists but cannot be read, or if a corrupt file cannot be moved.
    LoadResult load(Store &store, std::ostream &warnings) const;

  private:
    std::filesystem::path path_;
};

} // namespace minikv
