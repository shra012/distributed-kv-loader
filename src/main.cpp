/// Core Engine command-line entry point.

#include "core_engine/cli.hpp"
#include "core_engine/cluster.hpp"
#include "core_engine/error.hpp"
#include "core_engine/generator.hpp"
#include "core_engine/report.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace {

std::uint64_t total_input_bytes(const core_engine::Config& config) {
  std::uint64_t total = 0U;
  for (const auto& path : config.input_paths()) {
    if (path.has_value()) {
      std::error_code file_error;
      const std::uintmax_t size = std::filesystem::file_size(*path, file_error);
      if (!file_error) {
        total += static_cast<std::uint64_t>(size);
      }
    }
  }
  return total;
}

int run_load(const core_engine::LoadOptions& options) {
  const core_engine::Config config = core_engine::load_config(options.config);
  std::optional<core_engine::Manifest> manifest;
  if (options.manifest.has_value()) {
    manifest = core_engine::read_manifest(*options.manifest);
  }
  const std::uint64_t input_bytes_total = total_input_bytes(config);

  const auto started_at = std::chrono::steady_clock::now();
  core_engine::Cluster cluster(config);
  cluster.reserve_stores(manifest.has_value()
                             ? manifest->expected_per_node
                             : cluster.estimate_records_per_node(input_bytes_total),
                         input_bytes_total);
  cluster.run();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at);

  if (cluster.first_error().has_value()) {
    std::cerr << core_engine::format_error(*cluster.first_error()) << '\n';
    return core_engine::exit_code(cluster.first_error()->code());
  }

  const std::vector<core_engine::OwnershipVerification> scans =
      core_engine::scan_ownership(cluster, config);
  const core_engine::Invariants invariants =
      core_engine::check_invariants(config, cluster.stats(), scans);
  if (!invariants.all_pass()) {
    const core_engine::Error error(core_engine::ErrorCode::Verification,
                                   "post-join invariant check failed");
    std::cerr << core_engine::format_error(error) << '\n';
    return core_engine::exit_code(error.code());
  }

  if (manifest.has_value() &&
      !core_engine::report_matches_manifest(config, *manifest, cluster.stats())) {
    const core_engine::Error error(core_engine::ErrorCode::ManifestMismatch,
                                   "loaded data does not match manifest");
    std::cerr << core_engine::format_error(error) << '\n';
    return core_engine::exit_code(error.code());
  }

  const std::string report_text = core_engine::format_report(
      config, cluster.stats(), scans, invariants, manifest, elapsed, input_bytes_total);
  if (options.report.has_value()) {
    std::ofstream output(*options.report);
    if (!output.is_open()) {
      throw core_engine::Error(core_engine::ErrorCode::InputFile,
                               "cannot open report file for writing", std::nullopt,
                               options.report->string());
    }
    output << report_text;
    if (output.fail()) {
      throw core_engine::Error(core_engine::ErrorCode::InputFile,
                               "failed while writing report file", std::nullopt,
                               options.report->string());
    }
  }
  std::cout << report_text;
  return 0;
}

int run_generate(const core_engine::GenerateOptions& options) {
  const core_engine::Config config = core_engine::load_config(options.config);
  const core_engine::Manifest manifest = core_engine::generate(
      config, options.seed, options.bytes_per_file, options.duplicate_percent);
  if (options.manifest.has_value()) {
    core_engine::write_manifest(manifest, *options.manifest);
  }
  std::cout << "GENERATE files=" << manifest.files << " lines=" << manifest.lines
            << " unique=" << manifest.unique << " duplicates=" << manifest.duplicates << '\n';
  return 0;
}

int run_verify(const core_engine::VerifyOptions& options) {
  const core_engine::Manifest manifest = core_engine::read_manifest(options.manifest);
  core_engine::verify_report(options.report, manifest);
  return 0;
}

}

int main(int argc, char* argv[]) {
  const std::string_view program = argc > 0 && argv[0] != nullptr ? argv[0] : "core_engine";
  try {
    const core_engine::Command command = core_engine::parse_cli(argc, argv);
    if (std::holds_alternative<core_engine::HelpOptions>(command)) {
      std::cout << core_engine::usage(program);
      return 0;
    }
    if (std::holds_alternative<core_engine::VersionOptions>(command)) {
      std::cout << core_engine::version();
      return 0;
    }
    if (const auto* load = std::get_if<core_engine::LoadOptions>(&command)) {
      return run_load(*load);
    }
    if (const auto* generate = std::get_if<core_engine::GenerateOptions>(&command)) {
      return run_generate(*generate);
    }
    return run_verify(std::get<core_engine::VerifyOptions>(command));
  } catch (const core_engine::Error& error) {
    std::cerr << core_engine::format_error(error) << '\n';
    if (error.code() == core_engine::ErrorCode::CommandLine) {
      std::cerr << core_engine::usage(program);
    }
    return core_engine::exit_code(error.code());
  } catch (const std::exception&) {
    const core_engine::Error error(core_engine::ErrorCode::InternalInvariant,
                                   "unhandled internal exception");
    std::cerr << core_engine::format_error(error) << '\n';
    return core_engine::exit_code(error.code());
  } catch (...) {
    const core_engine::Error error(core_engine::ErrorCode::InternalInvariant,
                                   "unhandled non-standard exception");
    std::cerr << core_engine::format_error(error) << '\n';
    return core_engine::exit_code(error.code());
  }
}
