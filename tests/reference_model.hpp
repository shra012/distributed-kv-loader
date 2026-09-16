/// Provides a single-threaded oracle that routes and deduplicates parsed records independently.

#pragma once

#include "core_engine/hash.hpp"
#include "core_engine/packed_record.hpp"
#include "core_engine/parser.hpp"
#include "core_engine/schema.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace core_engine::test {

/// Builds the expected per-node key/value maps for a set of input files using the production
/// parser and hash, applying the same duplicate contract as core_engine::Store, independently of
/// any concurrent node or cluster implementation under test.
class ReferenceModel final {
public:
  /// Creates an empty model for a cluster of node_count nodes sharing the given schema.
  ReferenceModel(const Schema& schema, std::uint32_t node_count)
      : schema_(schema), node_count_(node_count), maps_(node_count), loaded_(node_count, 0U),
        duplicates_(node_count, 0U) {}

  /// Parses every record in path and routes each into its owning node's map, counting identical
  /// duplicates and throwing std::runtime_error on a conflicting duplicate.
  void load_file(const std::filesystem::path& path) {
    parse_file(path, schema_, kMaxRecordBytes, kReadBufferBytes,
               [this](const RecordView& view) { route(view); });
  }

  /// Returns the owning node's key/value map, keyed by canonical key bytes and valued by packed
  /// record bytes.
  [[nodiscard]] const std::unordered_map<std::string, std::string>&
  map_for_node(std::uint32_t node) const {
    return maps_.at(node);
  }

  /// Returns the number of unique records loaded onto node.
  [[nodiscard]] std::uint64_t loaded(std::uint32_t node) const { return loaded_.at(node); }

  /// Returns the number of identical duplicates dropped for node.
  [[nodiscard]] std::uint64_t duplicates(std::uint32_t node) const { return duplicates_.at(node); }

  /// Returns the total number of valid records parsed across every loaded file.
  [[nodiscard]] std::uint64_t total_parsed() const { return total_parsed_; }

  /// Returns the total number of unique records loaded across every node.
  [[nodiscard]] std::uint64_t total_unique() const {
    std::uint64_t total = 0U;
    for (const std::uint64_t count : loaded_) {
      total += count;
    }
    return total;
  }

  /// Returns the total number of identical duplicates dropped across every node.
  [[nodiscard]] std::uint64_t total_duplicates() const {
    std::uint64_t total = 0U;
    for (const std::uint64_t count : duplicates_) {
      total += count;
    }
    return total;
  }

private:
  static constexpr std::uint32_t kMaxRecordBytes = 1U << 20U;
  static constexpr std::uint32_t kReadBufferBytes = 1U << 20U;

  void route(const RecordView& view) {
    ++total_parsed_;
    const std::uint32_t owner = owner_of(view.canonical_key, node_count_);
    std::unordered_map<std::string, std::string>& map = maps_.at(owner);
    const std::string key_bytes(view.canonical_key);
    const std::string value_bytes(view.record);
    const auto found = map.find(key_bytes);
    if (found == map.end()) {
      map.emplace(key_bytes, value_bytes);
      ++loaded_.at(owner);
      return;
    }
    if (found->second == value_bytes) {
      ++duplicates_.at(owner);
      return;
    }
    throw std::runtime_error("reference model conflict for key of size " +
                             std::to_string(key_bytes.size()));
  }

  const Schema& schema_;
  std::uint32_t node_count_;
  std::vector<std::unordered_map<std::string, std::string>> maps_;
  std::vector<std::uint64_t> loaded_;
  std::vector<std::uint64_t> duplicates_;
  std::uint64_t total_parsed_{0U};
};

}
