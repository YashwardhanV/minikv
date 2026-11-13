#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "minikv/entry.hpp"

namespace minikv {

/// The state of every key a mutating command touched, captured just before
/// the command ran. Undoing means putting each key back to this state.
struct UndoRecord {
    /// (key, previous entry). nullopt means the key did not exist before.
    std::vector<std::pair<std::string, std::optional<Entry>>> previous;
};

/// Bounded history of undo records. When full, the oldest record is dropped
/// so memory use stays fixed no matter how long the session runs.
class UndoStack {
  public:
    static constexpr std::size_t kDefaultCapacity = 100;

    explicit UndoStack(std::size_t capacity = kDefaultCapacity);

    /// Adds a record on top, dropping the oldest one if the stack is full.
    void push(UndoRecord record);

    /// Removes and returns the most recent record, or nullopt if empty.
    std::optional<UndoRecord> pop();

    /// Number of records currently held.
    std::size_t size() const;

    /// True if there is nothing to undo.
    bool empty() const;

    /// Maximum number of records kept.
    std::size_t capacity() const;

  private:
    std::size_t capacity_;
    std::deque<UndoRecord> records_; // front = oldest, back = newest
};

} // namespace minikv
