/// Verifies invariant checking against deliberately broken snapshots and the report grammar.

#include "core_engine/error.hpp"
#include "core_engine/report.hpp"
#include "test_framework.hpp"

#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path write_config(core_engine::test::TestContext& context, std::string_view name,
                                   std::string_view content) {
  const std::filesystem::path path = context.temporary_directory() / name;
  std::ofstream output(path);
  if (!output.is_open()) {
    throw std::runtime_error("cannot create test configuration");
  }
  output << content;
  output.close();
  return path;
}

core_engine::NodeStats make_stats(std::uint64_t parsed, std::uint64_t local,
                                  std::uint64_t remote_out, std::uint64_t remote_in,
                                  std::uint64_t ingested, std::uint64_t loaded,
                                  std::uint64_t duplicates, std::uint64_t frames_out,
                                  std::uint64_t frames_in, std::uint64_t bytes_out,
                                  std::uint64_t bytes_in, std::uint64_t end_markers_received) {
  return core_engine::NodeStats{parsed,
                                parsed,
                                local,
                                remote_out,
                                frames_out,
                                bytes_out,
                                remote_in,
                                ingested,
                                loaded,
                                duplicates,
                                frames_in,
                                bytes_in,
                                end_markers_received,
                                0U,
                                0U,
                                0U,
                                0U};
}

core_engine::Config two_node_config(core_engine::test::TestContext& context) {
  const std::filesystem::path path = write_config(
      context, "report.conf",
      "version=1\nnode_count=2\nschema=id:string\nkey_field=id\ninput.0=-\ninput.1=-\n");
  return core_engine::load_config(path);
}

}

CORE_ENGINE_TEST(report_invariants_pass, "RPT-01") {
  const core_engine::Config config = two_node_config(test_context);
  const std::vector<core_engine::NodeStats> stats{
      make_stats(10U, 6U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 2U),
      make_stats(10U, 7U, 3U, 4U, 11U, 10U, 1U, 2U, 2U, 90U, 100U, 2U)};
  const std::vector<core_engine::OwnershipVerification> scans{
      core_engine::OwnershipVerification{0U, 8U, 0U},
      core_engine::OwnershipVerification{1U, 10U, 0U}};
  const core_engine::Invariants invariants = core_engine::check_invariants(config, stats, scans);
  CORE_ASSERT_TRUE(invariants.ownership);
  CORE_ASSERT_TRUE(invariants.uniqueness);
  CORE_ASSERT_TRUE(invariants.accounting);
  CORE_ASSERT_TRUE(invariants.completion);
  CORE_ASSERT_TRUE(invariants.all_pass());
}

CORE_ENGINE_TEST(report_invariants_ownership_fails, "RPT-02") {
  const core_engine::Config config = two_node_config(test_context);
  const std::vector<core_engine::NodeStats> stats{
      make_stats(10U, 6U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 2U),
      make_stats(10U, 7U, 3U, 4U, 10U, 9U, 1U, 2U, 2U, 90U, 100U, 2U)};
  const std::vector<core_engine::OwnershipVerification> scans{
      core_engine::OwnershipVerification{0U, 8U, 1U},
      core_engine::OwnershipVerification{1U, 9U, 0U}};
  const core_engine::Invariants invariants = core_engine::check_invariants(config, stats, scans);
  CORE_ASSERT_FALSE(invariants.ownership);
  CORE_ASSERT_FALSE(invariants.uniqueness);
  CORE_ASSERT_FALSE(invariants.all_pass());
}

CORE_ENGINE_TEST(report_invariants_accounting_fails, "RPT-03") {
  const core_engine::Config config = two_node_config(test_context);
  const std::vector<core_engine::NodeStats> broken_local{
      make_stats(10U, 5U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 2U),
      make_stats(10U, 7U, 3U, 4U, 10U, 9U, 1U, 2U, 2U, 90U, 100U, 2U)};
  const std::vector<core_engine::OwnershipVerification> scans{
      core_engine::OwnershipVerification{0U, 8U, 0U},
      core_engine::OwnershipVerification{1U, 9U, 0U}};
  CORE_ASSERT_FALSE(core_engine::check_invariants(config, broken_local, scans).accounting);

  const std::vector<core_engine::NodeStats> broken_symmetry{
      make_stats(10U, 6U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 2U),
      make_stats(10U, 7U, 3U, 999U, 10U, 9U, 1U, 2U, 2U, 90U, 100U, 2U)};
  CORE_ASSERT_FALSE(core_engine::check_invariants(config, broken_symmetry, scans).accounting);
}

CORE_ENGINE_TEST(report_invariants_completion_fails, "RPT-04") {
  const core_engine::Config config = two_node_config(test_context);
  const std::vector<core_engine::NodeStats> stats{
      make_stats(10U, 6U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 1U),
      make_stats(10U, 7U, 3U, 4U, 10U, 9U, 1U, 2U, 2U, 90U, 100U, 2U)};
  const std::vector<core_engine::OwnershipVerification> scans{
      core_engine::OwnershipVerification{0U, 8U, 0U},
      core_engine::OwnershipVerification{1U, 9U, 0U}};
  CORE_ASSERT_FALSE(core_engine::check_invariants(config, stats, scans).completion);
}

CORE_ENGINE_TEST(report_format_grammar, "RPT-05") {
  const core_engine::Config config = two_node_config(test_context);
  const std::vector<core_engine::NodeStats> stats{
      make_stats(10U, 6U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 2U),
      make_stats(10U, 7U, 3U, 4U, 11U, 10U, 1U, 2U, 2U, 90U, 100U, 2U)};
  const std::vector<core_engine::OwnershipVerification> scans{
      core_engine::OwnershipVerification{0U, 8U, 0U},
      core_engine::OwnershipVerification{1U, 10U, 0U}};
  const core_engine::Invariants invariants = core_engine::check_invariants(config, stats, scans);
  const std::string report = core_engine::format_report(
      config, stats, scans, invariants, std::nullopt, std::chrono::milliseconds{500}, 1000000U);

  CORE_ASSERT_TRUE(report.find("JOB status=SUCCESS version=1 nodes=2 files=0 lines=20 "
                               "parsed=20 unique=18 duplicates=2\n") != std::string::npos);
  CORE_ASSERT_TRUE(
      report.find("NODE id=0 parsed=10 local=6 remote_out=4 remote_in=3 ingested=9 loaded=8 "
                  "duplicates=1 frames_out=2 frames_in=2 bytes_out=100 bytes_in=90\n") !=
      std::string::npos);
  CORE_ASSERT_TRUE(report.find("DIST mean=9.00 min=8 max=10 max_deviation_percent=11.11\n") !=
                   std::string::npos);
  CORE_ASSERT_TRUE(report.find("VERIFY node=0 scanned=8 misowned=0 result=PASS\n") !=
                   std::string::npos);
  CORE_ASSERT_TRUE(report.find("VERIFY ownership=PASS uniqueness=PASS accounting=PASS "
                               "completion=PASS\n") != std::string::npos);
  CORE_ASSERT_TRUE(report.find("EXPECT") == std::string::npos);
  CORE_ASSERT_TRUE(report.find("TIMING elapsed_ms=500 records_per_s=40 input_mb_per_s=2.0\n") !=
                   std::string::npos);
}

CORE_ENGINE_TEST(report_expect_line, "RPT-06") {
  const core_engine::Config config = two_node_config(test_context);
  const std::vector<core_engine::NodeStats> stats{
      make_stats(10U, 6U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 2U),
      make_stats(10U, 7U, 3U, 4U, 11U, 10U, 1U, 2U, 2U, 90U, 100U, 2U)};
  const std::vector<core_engine::OwnershipVerification> scans{
      core_engine::OwnershipVerification{0U, 8U, 0U},
      core_engine::OwnershipVerification{1U, 10U, 0U}};
  const core_engine::Invariants invariants = core_engine::check_invariants(config, stats, scans);

  const core_engine::Manifest matching{1U, 42U, 0U, 5U, 0U, 20U, 18U, 2U, {8U, 10U}};
  const std::string matching_report = core_engine::format_report(
      config, stats, scans, invariants, matching, std::chrono::milliseconds{100}, 0U);
  CORE_ASSERT_TRUE(matching_report.find("EXPECT manifest=PASS unique=18 duplicates=2\n") !=
                   std::string::npos);

  const core_engine::Manifest mismatched{1U, 42U, 0U, 5U, 0U, 20U, 999U, 2U, {8U, 10U}};
  const std::string mismatched_report = core_engine::format_report(
      config, stats, scans, invariants, mismatched, std::chrono::milliseconds{100}, 0U);
  CORE_ASSERT_TRUE(mismatched_report.find("EXPECT manifest=FAIL unique=18 duplicates=2\n") !=
                   std::string::npos);
}

CORE_ENGINE_TEST(report_saved_verification, "RPT-07") {
  const core_engine::Config config = two_node_config(test_context);
  const std::vector<core_engine::NodeStats> stats{
      make_stats(10U, 6U, 4U, 3U, 9U, 8U, 1U, 2U, 2U, 100U, 90U, 2U),
      make_stats(10U, 7U, 3U, 4U, 11U, 10U, 1U, 2U, 2U, 90U, 100U, 2U)};
  const std::vector<core_engine::OwnershipVerification> scans{
      core_engine::OwnershipVerification{0U, 8U, 0U},
      core_engine::OwnershipVerification{1U, 10U, 0U}};
  const core_engine::Invariants invariants = core_engine::check_invariants(config, stats, scans);
  const core_engine::Manifest manifest{1U, 42U, 0U, 5U, 0U, 20U, 18U, 2U, {8U, 10U}};
  const std::string report = core_engine::format_report(config, stats, scans, invariants, manifest,
                                                        std::chrono::milliseconds{100}, 0U);
  const std::filesystem::path valid_path = write_config(test_context, "valid.report", report);
  core_engine::verify_report(valid_path, manifest);

  const std::filesystem::path invalid_path =
      write_config(test_context, "invalid.report", report.substr(0U, report.find("TIMING ")));
  bool malformed_rejected = false;
  try {
    core_engine::verify_report(invalid_path, manifest);
  } catch (const core_engine::Error& error) {
    malformed_rejected = error.code() == core_engine::ErrorCode::Verification;
  }
  CORE_ASSERT_TRUE(malformed_rejected);

  const core_engine::Manifest mismatch{1U, 42U, 0U, 5U, 0U, 20U, 18U, 2U, {9U, 9U}};
  bool mismatch_rejected = false;
  try {
    core_engine::verify_report(valid_path, mismatch);
  } catch (const core_engine::Error& error) {
    mismatch_rejected = error.code() == core_engine::ErrorCode::ManifestMismatch;
  }
  CORE_ASSERT_TRUE(mismatch_rejected);
}
