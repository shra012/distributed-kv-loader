/// Defines validated cluster configuration loading and memory limits.

#pragma once

#include "core_engine/schema.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace core_engine {

/// Owns a fully validated cluster configuration.
class Config final {
public:
  /// Returns the supported configuration format version.
  [[nodiscard]] std::uint32_t version() const noexcept;

  /// Returns the configured node count in the inclusive range one through five.
  [[nodiscard]] std::uint32_t node_count() const noexcept;

  /// Returns the validated record schema.
  [[nodiscard]] const Schema& schema() const noexcept;

  /// Returns resolved input paths by node, with no value for receiver-only nodes.
  [[nodiscard]] const std::vector<std::optional<std::filesystem::path>>&
  input_paths() const noexcept;

  /// Returns one node's resolved input path and throws std::out_of_range for an invalid node.
  [[nodiscard]] const std::optional<std::filesystem::path>& input_path(std::uint32_t node) const;

  /// Returns the maximum encoded batch size.
  [[nodiscard]] std::uint32_t batch_bytes() const noexcept;

  /// Returns the capacity of each unidirectional channel.
  [[nodiscard]] std::uint32_t channel_bytes() const noexcept;

  /// Returns the number of batches accepted by each inbound queue.
  [[nodiscard]] std::uint32_t inbound_queue_batches() const noexcept;

  /// Returns the maximum accepted raw record size.
  [[nodiscard]] std::uint32_t max_record_bytes() const noexcept;

  /// Returns the size of each input reader buffer.
  [[nodiscard]] std::uint32_t read_buffer_bytes() const noexcept;

  /// Returns the validated upper bound for temporary memory in bytes.
  [[nodiscard]] std::uint64_t temporary_memory_bytes() const noexcept;

private:
  friend Config load_config(const std::filesystem::path& path);

  Config(std::uint32_t version, std::uint32_t node_count, Schema schema,
         std::vector<std::optional<std::filesystem::path>> input_paths, std::uint32_t batch_bytes,
         std::uint32_t channel_bytes, std::uint32_t inbound_queue_batches,
         std::uint32_t max_record_bytes, std::uint32_t read_buffer_bytes,
         std::uint64_t temporary_memory_bytes);

  std::uint32_t version_;
  std::uint32_t node_count_;
  Schema schema_;
  std::vector<std::optional<std::filesystem::path>> input_paths_;
  std::uint32_t batch_bytes_;
  std::uint32_t channel_bytes_;
  std::uint32_t inbound_queue_batches_;
  std::uint32_t max_record_bytes_;
  std::uint32_t read_buffer_bytes_;
  std::uint64_t temporary_memory_bytes_;
};

/// Loads, validates, and resolves a strict key-value configuration file.
[[nodiscard]] Config load_config(const std::filesystem::path& path);

}
