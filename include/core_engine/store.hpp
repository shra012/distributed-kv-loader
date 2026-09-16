/// Declares the single-writer per-node record store and its ownership scan.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace core_engine {

/// Maps canonical keys to packed records with an open-addressing table over an append-only arena.
/// Single writer: find and scan_ownership are valid only while no insert or reserve runs.
class Store final {
public:
  /// Reports the outcome of a full ownership scan.
  struct OwnershipScanResult final {
    std::size_t scanned;
    std::size_t misowned;
  };

  /// Creates an empty store for node_id within a cluster of node_count nodes.
  Store(std::uint32_t node_id, std::uint32_t node_count) noexcept;

  /// Pre-sizes the table for at least hint entries, capped at 50,000,000.
  void reserve(std::size_t hint);

  /// Copies record in under canonical_key. Counts a byte-identical duplicate, or throws
  /// Error(ConflictingDuplicate) with node (default: this store), file, and line when the key
  /// already maps to different bytes.
  void insert(std::string_view canonical_key, std::string_view record,
              std::optional<std::uint32_t> node = std::nullopt, std::string_view file = {},
              std::optional<std::uint64_t> line = std::nullopt);

  /// Returns the number of distinct keys stored.
  [[nodiscard]] std::size_t size() const noexcept;

  /// Returns the number of identical duplicates dropped.
  [[nodiscard]] std::size_t duplicate_count() const noexcept;

  /// Returns the stored record bytes for canonical_key, or nullopt when absent.
  [[nodiscard]] std::optional<std::string_view> find(std::string_view canonical_key) const;

  /// Recomputes each stored key's owner and counts keys that do not belong to this node.
  [[nodiscard]] OwnershipScanResult scan_ownership() const;

private:
  struct Slot final {
    std::uint64_t hash;
    const char* data;
    std::uint32_t key_length;
    std::uint32_t record_length;
  };

  [[nodiscard]] std::size_t home(std::uint64_t hash) const noexcept;
  [[nodiscard]] std::size_t probe(std::string_view canonical_key,
                                  std::uint64_t hash) const noexcept;
  void grow(std::size_t minimum_capacity);
  [[nodiscard]] const char* copy_to_arena(std::string_view canonical_key, std::string_view record);

  std::uint32_t node_id_;
  std::uint32_t node_count_;
  std::size_t size_{0U};
  std::size_t duplicate_count_{0U};
  unsigned shift_{64U};
  std::vector<Slot> slots_;
  std::vector<std::unique_ptr<char[]>> arena_blocks_;
  char* arena_cursor_{nullptr};
  std::size_t arena_remaining_{0U};
};

}
