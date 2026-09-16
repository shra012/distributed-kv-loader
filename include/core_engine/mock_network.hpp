/// Provides a bounded in-process full-duplex socket transport.

#pragma once

#include "core_engine/cancel_token.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace core_engine {

/// Identifies a mock node address.
class NodeId final {
public:
  /// Creates an address from its numeric value.
  explicit constexpr NodeId(std::uint8_t value) noexcept : value_(value) {}

  /// Returns the numeric address value.
  [[nodiscard]] constexpr std::uint8_t value() const noexcept { return value_; }

  /// Compares two addresses.
  friend constexpr bool operator==(NodeId left, NodeId right) noexcept {
    return left.value_ == right.value_;
  }

private:
  std::uint8_t value_;
};

/// Selects socket directions for shutdown.
enum class ShutdownHow { Read, Write, Both };

/// Identifies the last mock socket failure.
enum class SocketError {
  None,
  BadFileDescriptor,
  NotConnected,
  ConnectionRefused,
  ConnectionReset,
  BrokenPipe,
  Cancelled,
  Injected
};

/// Selects a deterministic transport fault.
enum class NetworkFault { FailSend, FailRecv, CorruptSend, DropConnection };

/// Mirrors bounded POSIX socket operations within one process.
class MockNetwork final {
public:
  /// Creates a network with fixed per-direction channel capacity.
  MockNetwork(std::size_t channel_bytes, CancelToken& cancel_token);

  /// Closes all descriptors and wakes blocked operations.
  ~MockNetwork();

  MockNetwork(const MockNetwork&) = delete;
  MockNetwork& operator=(const MockNetwork&) = delete;
  MockNetwork(MockNetwork&&) = delete;
  MockNetwork& operator=(MockNetwork&&) = delete;

  /// Creates an unbound socket descriptor or returns -1.
  [[nodiscard]] int socket();

  /// Binds an unconnected socket to one address.
  [[nodiscard]] int bind(int fd, NodeId address);

  /// Converts a bound socket into a listener with bounded backlog.
  [[nodiscard]] int listen(int fd, int backlog);

  /// Blocks until one pending connection is accepted or an error occurs.
  [[nodiscard]] int accept(int fd);

  /// Connects immediately to a listening address without waiting for accept.
  [[nodiscard]] int connect(int fd, NodeId address);

  /// Sends at least one byte unless length is zero or an error occurs.
  [[nodiscard]] std::ptrdiff_t send(int fd, const void* buffer, std::size_t length);

  /// Receives available bytes, zero on orderly EOF, or -1 on error.
  [[nodiscard]] std::ptrdiff_t recv(int fd, void* buffer, std::size_t length);

  /// Shuts down selected directions and wakes affected peers.
  [[nodiscard]] int shutdown(int fd, ShutdownHow how);

  /// Closes a descriptor and wakes affected waiters.
  [[nodiscard]] int close(int fd);

  /// Returns the descriptor's most recent error or bad descriptor.
  [[nodiscard]] SocketError last_error(int fd) const;

  /// Limits each successful send or receive operation.
  void set_max_operation_bytes(std::size_t bytes);

  /// Forces successful send and receive operations to one byte.
  void set_one_byte_mode(bool enabled) noexcept;

  /// Injects a fault on the selected descriptor operation occurrence.
  [[nodiscard]] int inject_fault(int fd, NetworkFault fault, std::size_t occurrence);

  /// Sends the full buffer or returns -1 on the first error.
  [[nodiscard]] std::ptrdiff_t send_all(int fd, const void* buffer, std::size_t length);

  /// Receives the full buffer, zero on initial EOF, or -1 on error or partial EOF.
  [[nodiscard]] std::ptrdiff_t recv_exact(int fd, void* buffer, std::size_t length);

private:
  class Impl;
  std::shared_ptr<Impl> impl_;
  CancelToken::Subscription cancellation_;
};

}
