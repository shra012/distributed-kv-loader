/// Defines thread-safe per-node counters and immutable snapshots.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace core_engine {

/// Holds one immutable snapshot of every per-node counter.
struct NodeStats final {
  const std::uint64_t lines_read;
  const std::uint64_t parsed;
  const std::uint64_t local;
  const std::uint64_t remote_out;
  const std::uint64_t frames_out;
  const std::uint64_t bytes_out;
  const std::uint64_t remote_in;
  const std::uint64_t ingested;
  const std::uint64_t loaded;
  const std::uint64_t duplicates;
  const std::uint64_t frames_in;
  const std::uint64_t bytes_in;
  const std::uint64_t end_markers_received;
  const std::uint64_t malformed;
  const std::uint64_t transport;
  const std::uint64_t codec;
  const std::uint64_t conflict;
};

/// Names each per-node counter in NodeStats field order.
enum class Counter : std::size_t {
  LinesRead,
  Parsed,
  Local,
  RemoteOut,
  FramesOut,
  BytesOut,
  RemoteIn,
  Ingested,
  Loaded,
  Duplicates,
  FramesIn,
  BytesIn,
  EndMarkersReceived,
  Malformed,
  Transport,
  Codec,
  Conflict,
  Count
};

/// Owns one atomic value per Counter; add and snapshot are safe from any thread.
class NodeCounters final {
public:
  /// Adds delta to counter.
  void add(Counter counter, std::uint64_t delta = 1U) noexcept {
    static_cast<void>(
        values_[static_cast<std::size_t>(counter)].fetch_add(delta, std::memory_order_relaxed));
  }

  /// Returns a point-in-time snapshot of every counter.
  [[nodiscard]] NodeStats snapshot() const noexcept;

private:
  std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Counter::Count)> values_{};
};

}
