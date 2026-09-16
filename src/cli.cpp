/// Implements strict parsing for every public Core Engine command.

#include "core_engine/cli.hpp"

#include "core_engine/decimal.hpp"
#include "core_engine/error.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace core_engine {
namespace {

using OptionMap = std::map<std::string, std::string>;

[[noreturn]] void reject(std::string detail) {
  throw Error(ErrorCode::CommandLine, std::move(detail));
}

bool is_option(std::string_view value) { return value.size() > 2U && value.substr(0U, 2U) == "--"; }

std::string_view argument(const char* const argv[], int index) {
  if (argv[index] == nullptr) {
    reject("null command-line argument");
  }
  return argv[index];
}

OptionMap parse_options(int argc, const char* const argv[], int first) {
  OptionMap options;
  for (int index = first; index < argc; index += 2) {
    const std::string_view name = argument(argv, index);
    if (!is_option(name)) {
      reject("unexpected argument: " + std::string(name));
    }
    if (index + 1 >= argc || is_option(argument(argv, index + 1))) {
      reject("missing value for option: " + std::string(name));
    }
    std::string value = std::string(argument(argv, index + 1));
    if (value.empty()) {
      reject("empty value for option: " + std::string(name));
    }
    const auto inserted = options.emplace(std::string(name), std::move(value));
    if (!inserted.second) {
      reject("duplicate option: " + std::string(name));
    }
  }
  return options;
}

std::string take_required(OptionMap& options, std::string_view name) {
  const auto found = options.find(std::string(name));
  if (found == options.end()) {
    reject("missing required option: " + std::string(name));
  }
  std::string value = std::move(found->second);
  options.erase(found);
  return value;
}

std::optional<std::filesystem::path> take_path(OptionMap& options, std::string_view name) {
  const auto found = options.find(std::string(name));
  if (found == options.end()) {
    return std::nullopt;
  }
  std::filesystem::path path{std::move(found->second)};
  options.erase(found);
  return path;
}

std::uint64_t parse_unsigned(std::string_view value, std::string_view name, bool allow_zero) {
  const std::optional<std::uint64_t> result = parse_decimal(value);
  if (!result.has_value()) {
    reject("invalid unsigned integer for option: " + std::string(name));
  }
  if (!allow_zero && *result == 0U) {
    reject("option must be positive: " + std::string(name));
  }
  return *result;
}

void reject_unknown(const OptionMap& options) {
  if (!options.empty()) {
    reject("unknown option: " + options.cbegin()->first);
  }
}

LoadOptions parse_load(int argc, const char* const argv[]) {
  OptionMap options = parse_options(argc, argv, 2);
  LoadOptions result{take_required(options, "--config"), take_path(options, "--manifest"),
                     take_path(options, "--report")};
  reject_unknown(options);
  return result;
}

GenerateOptions parse_generate(int argc, const char* const argv[]) {
  OptionMap options = parse_options(argc, argv, 2);
  const std::filesystem::path config = take_required(options, "--config");
  const std::uint64_t bytes =
      parse_unsigned(take_required(options, "--bytes-per-file"), "--bytes-per-file", false);
  const std::uint64_t seed = parse_unsigned(take_required(options, "--seed"), "--seed", true);
  std::uint32_t duplicate_percent = 5U;
  const auto duplicate = options.find("--duplicate-percent");
  if (duplicate != options.end()) {
    const std::uint64_t parsed = parse_unsigned(duplicate->second, "--duplicate-percent", true);
    if (parsed > 100U) {
      reject("option exceeds 100: --duplicate-percent");
    }
    duplicate_percent = static_cast<std::uint32_t>(parsed);
    options.erase(duplicate);
  }
  GenerateOptions result{config, bytes, seed, duplicate_percent, take_path(options, "--manifest")};
  reject_unknown(options);
  return result;
}

VerifyOptions parse_verify(int argc, const char* const argv[]) {
  OptionMap options = parse_options(argc, argv, 2);
  VerifyOptions result{take_required(options, "--report"), take_required(options, "--manifest")};
  reject_unknown(options);
  return result;
}

}

Command parse_cli(int argc, const char* const argv[]) {
  if (argc < 2 || argv == nullptr || argv[0] == nullptr) {
    reject("missing command");
  }
  const std::string_view command = argument(argv, 1);
  if (command == "--help" || command == "--version") {
    if (argc != 2) {
      reject("global option does not accept arguments: " + std::string(command));
    }
    return command == "--help" ? Command{HelpOptions{}} : Command{VersionOptions{}};
  }
  if (command == "load") {
    return parse_load(argc, argv);
  }
  if (command == "generate") {
    return parse_generate(argc, argv);
  }
  if (command == "verify") {
    return parse_verify(argc, argv);
  }
  reject("unknown command: " + std::string(command));
}

std::string usage(std::string_view program) {
  const std::string executable = program.empty() ? "core_engine" : std::string(program);
  return "Usage:\n  " + executable +
         " load --config <path> [--manifest <path>] [--report <path>]\n  " + executable +
         " generate --config <path> --bytes-per-file <bytes> --seed <value> "
         "[--duplicate-percent <n>] [--manifest <path>]\n  " +
         executable + " verify --report <path> --manifest <path>\n  " + executable +
         " --help | --version\n";
}

std::string version() { return "core_engine 0.1.0\n"; }

}
