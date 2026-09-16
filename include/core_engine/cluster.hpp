/// Declares the cluster coordinator: startup, load, join, and snapshot collection.

#pragma once

#include "core_engine/cancel_token.hpp"
#include "core_engine/config.hpp"
#include "core_engine/error.hpp"
#include "core_engine/mock_network.hpp"
#include "core_engine/node.hpp"
#include "core_engine/stats.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace core_engine {

/// Selects the connection end that receives an injected fault.
enum class FaultEndpoint { Sender, Receiver };

/// Describes one transport fault injected before threads start.
struct TransportFaultInjection final {
  std::uint32_t source;
  std::uint32_t destination;
  FaultEndpoint endpoint;
  NetworkFault fault;
  std::size_t occurrence;
};

/// Holds optional fault injections used by failure tests.
struct ClusterOptions final {
  std::optional<TransportFaultInjection> transport_fault;
  std::optional<std::uint32_t> misroute_source;
};

/// Runs one load job over a mock network with one Node per configured node. config must outlive
/// the cluster.
class Cluster final {
public:
  /// Creates the nodes without starting threads.
  explicit Cluster(const Config& config, ClusterOptions options = {});

  /// Joins any running node.
  ~Cluster();

  Cluster(const Cluster&) = delete;
  Cluster& operator=(const Cluster&) = delete;
  Cluster(Cluster&&) = delete;
  Cluster& operator=(Cluster&&) = delete;

  /// Connects every node pair, loads every input, and joins every thread. Never throws; check
  /// first_error() afterward.
  void run();

  /// Returns the first failure reported during run(), or nullopt.
  [[nodiscard]] const std::optional<Error>& first_error() const noexcept;

  /// Returns counter snapshots in node-ID order; valid after run().
  [[nodiscard]] const std::vector<NodeStats>& stats() const noexcept;

  /// Returns one node's store; valid after run().
  [[nodiscard]] const Store& store(std::uint32_t node) const;

  /// Pre-sizes stores from per-node record hints capped by what input_bytes could hold; call
  /// before run().
  void reserve_stores(const std::vector<std::uint64_t>& expected, std::uint64_t input_bytes);

  /// Estimates records per node from the line density of the first readable input's first 64 KiB.
  [[nodiscard]] std::vector<std::uint64_t>
  estimate_records_per_node(std::uint64_t input_bytes) const;

private:
  const Config& config_;
  ClusterOptions options_;
  CancelToken cancel_token_;
  FirstFailure first_failure_;
  MockNetwork network_;
  std::vector<std::unique_ptr<Node>> nodes_;
  std::optional<Error> first_error_;
  std::vector<NodeStats> stats_;
};

}
