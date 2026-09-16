/// Verifies end-to-end cluster behavior against the reference model and documented failure paths.

#include "core_engine/cluster.hpp"
#include "core_engine/error.hpp"
#include "core_engine/report.hpp"
#include "reference_model.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path write_file(core_engine::test::TestContext& context, std::string_view name,
                                 std::string_view content) {
  const std::filesystem::path path = context.temporary_directory() / name;
  std::ofstream output(path);
  if (!output.is_open()) {
    throw std::runtime_error("cannot create test fixture file");
  }
  output << content;
  output.close();
  if (!output.good()) {
    throw std::runtime_error("cannot write test fixture file");
  }
  return path;
}

std::string input_line(std::uint32_t node, const std::string& value) {
  return "input." + std::to_string(node) + "=" + value + "\n";
}

std::filesystem::path
write_cluster_config(core_engine::test::TestContext& context, std::uint32_t node_count,
                     const std::vector<std::optional<std::string>>& per_node_lines,
                     std::string_view schema = "id:string,value:int32",
                     std::string_view key_field = "id", std::string_view extra_keys = "") {
  std::ostringstream inputs;
  std::vector<std::filesystem::path> paths;
  for (std::uint32_t node = 0U; node < node_count; ++node) {
    const std::optional<std::string>& body = per_node_lines.at(node);
    if (!body.has_value()) {
      inputs << input_line(node, "-");
      continue;
    }
    const std::filesystem::path path =
        write_file(context, "node" + std::to_string(node) + ".tsv", *body);
    inputs << input_line(node, path.string());
  }
  std::ostringstream config;
  config << "version=1\nnode_count=" << node_count << "\nschema=" << schema
         << "\nkey_field=" << key_field << '\n'
         << inputs.str() << extra_keys;
  return write_file(context, "cluster.conf", config.str());
}

void run_success(core_engine::Cluster& cluster) {
  cluster.run();
  if (cluster.first_error().has_value()) {
    throw std::runtime_error("cluster.run() failed: " +
                             core_engine::format_error(*cluster.first_error()));
  }
}

void assert_matches_reference_model(const core_engine::Cluster& cluster,
                                    const core_engine::Config& config,
                                    const core_engine::test::ReferenceModel& model) {
  for (std::uint32_t node = 0U; node < config.node_count(); ++node) {
    const core_engine::Store& store = cluster.store(node);
    const auto& expected_map = model.map_for_node(node);
    CORE_ASSERT_EQ(store.size(), expected_map.size());
    for (const auto& [key_bytes, value_bytes] : expected_map) {
      const std::optional<std::string_view> found = store.find(key_bytes);
      CORE_ASSERT_TRUE(found.has_value());
      if (found.has_value()) {
        CORE_ASSERT_EQ(std::string(*found), value_bytes);
      }
    }
    const core_engine::NodeStats stats = cluster.stats().at(node);
    CORE_ASSERT_EQ(stats.loaded, model.loaded(node));
    CORE_ASSERT_EQ(stats.duplicates, model.duplicates(node));
  }
}

core_engine::test::ReferenceModel
build_reference(const core_engine::Config& config,
                const std::vector<std::optional<std::filesystem::path>>& paths) {
  core_engine::test::ReferenceModel model(config.schema(), config.node_count());
  for (const auto& path : paths) {
    if (path.has_value()) {
      model.load_file(*path);
    }
  }
  return model;
}

std::uint64_t sum_counter(const std::vector<core_engine::NodeStats>& stats,
                          const std::uint64_t core_engine::NodeStats::* field) {
  std::uint64_t total = 0U;
  for (const core_engine::NodeStats& entry : stats) {
    total += entry.*field;
  }
  return total;
}

}

CORE_ENGINE_TEST(cluster_e2e_01, "E2E-01") {
  const std::filesystem::path config_path =
      write_cluster_config(test_context, 1U, {std::string("k0\t1\nk1\t2\nk2\t3\n")});
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::Cluster cluster(config);
  run_success(cluster);
  const core_engine::NodeStats stats = cluster.stats().at(0U);
  CORE_ASSERT_EQ(stats.parsed, std::uint64_t{3U});
  CORE_ASSERT_EQ(stats.local, std::uint64_t{3U});
  CORE_ASSERT_EQ(stats.remote_out, std::uint64_t{0U});
  CORE_ASSERT_EQ(stats.remote_in, std::uint64_t{0U});
  CORE_ASSERT_EQ(cluster.store(0U).size(), std::size_t{3U});
}

CORE_ENGINE_TEST(cluster_e2e_02, "E2E-02") {
  const std::vector<std::optional<std::string>> bodies{std::string("alpha\t1\nbravo\t2\n"),
                                                       std::string("charlie\t3\ndelta\t4\n")};
  const std::filesystem::path config_path = write_cluster_config(test_context, 2U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::test::ReferenceModel model = build_reference(config, config.input_paths());
  core_engine::Cluster cluster(config);
  run_success(cluster);
  assert_matches_reference_model(cluster, config, model);
  CORE_ASSERT_EQ(model.total_parsed(), std::uint64_t{4U});
  CORE_ASSERT_TRUE(cluster.stats().at(0U).remote_out > 0U ||
                   cluster.stats().at(1U).remote_out > 0U);
}

CORE_ENGINE_TEST(cluster_e2e_03, "E2E-03") {
  std::vector<std::optional<std::string>> bodies;
  for (std::uint32_t node = 0U; node < 5U; ++node) {
    std::ostringstream body;
    for (std::uint32_t index = 0U; index < 6U; ++index) {
      body << "n" << node << "k" << index << '\t' << (node * 100U + index) << '\n';
    }
    bodies.push_back(body.str());
  }
  const std::filesystem::path config_path = write_cluster_config(test_context, 5U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::test::ReferenceModel model = build_reference(config, config.input_paths());
  core_engine::Cluster cluster(config);
  run_success(cluster);
  assert_matches_reference_model(cluster, config, model);
  CORE_ASSERT_EQ(model.total_parsed(), std::uint64_t{30U});

  std::uint64_t sum_remote_out = 0U;
  std::uint64_t sum_remote_in = 0U;
  std::uint64_t sum_frames_out = 0U;
  std::uint64_t sum_frames_in = 0U;
  std::uint64_t sum_bytes_out = 0U;
  std::uint64_t sum_bytes_in = 0U;
  std::uint64_t sum_parsed = 0U;
  std::uint64_t sum_ingested = 0U;
  for (const core_engine::NodeStats& stats : cluster.stats()) {
    CORE_ASSERT_EQ(stats.parsed, stats.local + stats.remote_out);
    CORE_ASSERT_EQ(stats.ingested, stats.loaded + stats.duplicates);
    sum_remote_out += stats.remote_out;
    sum_remote_in += stats.remote_in;
    sum_frames_out += stats.frames_out;
    sum_frames_in += stats.frames_in;
    sum_bytes_out += stats.bytes_out;
    sum_bytes_in += stats.bytes_in;
    sum_parsed += stats.parsed;
    sum_ingested += stats.ingested;
  }
  CORE_ASSERT_EQ(sum_remote_out, sum_remote_in);
  CORE_ASSERT_EQ(sum_frames_out, sum_frames_in);
  CORE_ASSERT_EQ(sum_bytes_out, sum_bytes_in);
  CORE_ASSERT_EQ(sum_parsed, sum_ingested);
}

CORE_ENGINE_TEST(cluster_e2e_04, "E2E-04") {
  const std::vector<std::optional<std::string>> bodies{std::string("only\t1\nsource\t2\nnode\t3\n"),
                                                       std::nullopt, std::nullopt};
  const std::filesystem::path config_path = write_cluster_config(test_context, 3U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::test::ReferenceModel model = build_reference(config, config.input_paths());
  core_engine::Cluster cluster(config);
  run_success(cluster);
  assert_matches_reference_model(cluster, config, model);
  CORE_ASSERT_EQ(cluster.stats().at(1U).parsed, std::uint64_t{0U});
  CORE_ASSERT_EQ(cluster.stats().at(2U).parsed, std::uint64_t{0U});
  const std::uint64_t receiver_records =
      cluster.stats().at(1U).loaded + cluster.stats().at(2U).loaded;
  CORE_ASSERT_TRUE(receiver_records > 0U);
}

CORE_ENGINE_TEST(cluster_e2e_05, "E2E-05") {
  const std::vector<std::optional<std::string>> bodies{std::string(""), std::nullopt};
  const std::filesystem::path config_path = write_cluster_config(test_context, 2U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::Cluster cluster(config);
  run_success(cluster);
  CORE_ASSERT_EQ(cluster.stats().at(0U).parsed, std::uint64_t{0U});
  CORE_ASSERT_EQ(cluster.stats().at(1U).parsed, std::uint64_t{0U});
  CORE_ASSERT_EQ(cluster.store(0U).size(), std::size_t{0U});
  CORE_ASSERT_EQ(cluster.store(1U).size(), std::size_t{0U});
}

CORE_ENGINE_TEST(cluster_e2e_06, "E2E-06") {
  const std::vector<std::optional<std::string>> bodies{std::string("shared\t7\nonly_a\t1\n"),
                                                       std::string("shared\t7\nonly_b\t2\n")};
  const std::filesystem::path config_path = write_cluster_config(test_context, 2U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::test::ReferenceModel model = build_reference(config, config.input_paths());
  core_engine::Cluster cluster(config);
  run_success(cluster);
  assert_matches_reference_model(cluster, config, model);
  CORE_ASSERT_EQ(model.total_duplicates(), std::uint64_t{1U});
  CORE_ASSERT_EQ(model.total_unique(), std::uint64_t{3U});
}

CORE_ENGINE_TEST(cluster_e2e_07, "E2E-07") {
  const std::vector<std::optional<std::string>> bodies{std::string("same\t1\n"),
                                                       std::string("same\t999\n")};
  const std::filesystem::path config_path = write_cluster_config(test_context, 2U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::Cluster cluster(config);
  cluster.run();
  CORE_ASSERT_TRUE(cluster.first_error().has_value());
  if (cluster.first_error().has_value()) {
    CORE_ASSERT_EQ(cluster.first_error()->code(), core_engine::ErrorCode::ConflictingDuplicate);
  }
  CORE_ASSERT_EQ(sum_counter(cluster.stats(), &core_engine::NodeStats::conflict),
                 std::uint64_t{1U});
}

CORE_ENGINE_TEST(cluster_e2e_08, "E2E-08") {
  const std::filesystem::path missing = test_context.temporary_directory() / "missing.tsv";
  std::ostringstream config;
  config << "version=1\nnode_count=2\nschema=id:string,value:int32\nkey_field=id\n"
         << input_line(0U, missing.string()) << input_line(1U, "-");
  const std::filesystem::path config_path = write_file(test_context, "e2e08.conf", config.str());
  const core_engine::Config config_value = core_engine::load_config(config_path);
  core_engine::Cluster cluster(config_value);
  cluster.run();
  CORE_ASSERT_TRUE(cluster.first_error().has_value());
  if (cluster.first_error().has_value()) {
    CORE_ASSERT_EQ(cluster.first_error()->code(), core_engine::ErrorCode::InputFile);
  }
}

CORE_ENGINE_TEST(cluster_e2e_09, "E2E-09") {
  const std::vector<std::optional<std::string>> bodies{std::string("good\t1\nbad_missing_field\n"),
                                                       std::string("other\t2\n")};
  const std::filesystem::path config_path = write_cluster_config(test_context, 2U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::Cluster cluster(config);
  cluster.run();
  CORE_ASSERT_TRUE(cluster.first_error().has_value());
  if (cluster.first_error().has_value()) {
    CORE_ASSERT_EQ(cluster.first_error()->code(), core_engine::ErrorCode::MalformedRecord);
    CORE_ASSERT_TRUE(cluster.first_error()->line().has_value());
    if (cluster.first_error()->line().has_value()) {
      CORE_ASSERT_EQ(*cluster.first_error()->line(), std::uint64_t{2U});
    }
  }
  CORE_ASSERT_EQ(sum_counter(cluster.stats(), &core_engine::NodeStats::malformed),
                 std::uint64_t{1U});
}

CORE_ENGINE_TEST(cluster_e2e_10, "E2E-10") {
  std::ostringstream body_a;
  std::ostringstream body_b;
  for (std::uint32_t index = 0U; index < 20U; ++index) {
    body_a << "aa" << index << '\t' << index << '\n';
    body_b << "bb" << index << '\t' << index << '\n';
  }
  const std::vector<std::optional<std::string>> bodies{body_a.str(), body_b.str()};
  std::ostringstream sizing;
  sizing << "batch_bytes=16\nchannel_bytes=64\ninbound_queue_batches=2\nmax_record_bytes=16\n"
            "read_buffer_bytes=256\n";
  const std::filesystem::path config_path =
      write_cluster_config(test_context, 2U, bodies, "id:string,value:int32", "id", sizing.str());
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::test::ReferenceModel model = build_reference(config, config.input_paths());
  core_engine::Cluster cluster(config);
  run_success(cluster);
  assert_matches_reference_model(cluster, config, model);
  CORE_ASSERT_EQ(model.total_parsed(), std::uint64_t{40U});
}

CORE_ENGINE_TEST(cluster_e2e_11, "E2E-11") {
  std::ostringstream body_a;
  std::ostringstream body_b;
  for (std::uint32_t index = 0U; index < 12U; ++index) {
    body_a << "x" << index << '\t' << index << '\n';
    body_b << "y" << index << '\t' << index << '\n';
  }
  const std::vector<std::optional<std::string>> bodies{body_a.str(), body_b.str()};
  std::ostringstream sizing;
  sizing << "batch_bytes=12\nchannel_bytes=12\ninbound_queue_batches=1\nmax_record_bytes=12\n"
            "read_buffer_bytes=64\n";
  const std::filesystem::path config_path =
      write_cluster_config(test_context, 2U, bodies, "id:string,value:int32", "id", sizing.str());
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::test::ReferenceModel model = build_reference(config, config.input_paths());
  core_engine::Cluster cluster(config);
  run_success(cluster);
  assert_matches_reference_model(cluster, config, model);
  CORE_ASSERT_EQ(model.total_parsed(), std::uint64_t{24U});
}

CORE_ENGINE_TEST(cluster_e2e_15, "E2E-15") {
  const std::vector<std::optional<std::string>> bodies{std::string("1\talpha\n2\tbravo\n"),
                                                       std::string("3\tcharlie\n-4\tdelta\n")};
  const std::filesystem::path config_path =
      write_cluster_config(test_context, 2U, bodies, "id:int32,name:string", "id");
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::test::ReferenceModel model = build_reference(config, config.input_paths());
  core_engine::Cluster cluster(config);
  run_success(cluster);
  assert_matches_reference_model(cluster, config, model);
  CORE_ASSERT_EQ(model.total_parsed(), std::uint64_t{4U});
  CORE_ASSERT_EQ(model.total_unique(), std::uint64_t{4U});
}

CORE_ENGINE_TEST(cluster_transport_faults, "NET-07-CLUSTER") {
  const std::vector<std::optional<std::string>> bodies{
      std::string("alpha\t1\nbravo\t2\ncharlie\t3\ndelta\t4\n"), std::nullopt, std::nullopt};
  const std::filesystem::path config_path = write_cluster_config(test_context, 3U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  const std::vector<std::pair<core_engine::FaultEndpoint, core_engine::NetworkFault>> faults{
      {core_engine::FaultEndpoint::Sender, core_engine::NetworkFault::FailSend},
      {core_engine::FaultEndpoint::Receiver, core_engine::NetworkFault::FailRecv},
      {core_engine::FaultEndpoint::Sender, core_engine::NetworkFault::CorruptSend},
      {core_engine::FaultEndpoint::Sender, core_engine::NetworkFault::DropConnection}};
  for (const auto& [endpoint, fault] : faults) {
    const core_engine::ClusterOptions options{
        core_engine::TransportFaultInjection{0U, 1U, endpoint, fault, 1U}, std::nullopt};
    core_engine::Cluster cluster(config, options);
    cluster.run();
    CORE_ASSERT_TRUE(cluster.first_error().has_value());
    if (cluster.first_error().has_value()) {
      const core_engine::ErrorCode code = cluster.first_error()->code();
      CORE_ASSERT_TRUE(code == core_engine::ErrorCode::Transport ||
                       code == core_engine::ErrorCode::Codec);
      CORE_ASSERT_EQ(core_engine::exit_code(code), 4);
    }
    CORE_ASSERT_TRUE(sum_counter(cluster.stats(), &core_engine::NodeStats::transport) +
                         sum_counter(cluster.stats(), &core_engine::NodeStats::codec) >
                     0U);
  }
}

CORE_ENGINE_TEST(cluster_e2e_12, "E2E-12") {
  const std::vector<std::optional<std::string>> bodies{
      std::string("alpha\t1\nbravo\t2\nshared\t7\n"),
      std::string("charlie\t3\ndelta\t4\nshared\t7\n"), std::nullopt};
  const std::filesystem::path config_path = write_cluster_config(test_context, 3U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  std::string expected;
  for (std::uint32_t iteration = 0U; iteration < 20U; ++iteration) {
    core_engine::Cluster cluster(config);
    run_success(cluster);
    const std::vector<core_engine::OwnershipVerification> scans =
        core_engine::scan_ownership(cluster, config);
    const core_engine::Invariants invariants =
        core_engine::check_invariants(config, cluster.stats(), scans);
    CORE_ASSERT_TRUE(invariants.all_pass());
    std::string report =
        core_engine::format_report(config, cluster.stats(), scans, invariants, std::nullopt,
                                   std::chrono::milliseconds{iteration + 1U}, 100U);
    const std::size_t timing = report.find("TIMING ");
    CORE_ASSERT_NE(timing, std::string::npos);
    report.erase(timing);
    if (iteration == 0U) {
      expected = report;
    } else {
      CORE_ASSERT_EQ(report, expected);
    }
  }
}

CORE_ENGINE_TEST(cluster_e2e_13, "E2E-13") {
  const std::vector<std::optional<std::string>> bodies{
      std::string("alpha\t1\nbravo\t2\ncharlie\t3\n"), std::nullopt, std::nullopt};
  const std::filesystem::path config_path = write_cluster_config(test_context, 3U, bodies);
  const core_engine::Config config = core_engine::load_config(config_path);
  core_engine::Cluster cluster(config, core_engine::ClusterOptions{std::nullopt, 0U});
  run_success(cluster);
  const std::vector<core_engine::OwnershipVerification> scans =
      core_engine::scan_ownership(cluster, config);
  const core_engine::Invariants invariants =
      core_engine::check_invariants(config, cluster.stats(), scans);
  CORE_ASSERT_FALSE(invariants.ownership);
  CORE_ASSERT_FALSE(invariants.all_pass());
  CORE_ASSERT_EQ(core_engine::exit_code(core_engine::ErrorCode::Verification), 5);
}
