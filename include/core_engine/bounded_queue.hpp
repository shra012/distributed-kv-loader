/// Provides a bounded FIFO queue with blocking close and cancellation semantics.

#pragma once

#include "core_engine/cancel_token.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace core_engine {

/// Identifies why a blocking queue operation returned.
enum class QueueStatus { Success, Closed, Cancelled };

/// Owns a fixed-capacity thread-safe FIFO bound to one cancellation token.
template <typename Value> class BoundedQueue final {
private:
  struct State final {
    explicit State(std::size_t capacity_value) : capacity(capacity_value) {}

    std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::deque<Value> values;
    const std::size_t capacity;
    bool closed{false};
    bool cancelled{false};
  };

public:
  /// Creates an open queue; capacity must be greater than zero and the token must outlive it.
  BoundedQueue(std::size_t capacity, CancelToken& cancel_token)
      : state_(create_state(capacity)),
        cancellation_(cancel_token.subscribe([weak_state = std::weak_ptr<State>(state_)] {
          if (const std::shared_ptr<State> state = weak_state.lock()) {
            cancel_state(*state);
          }
        })) {}

  /// Queues have stable synchronization identity and cannot be copied or moved.
  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;
  BoundedQueue(BoundedQueue&&) = delete;
  BoundedQueue& operator=(BoundedQueue&&) = delete;

  /// Waits for capacity and inserts one value, or returns closed or cancelled.
  [[nodiscard]] QueueStatus push(Value value) {
    const std::shared_ptr<State> state = state_;
    std::unique_lock<std::mutex> lock(state->mutex);
    state->not_full.wait(lock, [&state] {
      return state->cancelled || state->closed || state->values.size() < state->capacity;
    });
    if (state->cancelled) {
      return QueueStatus::Cancelled;
    }
    if (state->closed) {
      return QueueStatus::Closed;
    }
    state->values.push_back(std::move(value));
    lock.unlock();
    state->not_empty.notify_one();
    return QueueStatus::Success;
  }

  /// Waits for a value and removes it in FIFO order, draining values after close.
  [[nodiscard]] QueueStatus pop(Value& output) {
    const std::shared_ptr<State> state = state_;
    std::unique_lock<std::mutex> lock(state->mutex);
    state->not_empty.wait(
        lock, [&state] { return state->cancelled || state->closed || !state->values.empty(); });
    if (state->cancelled) {
      return QueueStatus::Cancelled;
    }
    if (state->values.empty()) {
      return QueueStatus::Closed;
    }
    output = std::move(state->values.front());
    state->values.pop_front();
    lock.unlock();
    state->not_full.notify_one();
    return QueueStatus::Success;
  }

  /// Closes the queue once, rejects future pushes, and wakes all waiters.
  bool close() {
    const std::shared_ptr<State> state = state_;
    {
      const std::lock_guard<std::mutex> lock(state->mutex);
      if (state->closed) {
        return false;
      }
      state->closed = true;
    }
    state->not_empty.notify_all();
    state->not_full.notify_all();
    return true;
  }

  /// Returns the configured maximum entry count.
  [[nodiscard]] std::size_t capacity() const noexcept { return state_->capacity; }

  /// Returns the current entry count under synchronization.
  [[nodiscard]] std::size_t size() const {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->values.size();
  }

  /// Returns whether close has been requested.
  [[nodiscard]] bool is_closed() const {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->closed;
  }

private:
  static std::shared_ptr<State> create_state(std::size_t capacity) {
    if (capacity == 0U) {
      throw std::invalid_argument("bounded queue capacity must be greater than zero");
    }
    return std::make_shared<State>(capacity);
  }

  static void cancel_state(State& state) {
    {
      const std::lock_guard<std::mutex> lock(state.mutex);
      state.cancelled = true;
    }
    state.not_empty.notify_all();
    state.not_full.notify_all();
  }

  std::shared_ptr<State> state_;
  CancelToken::Subscription cancellation_;
};

}
