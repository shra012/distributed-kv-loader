/// Verifies strict CLI parsing, exit mappings, and safe diagnostics.

#include "core_engine/cli.hpp"
#include "core_engine/error.hpp"

#include "test_framework.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace {

template <std::size_t Size> core_engine::Command parse(const char* const (&arguments)[Size]) {
  return core_engine::parse_cli(static_cast<int>(Size), arguments);
}

template <std::size_t Size> void assert_command_error(const char* const (&arguments)[Size]) {
  try {
    static_cast<void>(parse(arguments));
  } catch (const core_engine::Error& error) {
    CORE_ASSERT_EQ(error.code(), core_engine::ErrorCode::CommandLine);
    CORE_ASSERT_EQ(core_engine::exit_code(error.code()), 2);
    return;
  }
  CORE_ASSERT_TRUE(false);
}

}

CORE_ENGINE_TEST(e2e_17, "E2E-17") {
  static_cast<void>(test_context);
  const char* const help[] = {"core_engine", "--help"};
  const char* const version[] = {"core_engine", "--version"};
  CORE_ASSERT_TRUE(std::holds_alternative<core_engine::HelpOptions>(parse(help)));
  CORE_ASSERT_TRUE(std::holds_alternative<core_engine::VersionOptions>(parse(version)));
  CORE_ASSERT_TRUE(core_engine::usage("core_engine").find("core_engine load --config <path>") !=
                   std::string::npos);
  CORE_ASSERT_EQ(core_engine::version(), std::string("core_engine 0.1.0\n"));

  const char* const load_minimal[] = {"core_engine", "load", "--config", "input.conf"};
  const core_engine::Command load_minimal_command = parse(load_minimal);
  const auto& load_minimal_options = std::get<core_engine::LoadOptions>(load_minimal_command);
  CORE_ASSERT_FALSE(load_minimal_options.manifest.has_value());
  CORE_ASSERT_FALSE(load_minimal_options.report.has_value());

  const char* const load[] = {"core_engine", "load",       "--report",   "result.txt",
                              "--config",    "input.conf", "--manifest", "input.manifest"};
  const core_engine::Command load_command = parse(load);
  const auto& load_options = std::get<core_engine::LoadOptions>(load_command);
  CORE_ASSERT_EQ(load_options.config.string(), std::string("input.conf"));
  CORE_ASSERT_EQ(load_options.report->string(), std::string("result.txt"));
  CORE_ASSERT_EQ(load_options.manifest->string(), std::string("input.manifest"));

  const char* const generate[] = {
      "core_engine",         "generate", "--seed",   "0",         "--bytes-per-file", "104857600",
      "--duplicate-percent", "100",      "--config", "input.conf"};
  const core_engine::Command generate_command = parse(generate);
  const auto& generate_options = std::get<core_engine::GenerateOptions>(generate_command);
  CORE_ASSERT_EQ(generate_options.bytes_per_file, UINT64_C(104857600));
  CORE_ASSERT_EQ(generate_options.seed, UINT64_C(0));
  CORE_ASSERT_EQ(generate_options.duplicate_percent, UINT32_C(100));

  const char* const generate_default[] = {"core_engine",      "generate", "--config", "input.conf",
                                          "--bytes-per-file", "1",        "--seed",   "42"};
  const core_engine::Command generate_default_command = parse(generate_default);
  const auto& generate_default_options =
      std::get<core_engine::GenerateOptions>(generate_default_command);
  CORE_ASSERT_EQ(generate_default_options.duplicate_percent, UINT32_C(5));

  const char* const verify[] = {"core_engine",    "verify",   "--manifest",
                                "input.manifest", "--report", "result.txt"};
  CORE_ASSERT_TRUE(std::holds_alternative<core_engine::VerifyOptions>(parse(verify)));

  const char* const bad_command[] = {"core_engine", "bad"};
  const char* const no_command[] = {"core_engine"};
  const char* const help_extra[] = {"core_engine", "--help", "extra"};
  const char* const missing_flag[] = {"core_engine", "load"};
  const char* const missing_value[] = {"core_engine", "load", "--config"};
  const char* const unknown_flag[] = {"core_engine", "load",  "--config",
                                      "input.conf",  "--bad", "value"};
  const char* const duplicate_flag[] = {"core_engine", "load", "--config", "a", "--config", "b"};
  const char* const overflow[] = {
      "core_engine",          "generate", "--config", "input.conf", "--bytes-per-file",
      "18446744073709551616", "--seed",   "1"};
  const char* const bad_percent[] = {"core_engine",         "generate", "--config", "input.conf",
                                     "--bytes-per-file",    "1",        "--seed",   "1",
                                     "--duplicate-percent", "101"};
  const char* const zero_bytes[] = {"core_engine",      "generate", "--config", "input.conf",
                                    "--bytes-per-file", "0",        "--seed",   "1"};
  const char* const signed_seed[] = {"core_engine",      "generate", "--config", "input.conf",
                                     "--bytes-per-file", "1",        "--seed",   "-1"};
  const char* const incomplete_verify[] = {"core_engine", "verify", "--report", "result.txt"};
  const char* const positional[] = {"core_engine", "load", "input.conf"};
  assert_command_error(bad_command);
  assert_command_error(no_command);
  assert_command_error(help_extra);
  assert_command_error(missing_flag);
  assert_command_error(missing_value);
  assert_command_error(unknown_flag);
  assert_command_error(duplicate_flag);
  assert_command_error(overflow);
  assert_command_error(bad_percent);
  assert_command_error(zero_bytes);
  assert_command_error(signed_seed);
  assert_command_error(incomplete_verify);
  assert_command_error(positional);

  const core_engine::Error error(core_engine::ErrorCode::ConflictingDuplicate, "different\nvalue",
                                 UINT32_C(2), "data/node2.tsv", UINT64_C(41));
  CORE_ASSERT_EQ(core_engine::format_error(error),
                 std::string("ERROR code=CONFLICTING_DUPLICATE node=2 file=data/node2.tsv "
                             "line=41 detail=\"different\\nvalue\""));
  CORE_ASSERT_EQ(core_engine::exit_code(error.code()), 3);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::CommandLine), 2);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::Configuration), 2);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::InputFile), 3);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::Transport), 4);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::Codec), 4);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::Verification), 5);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::ManifestMismatch), 5);
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::InternalInvariant), 6);
}
