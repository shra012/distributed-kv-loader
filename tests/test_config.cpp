/// Verifies strict configuration parsing, defaults, paths, and memory limits.

#include "core_engine/config.hpp"
#include "core_engine/error.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path write_config(core_engine::test::TestContext& context, std::string_view name,
                                   std::string_view content) {
  const std::filesystem::path path = context.temporary_directory() / name;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output.is_open()) {
    throw std::runtime_error("cannot create test configuration");
  }
  output << content;
  output.close();
  if (!output.good()) {
    throw std::runtime_error("cannot write test configuration");
  }
  return path;
}

core_engine::ErrorCode load_error(const std::filesystem::path& path) {
  try {
    static_cast<void>(core_engine::load_config(path));
  } catch (const core_engine::Error& error) {
    return error.code();
  }
  throw std::runtime_error("expected configuration failure");
}

std::string minimal_config(std::string_view node_count, std::string_view inputs) {
  return "version=1\nnode_count=" + std::string(node_count) + "\nschema=id:string\nkey_field=id\n" +
         std::string(inputs);
}

std::string sized_config(std::string_view node_count, std::string_view inputs,
                         std::string_view batch, std::string_view channel, std::string_view queue,
                         std::string_view maximum, std::string_view reader) {
  return minimal_config(node_count, inputs) + "batch_bytes=" + std::string(batch) +
         "\nchannel_bytes=" + std::string(channel) +
         "\ninbound_queue_batches=" + std::string(queue) +
         "\nmax_record_bytes=" + std::string(maximum) +
         "\nread_buffer_bytes=" + std::string(reader) + '\n';
}

}

CORE_ENGINE_TEST(config_minimal_defaults, "CFG-01") {
  const auto path = write_config(test_context, "minimal.conf", minimal_config("1", "input.0=-\n"));
  const core_engine::Config config = core_engine::load_config(path);

  CORE_ASSERT_EQ(config.version(), 1U);
  CORE_ASSERT_EQ(config.node_count(), 1U);
  CORE_ASSERT_EQ(config.schema().fields().size(), std::size_t{1U});
  CORE_ASSERT_FALSE(config.input_path(0U).has_value());
  CORE_ASSERT_EQ(config.batch_bytes(), 262144U);
  CORE_ASSERT_EQ(config.channel_bytes(), 1048576U);
  CORE_ASSERT_EQ(config.inbound_queue_batches(), 8U);
  CORE_ASSERT_EQ(config.max_record_bytes(), 65536U);
  CORE_ASSERT_EQ(config.read_buffer_bytes(), 1048576U);
  CORE_ASSERT_EQ(config.temporary_memory_bytes(), UINT64_C(3670016));
}

CORE_ENGINE_TEST(config_full_five_nodes, "CFG-02") {
  const std::string inputs =
      "input.0=a.tsv\ninput.1=b.tsv\ninput.2=-\ninput.3=d.tsv\ninput.4=e.tsv\n";
  const auto path =
      write_config(test_context, "full.conf",
                   sized_config("5", inputs, "131072", "524288", "4", "65536", "262144"));
  const core_engine::Config config = core_engine::load_config(path);

  CORE_ASSERT_EQ(config.node_count(), 5U);
  CORE_ASSERT_EQ(config.input_paths().size(), std::size_t{5U});
  CORE_ASSERT_FALSE(config.input_path(2U).has_value());
  CORE_ASSERT_EQ(config.batch_bytes(), 131072U);
  CORE_ASSERT_EQ(config.channel_bytes(), 524288U);
  CORE_ASSERT_EQ(config.inbound_queue_batches(), 4U);
  CORE_ASSERT_EQ(config.max_record_bytes(), 65536U);
  CORE_ASSERT_EQ(config.read_buffer_bytes(), 262144U);
}

CORE_ENGINE_TEST(config_node_count_range, "CFG-03") {
  const auto zero = write_config(test_context, "zero.conf", minimal_config("0", "input.0=-\n"));
  const auto six = write_config(test_context, "six.conf", minimal_config("6", "input.0=-\n"));

  CORE_ASSERT_EQ(load_error(zero), core_engine::ErrorCode::Configuration);
  CORE_ASSERT_EQ(load_error(six), core_engine::ErrorCode::Configuration);
}

CORE_ENGINE_TEST(config_key_rules_and_comments, "CFG-04") {
  const auto comments = write_config(
      test_context, "comments.conf",
      "# heading\n  # indented\n\nversion=1\nnode_count=1\nschema=id:string\nkey_field=id\n"
      "input.0=-\n");
  CORE_ASSERT_EQ(core_engine::load_config(comments).node_count(), 1U);

  const auto missing = write_config(test_context, "missing.conf",
                                    "version=1\nnode_count=1\nschema=id:string\ninput.0=-\n");
  const auto duplicate = write_config(test_context, "duplicate.conf",
                                      minimal_config("1", "input.0=-\n") + "node_count=1\n");
  const auto unknown = write_config(test_context, "unknown.conf",
                                    minimal_config("1", "input.0=-\n") + "mystery=1\n");
  const auto malformed = write_config(test_context, "malformed.conf",
                                      minimal_config("1", "input.0=-\n") + "batch_bytes=1=2\n");

  CORE_ASSERT_EQ(load_error(missing), core_engine::ErrorCode::Configuration);
  CORE_ASSERT_EQ(load_error(duplicate), core_engine::ErrorCode::Configuration);
  CORE_ASSERT_EQ(load_error(unknown), core_engine::ErrorCode::Configuration);
  CORE_ASSERT_EQ(load_error(malformed), core_engine::ErrorCode::Configuration);
}

CORE_ENGINE_TEST(config_input_ids, "CFG-05") {
  const auto missing =
      write_config(test_context, "missing-input.conf", minimal_config("2", "input.0=-\n"));
  const auto noncontiguous = write_config(test_context, "noncontiguous.conf",
                                          minimal_config("2", "input.0=-\ninput.2=-\n"));
  const auto malformed =
      write_config(test_context, "malformed-input.conf", minimal_config("1", "input.00=-\n"));

  CORE_ASSERT_EQ(load_error(missing), core_engine::ErrorCode::Configuration);
  CORE_ASSERT_EQ(load_error(noncontiguous), core_engine::ErrorCode::Configuration);
  CORE_ASSERT_EQ(load_error(malformed), core_engine::ErrorCode::Configuration);
}

CORE_ENGINE_TEST(config_relative_input_path, "CFG-06") {
  const auto path = write_config(test_context, "nested/settings.conf",
                                 minimal_config("1", "input.0=../data/input.tsv\n"));
  const core_engine::Config config = core_engine::load_config(path);
  const std::filesystem::path expected =
      (test_context.temporary_directory() / "data/input.tsv").lexically_normal();

  CORE_ASSERT_TRUE(config.input_path(0U).has_value());
  CORE_ASSERT_EQ(*config.input_path(0U), expected);
  CORE_ASSERT_TRUE(config.input_path(0U)->is_absolute());
}

CORE_ENGINE_TEST(config_sizes_and_budget, "CFG-07") {
  const std::string one_input = "input.0=-\n";
  const std::string five_inputs = "input.0=-\ninput.1=-\ninput.2=-\ninput.3=-\ninput.4=-\n";
  const std::vector<std::string> invalid{
      sized_config("1", one_input, "0", "1024", "1", "1", "1"),
      sized_config("1", one_input, "1", "0", "1", "1", "1"),
      sized_config("1", one_input, "1", "1", "0", "1", "1"),
      sized_config("1", one_input, "1", "1", "1", "0", "1"),
      sized_config("1", one_input, "1", "1", "1", "1", "0"),
      sized_config("1", one_input, "1", "-1", "1", "1", "1"),
      sized_config("1", one_input, "1", "1", "4294967296", "1", "1"),
      sized_config("1", one_input, "1", "1", "8x", "1", "1"),
      sized_config("1", one_input, "2048", "1024", "1", "1", "1"),
      sized_config("1", one_input, "1024", "1024", "1", "2048", "2048"),
      sized_config("1", one_input, "1024", "1024", "1", "1024", "512"),
      sized_config("1", one_input, "1", "1", "1073741822", "1", "1"),
      sized_config("5", five_inputs, "134217728", "134217728", "8", "134217728", "134217728")};

  const auto boundary =
      write_config(test_context, "boundary.conf",
                   sized_config("1", one_input, "1", "1", "1073741821", "1", "1"));
  CORE_ASSERT_EQ(core_engine::load_config(boundary).temporary_memory_bytes(), UINT64_C(1073741824));

  std::uint32_t index = 0U;
  for (const std::string& content : invalid) {
    const auto path =
        write_config(test_context, "invalid-" + std::to_string(index) + ".conf", content);
    CORE_ASSERT_EQ(load_error(path), core_engine::ErrorCode::Configuration);
    ++index;
  }
}
