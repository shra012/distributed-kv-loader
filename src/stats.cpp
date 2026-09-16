/// Implements thread-safe per-node statistics snapshots.

#include "core_engine/stats.hpp"

namespace core_engine {

NodeStats NodeCounters::snapshot() const noexcept {
  const auto value = [this](Counter counter) {
    return values_[static_cast<std::size_t>(counter)].load(std::memory_order_relaxed);
  };
  return NodeStats{value(Counter::LinesRead),
                   value(Counter::Parsed),
                   value(Counter::Local),
                   value(Counter::RemoteOut),
                   value(Counter::FramesOut),
                   value(Counter::BytesOut),
                   value(Counter::RemoteIn),
                   value(Counter::Ingested),
                   value(Counter::Loaded),
                   value(Counter::Duplicates),
                   value(Counter::FramesIn),
                   value(Counter::BytesIn),
                   value(Counter::EndMarkersReceived),
                   value(Counter::Malformed),
                   value(Counter::Transport),
                   value(Counter::Codec),
                   value(Counter::Conflict)};
}

}
