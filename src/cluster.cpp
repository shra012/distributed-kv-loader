/// Implements the cluster coordinator: three-phase startup, join, and snapshot collection.

#include "core_engine/cluster.hpp"

#include "core_engine/schema.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>

namespace core_engine {

Cluster::Cluster(const Config& config, ClusterOptions options)
    : config_(config), options_(options), network_(config.channel_bytes(), cancel_token_) {
  nodes_.reserve(config_.node_count());
  for (std::uint32_t index = 0U; index < config_.node_count(); ++index) {
    nodes_.push_back(std::make_unique<Node>(NodeId(static_cast<std::uint8_t>(index)), config_,
                                            network_, cancel_token_, first_failure_));
  }
}

Cluster::~Cluster() = default;

void Cluster::run() {
  try {
    for (const std::unique_ptr<Node>& node : nodes_) {
      node->bind_and_listen();
    }
    for (std::uint32_t destination = 0U; destination < config_.node_count(); ++destination) {
      for (std::uint32_t source = 0U; source < config_.node_count(); ++source) {
        if (source == destination) {
          continue;
        }
        nodes_[source]->connect_to(NodeId(static_cast<std::uint8_t>(destination)));
      }
    }
    for (std::uint32_t destination = 0U; destination < config_.node_count(); ++destination) {
      for (std::uint32_t source = 0U; source < config_.node_count(); ++source) {
        if (source == destination) {
          continue;
        }
        nodes_[destination]->accept_from(NodeId(static_cast<std::uint8_t>(source)));
      }
    }
    if (options_.transport_fault.has_value()) {
      const TransportFaultInjection& injection = *options_.transport_fault;
      if (injection.source >= config_.node_count() ||
          injection.destination >= config_.node_count() ||
          injection.source == injection.destination || injection.occurrence == 0U) {
        throw Error(ErrorCode::InternalInvariant, "invalid cluster transport fault injection");
      }
      const bool inbound = injection.endpoint == FaultEndpoint::Receiver;
      const std::uint32_t node = inbound ? injection.destination : injection.source;
      const std::uint32_t peer = inbound ? injection.source : injection.destination;
      nodes_[node]->inject_transport_fault(NodeId(static_cast<std::uint8_t>(peer)), inbound,
                                           injection.fault, injection.occurrence);
    }
    if (options_.misroute_source.has_value()) {
      if (*options_.misroute_source >= config_.node_count() || config_.node_count() < 3U) {
        throw Error(ErrorCode::InternalInvariant, "invalid cluster misroute injection");
      }
      nodes_[*options_.misroute_source]->misroute_next_record();
    }
  } catch (const Error& error) {
    first_failure_.report(error);
    cancel_token_.cancel();
  } catch (const std::exception& error) {
    first_failure_.report(Error(ErrorCode::Transport, error.what()));
    cancel_token_.cancel();
  }

  if (!first_failure_.has_error()) {
    for (const std::unique_ptr<Node>& node : nodes_) {
      node->start();
    }
  }

  for (const std::unique_ptr<Node>& node : nodes_) {
    node->join();
  }

  stats_.reserve(nodes_.size());
  for (const std::unique_ptr<Node>& node : nodes_) {
    stats_.push_back(node->stats());
  }
  first_error_ = first_failure_.error();
}

const std::optional<Error>& Cluster::first_error() const noexcept { return first_error_; }

const std::vector<NodeStats>& Cluster::stats() const noexcept { return stats_; }

const Store& Cluster::store(std::uint32_t node) const { return nodes_.at(node)->store(); }

void Cluster::reserve_stores(const std::vector<std::uint64_t>& expected,
                             std::uint64_t input_bytes) {
  if (expected.size() != nodes_.size()) {
    throw Error(ErrorCode::ManifestMismatch, "manifest node count does not match configuration");
  }
  std::uint64_t minimum_line_bytes =
      config_.schema().key_field().type == FieldType::String ? 1U : 0U;
  for (const Field& field : config_.schema().fields()) {
    minimum_line_bytes += field.type == FieldType::Int32 ? 2U : 1U;
  }
  const std::uint64_t input_limit = input_bytes / minimum_line_bytes;
  for (std::size_t node = 0U; node < nodes_.size(); ++node) {
    const std::uint64_t hint = std::min(expected[node], input_limit);
    nodes_[node]->reserve_store(static_cast<std::size_t>(hint));
  }
}

std::vector<std::uint64_t> Cluster::estimate_records_per_node(std::uint64_t input_bytes) const {
  constexpr std::size_t kSampleBytes = 64U * 1024U;
  std::vector<std::uint64_t> estimate(nodes_.size(), 0U);
  for (const auto& path : config_.input_paths()) {
    if (!path.has_value()) {
      continue;
    }
    std::ifstream input(*path, std::ios::binary);
    std::string sample(kSampleBytes, '\0');
    input.read(sample.data(), static_cast<std::streamsize>(sample.size()));
    const auto sampled = static_cast<std::uint64_t>(input.gcount());
    const auto lines = static_cast<std::uint64_t>(
        std::count(sample.begin(), sample.begin() + input.gcount(), '\n'));
    if (lines == 0U) {
      continue;
    }
    std::fill(estimate.begin(), estimate.end(), input_bytes * lines / sampled / nodes_.size());
    break;
  }
  return estimate;
}

}
