/// Declares one simulated cluster node's sockets, threads, store, and counters.

#pragma once

#include "core_engine/cancel_token.hpp"
#include "core_engine/config.hpp"
#include "core_engine/error.hpp"
#include "core_engine/mock_network.hpp"
#include "core_engine/stats.hpp"
#include "core_engine/store.hpp"

#include <memory>

namespace core_engine {

/// Owns one node's sockets, threads, store, and counters. The coordinator calls every method from
/// one thread; stats() and store() are valid only after join().
class Node final {
public:
  /// Creates an idle node with no sockets or threads.
  Node(NodeId id, const Config& config, MockNetwork& network, CancelToken& cancel_token,
       FirstFailure& first_failure);

  /// Joins running threads and closes open sockets.
  ~Node();

  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;
  Node(Node&&) = delete;
  Node& operator=(Node&&) = delete;

  /// Creates the listening socket. Throws Error(Transport) on failure.
  void bind_and_listen();

  /// Connects to a listening peer. Throws Error(Transport) on failure.
  void connect_to(NodeId peer);

  /// Accepts the pending connection from source. Throws Error(Transport) on failure.
  void accept_from(NodeId source);

  /// Starts reader, receiver, and store worker threads once every connection exists.
  void start();

  /// Joins started threads and closes sockets; later calls do nothing.
  void join();

  /// Returns a snapshot of this node's counters.
  [[nodiscard]] NodeStats stats() const;

  /// Returns this node's store.
  [[nodiscard]] const Store& store() const noexcept;

  /// Returns this node's identifier.
  [[nodiscard]] NodeId id() const noexcept;

  /// Injects a fault on the inbound or outbound connection with peer; call before start().
  void inject_transport_fault(NodeId peer, bool inbound, NetworkFault fault,
                              std::size_t occurrence);

  /// Sends the next record to a non-owner node; call before start().
  void misroute_next_record() noexcept;

  /// Pre-sizes the store; call before start().
  void reserve_store(std::size_t hint);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
