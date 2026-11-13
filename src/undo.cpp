#include "minikv/undo.hpp"

namespace minikv {

UndoStack::UndoStack(std::size_t capacity) : capacity_(capacity) {}

void UndoStack::push(UndoRecord record) {
    if (capacity_ == 0) {
        return;
    }
    if (records_.size() == capacity_) {
        records_.pop_front(); // drop the oldest to make room
    }
    records_.push_back(std::move(record));
}

std::optional<UndoRecord> UndoStack::pop() {
    if (records_.empty()) {
        return std::nullopt;
    }
    UndoRecord top = std::move(records_.back());
    records_.pop_back();
    return top;
}

std::size_t UndoStack::size() const { return records_.size(); }

bool UndoStack::empty() const { return records_.empty(); }

std::size_t UndoStack::capacity() const { return capacity_; }

} // namespace minikv
