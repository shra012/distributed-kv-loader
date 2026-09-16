/// Defines strict command-line parsing and command option contracts.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace core_engine {

/// Requests a cluster load.
struct LoadOptions final {
  std::filesystem::path config;
  std::optional<std::filesystem::path> manifest;
  std::optional<std::filesystem::path> report;
};

/// Requests deterministic input generation.
struct GenerateOptions final {
  std::filesystem::path config;
  std::uint64_t bytes_per_file;
  std::uint64_t seed;
  std::uint32_t duplicate_percent;
  std::optional<std::filesystem::path> manifest;
};

/// Requests validation of a saved report against a manifest.
struct VerifyOptions final {
  std::filesystem::path report;
  std::filesystem::path manifest;
};

/// Requests help output.
struct HelpOptions final {};

/// Requests version output.
struct VersionOptions final {};

/// Contains exactly one validated command.
using Command =
    std::variant<LoadOptions, GenerateOptions, VerifyOptions, HelpOptions, VersionOptions>;

/// Parses one complete command line or throws Error with CommandLine code.
[[nodiscard]] Command parse_cli(int argc, const char* const argv[]);

/// Returns the complete command usage text for the supplied program name.
[[nodiscard]] std::string usage(std::string_view program);

/// Returns the stable program version line.
[[nodiscard]] std::string version();

}
