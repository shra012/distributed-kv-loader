/// Declares deterministic input generation and its manifest.

#pragma once

#include "core_engine/config.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace core_engine {

/// Records the consistency-checked outcome of one deterministic generation run.
struct Manifest final {
  std::uint32_t version;
  std::uint64_t seed;
  std::uint64_t bytes_per_file;
  std::uint32_t duplicate_percent;
  std::uint32_t files;
  std::uint64_t lines;
  std::uint64_t unique;
  std::uint64_t duplicates;
  std::vector<std::uint64_t> expected_per_node;
};

/// Writes deterministic per-node TSV input files for every non-receiver-only node and returns
/// the resulting manifest; throws Error with InputFile on any write failure.
[[nodiscard]] Manifest generate(const Config& config, std::uint64_t seed,
                                std::uint64_t bytes_per_file, std::uint32_t duplicate_percent);

/// Writes a manifest in the documented key=value grammar; throws Error with InputFile on failure.
void write_manifest(const Manifest& manifest, const std::filesystem::path& path);

/// Reads and strictly parses a manifest previously written by write_manifest. Throws Error with
/// InputFile if the file cannot be opened or does not match the documented key=value grammar.
[[nodiscard]] Manifest read_manifest(const std::filesystem::path& path);

}
