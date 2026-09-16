/// Declares the routed record batch and its bounded incremental builder.

#pragma once

#include "core_engine/error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace core_engine {

/// Holds one routed batch: source, destination, sequence, and concatenated packed-record bytes.
struct Batch final {
  std::uint8_t source;
  std::uint8_t destination;
  std::uint64_t sequence;
  std::uint32_t record_count;
  std::string payload;
};

/// Accumulates packed records into a batch whose payload never exceeds a fixed byte limit.
class BatchBuilder final {
public:
  /// Creates an empty builder for one source/destination pair.
  BatchBuilder(std::uint8_t source, std::uint8_t destination, std::uint32_t batch_bytes_limit)
      : source_(source), destination_(destination), limit_(batch_bytes_limit) {
    payload_.reserve(limit_);
  }

  /// Returns false when the current batch must be taken before appending record_bytes.
  [[nodiscard]] bool fits(std::size_t record_bytes) const noexcept {
    return empty() || payload_.size() + record_bytes <= limit_;
  }

  /// Appends one packed record. Throws Error(InternalInvariant) if it exceeds the limit.
  void append(std::string_view packed_record) {
    if (payload_.size() + packed_record.size() > limit_) {
      throw Error(ErrorCode::InternalInvariant, "packed record exceeds the batch byte limit");
    }
    payload_.append(packed_record);
    ++record_count_;
  }

  /// Returns true when no record has been appended since construction or the last take.
  [[nodiscard]] bool empty() const noexcept { return record_count_ == 0U; }

  /// Returns the accumulated batch stamped with sequence and starts a new empty one.
  [[nodiscard]] Batch take(std::uint64_t sequence) {
    Batch batch{source_, destination_, sequence, record_count_, std::move(payload_)};
    payload_.clear();
    payload_.reserve(limit_);
    record_count_ = 0U;
    return batch;
  }

private:
  std::uint8_t source_;
  std::uint8_t destination_;
  std::uint32_t limit_;
  std::string payload_;
  std::uint32_t record_count_{0U};
};

}
