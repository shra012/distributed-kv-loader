/// Declares post-join invariant checking and the deterministic job report grammar.

#pragma once

#include "core_engine/cluster.hpp"
#include "core_engine/config.hpp"
#include "core_engine/generator.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace core_engine {

/// Holds one node's ownership scan outcome for the VERIFY node= lines.
struct OwnershipVerification final {
  std::uint32_t node;
  std::size_t scanned;
  std::size_t misowned;
};

/// Holds the four cluster-wide invariant results.
struct Invariants final {
  bool ownership;
  bool uniqueness;
  bool accounting;
  bool completion;

  /// Returns true only when every invariant passed.
  [[nodiscard]] bool all_pass() const noexcept {
    return ownership && uniqueness && accounting && completion;
  }
};

/// Runs the per-node ownership scan for every node in a completed cluster.
[[nodiscard]] std::vector<OwnershipVerification> scan_ownership(const Cluster& cluster,
                                                                const Config& config);

/// Checks ownership, uniqueness, accounting, and completion invariants.
[[nodiscard]] Invariants check_invariants(const Config& config, const std::vector<NodeStats>& stats,
                                          const std::vector<OwnershipVerification>& scans);

/// Returns whether report counters match every manifest expectation.
[[nodiscard]] bool report_matches_manifest(const Config& config, const Manifest& manifest,
                                           const std::vector<NodeStats>& stats);

/// Formats the success report: JOB, NODE lines, DIST, VERIFY lines, optional EXPECT, and TIMING.
/// Every line except TIMING is deterministic for fixed input.
[[nodiscard]] std::string format_report(const Config& config, const std::vector<NodeStats>& stats,
                                        const std::vector<OwnershipVerification>& scans,
                                        const Invariants& invariants,
                                        const std::optional<Manifest>& manifest,
                                        std::chrono::milliseconds elapsed,
                                        std::uint64_t input_bytes_total);

/// Validates a saved report against a manifest or throws a verification error.
void verify_report(const std::filesystem::path& report_path, const Manifest& manifest);

}
