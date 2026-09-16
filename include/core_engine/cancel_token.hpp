/// Provides shared cancellation state with race-safe waiter notifications.

#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace core_engine {

/// Shares one-way cancellation and wakes registered blocking operations.
class CancelToken final {
private:
  struct CallbackState final {
    explicit CallbackState(std::function<void()> callback_value)
        : callback(std::move(callback_value)) {}

    std::mutex mutex;
    bool active{true};
    std::function<void()> callback;
  };

  struct State final {
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::vector<std::weak_ptr<CallbackState>> callbacks;
  };

public:
  /// Owns a registration and prevents callbacks after its destruction.
  class Subscription final {
  public:
    /// Creates an empty registration.
    Subscription() = default;

    /// Transfers a registration without duplicating it.
    Subscription(Subscription&& other) noexcept : callback_(std::move(other.callback_)) {}

    /// Replaces this registration by transferring another registration.
    Subscription& operator=(Subscription&& other) noexcept {
      if (this != &other) {
        reset();
        callback_ = std::move(other.callback_);
      }
      return *this;
    }

    /// Unregisters the callback after any active invocation completes.
    ~Subscription() { reset(); }

    /// Subscriptions have unique registration ownership.
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

  private:
    friend class CancelToken;

    explicit Subscription(std::shared_ptr<CallbackState> callback)
        : callback_(std::move(callback)) {}

    void reset() {
      if (!callback_) {
        return;
      }
      std::shared_ptr<CallbackState> callback = std::move(callback_);
      const std::lock_guard<std::mutex> lock(callback->mutex);
      callback->active = false;
      callback->callback = {};
    }

    std::shared_ptr<CallbackState> callback_;
  };

  /// Creates a non-cancelled shared token.
  CancelToken() : state_(std::make_shared<State>()) {}

  /// Returns true after cancellation has been requested.
  [[nodiscard]] bool is_cancelled() const noexcept {
    return state_->cancelled.load(std::memory_order_acquire);
  }

  /// Requests cancellation once and synchronously wakes all current subscribers.
  bool cancel() {
    if (state_->cancelled.exchange(true, std::memory_order_acq_rel)) {
      return false;
    }

    std::vector<std::weak_ptr<CallbackState>> callbacks;
    {
      const std::lock_guard<std::mutex> lock(state_->mutex);
      callbacks.swap(state_->callbacks);
    }
    for (const std::weak_ptr<CallbackState>& weak_callback : callbacks) {
      if (const std::shared_ptr<CallbackState> callback = weak_callback.lock()) {
        invoke(*callback);
      }
    }
    return true;
  }

  /// Registers a wake callback or invokes it immediately when already cancelled.
  [[nodiscard]] Subscription subscribe(std::function<void()> callback) {
    auto callback_state = std::make_shared<CallbackState>(std::move(callback));
    bool invoke_now = false;
    {
      const std::lock_guard<std::mutex> lock(state_->mutex);
      auto& callbacks = state_->callbacks;
      callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                     [](const auto& entry) { return entry.expired(); }),
                      callbacks.end());
      if (state_->cancelled.load(std::memory_order_acquire)) {
        invoke_now = true;
      } else {
        callbacks.emplace_back(callback_state);
      }
    }
    if (invoke_now) {
      invoke(*callback_state);
    }
    return Subscription(std::move(callback_state));
  }

private:
  static void invoke(CallbackState& callback) {
    const std::lock_guard<std::mutex> lock(callback.mutex);
    if (callback.active) {
      callback.callback();
    }
  }

  std::shared_ptr<State> state_;
};

}
