/// Verifies deterministic generation, manifest arithmetic, and ownership accounting.

#include "core_engine/config.hpp"
#include "core_engine/error.hpp"
#include "core_engine/generator.hpp"
#include "core_engine/hash.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path write_config(const std::filesystem::path& directory, std::string_view name,
                                   std::string_view content) {
  const std::filesystem::path path = directory / name;
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

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("cannot read generated file");
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string canonical_key_bytes(std::string_view key_text, bool key_is_int32) {
  std::string bytes;
  if (!key_is_int32) {
    bytes.push_back('\x01');
    bytes.append(key_text);
    return bytes;
  }
  bytes.push_back('\x02');
  const std::int32_t value = std::stoi(std::string(key_text));
  const auto unsigned_value = static_cast<std::uint32_t>(value);
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t shift = (3U - index) * 8U;
    bytes.push_back(static_cast<char>((unsigned_value >> shift) & 0xFFU));
  }
  return bytes;
}

std::vector<std::string> split_fields(std::string_view line) {
  std::vector<std::string> fields;
  std::size_t start = 0U;
  while (start <= line.size()) {
    const std::size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.emplace_back(line.substr(start));
      break;
    }
    fields.emplace_back(line.substr(start, tab - start));
    start = tab + 1U;
  }
  return fields;
}

}

CORE_ENGINE_TEST(generator_determinism, "GEN-01") {
  const std::filesystem::path root = test_context.temporary_directory();
  const std::filesystem::path run_a = root / "run-a";
  const std::filesystem::path run_b = root / "run-b";
  std::filesystem::create_directories(run_a);
  std::filesystem::create_directories(run_b);

  constexpr std::string_view config_body = "version=1\nnode_count=2\nschema=id:string,age:int32\n"
                                           "key_field=id\ninput.0=node0.tsv\ninput.1=node1.tsv\n";
  const std::filesystem::path config_a = write_config(run_a, "gen.conf", config_body);
  const std::filesystem::path config_b = write_config(run_b, "gen.conf", config_body);

  const core_engine::Config loaded_a = core_engine::load_config(config_a);
  const core_engine::Config loaded_b = core_engine::load_config(config_b);

  constexpr std::uint64_t seed = 12345U;
  constexpr std::uint64_t bytes_per_file = 2000U;
  constexpr std::uint32_t duplicate_percent = 10U;

  const core_engine::Manifest manifest_a =
      core_engine::generate(loaded_a, seed, bytes_per_file, duplicate_percent);
  const core_engine::Manifest manifest_b =
      core_engine::generate(loaded_b, seed, bytes_per_file, duplicate_percent);

  CORE_ASSERT_EQ(read_file(run_a / "node0.tsv"), read_file(run_b / "node0.tsv"));
  CORE_ASSERT_EQ(read_file(run_a / "node1.tsv"), read_file(run_b / "node1.tsv"));
  CORE_ASSERT_EQ(manifest_a.lines, manifest_b.lines);
  CORE_ASSERT_EQ(manifest_a.unique, manifest_b.unique);
  CORE_ASSERT_EQ(manifest_a.duplicates, manifest_b.duplicates);
  CORE_ASSERT_TRUE(manifest_a.expected_per_node == manifest_b.expected_per_node);
}

CORE_ENGINE_TEST(generator_manifest_arithmetic, "GEN-02") {
  const std::filesystem::path root = test_context.temporary_directory();
  constexpr std::string_view config_body =
      "version=1\nnode_count=3\nschema=id:string,name:string,age:int32\nkey_field=id\n"
      "input.0=node0.tsv\ninput.1=node1.tsv\ninput.2=-\n";
  const std::filesystem::path config_path = write_config(root, "gen.conf", config_body);
  const core_engine::Config config = core_engine::load_config(config_path);

  const core_engine::Manifest manifest = core_engine::generate(config, 777U, 8000U, 20U);

  CORE_ASSERT_EQ(manifest.files, 2U);
  CORE_ASSERT_EQ(manifest.lines, manifest.unique + manifest.duplicates);
  CORE_ASSERT_EQ(manifest.expected_per_node.size(), std::size_t{3});

  std::uint64_t total_expected = 0U;
  for (const std::uint64_t count : manifest.expected_per_node) {
    total_expected += count;
  }
  CORE_ASSERT_EQ(total_expected, manifest.unique);
}

CORE_ENGINE_TEST(generator_ownership_matches_manifest, "GEN-03") {
  const std::filesystem::path root = test_context.temporary_directory();
  constexpr std::string_view config_body = "version=1\nnode_count=3\nschema=id:string,age:int32\n"
                                           "key_field=id\ninput.0=node0.tsv\ninput.1=node1.tsv\n"
                                           "input.2=node2.tsv\n";
  const std::filesystem::path config_path = write_config(root, "gen.conf", config_body);
  const core_engine::Config config = core_engine::load_config(config_path);

  const core_engine::Manifest manifest = core_engine::generate(config, 4242U, 6000U, 15U);

  std::vector<std::uint64_t> observed(config.node_count(), 0U);
  std::set<std::string> seen_keys;
  for (std::uint32_t node = 0U; node < config.node_count(); ++node) {
    const std::filesystem::path path = *config.input_path(node);
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty()) {
        continue;
      }
      const std::vector<std::string> fields = split_fields(line);
      const std::string canonical = canonical_key_bytes(fields[config.schema().key_index()], false);
      if (!seen_keys.insert(canonical).second) {
        continue;
      }
      const std::uint32_t owner = core_engine::owner_of(canonical, config.node_count());
      ++observed[owner];
    }
  }

  CORE_ASSERT_TRUE(observed == manifest.expected_per_node);
  CORE_ASSERT_EQ(seen_keys.size(), manifest.unique);
}

CORE_ENGINE_TEST(generator_manifest_round_trip, "GEN-04") {
  const std::filesystem::path root = test_context.temporary_directory();
  const core_engine::Manifest original{1U, 99U, 12345U, 7U, 3U, 40U, 35U, 5U, {10U, 12U, 13U}};
  const std::filesystem::path manifest_path = root / "round_trip.manifest";
  core_engine::write_manifest(original, manifest_path);
  const core_engine::Manifest parsed = core_engine::read_manifest(manifest_path);

  CORE_ASSERT_EQ(parsed.version, original.version);
  CORE_ASSERT_EQ(parsed.seed, original.seed);
  CORE_ASSERT_EQ(parsed.bytes_per_file, original.bytes_per_file);
  CORE_ASSERT_EQ(parsed.duplicate_percent, original.duplicate_percent);
  CORE_ASSERT_EQ(parsed.files, original.files);
  CORE_ASSERT_EQ(parsed.lines, original.lines);
  CORE_ASSERT_EQ(parsed.unique, original.unique);
  CORE_ASSERT_EQ(parsed.duplicates, original.duplicates);
  CORE_ASSERT_TRUE(parsed.expected_per_node == original.expected_per_node);

  const std::filesystem::path missing = root / "missing.manifest";
  bool threw = false;
  try {
    static_cast<void>(core_engine::read_manifest(missing));
  } catch (const core_engine::Error& error) {
    threw = true;
    CORE_ASSERT_EQ(error.code(), core_engine::ErrorCode::InputFile);
  }
  CORE_ASSERT_TRUE(threw);
}

CORE_ENGINE_TEST(generator_manifest_rejects_invalid, "GEN-05") {
  const std::filesystem::path root = test_context.temporary_directory();
  const std::vector<std::string> invalid{
      "version=1\nversion=1\nseed=1\nbytes_per_file=1\nduplicate_percent=0\nfiles=1\n"
      "lines=1\nunique=1\nduplicates=0\nexpected.0=1\n",
      "version=1\nseed=18446744073709551616\nbytes_per_file=1\nduplicate_percent=0\n"
      "files=1\nlines=1\nunique=1\nduplicates=0\nexpected.0=1\n",
      "version=1\nseed=1\nbytes_per_file=1\nduplicate_percent=0\nfiles=1\nlines=2\n"
      "unique=1\nduplicates=0\nexpected.0=1\n",
      "version=1\nseed=1\nbytes_per_file=1\nduplicate_percent=0\nfiles=1\nlines=1\n"
      "unique=1\nduplicates=0\nexpected.0=0\n"};
  for (std::size_t index = 0U; index < invalid.size(); ++index) {
    const std::filesystem::path path = root / ("invalid-" + std::to_string(index));
    std::ofstream output(path);
    output << invalid[index];
    output.close();
    bool rejected = false;
    try {
      static_cast<void>(core_engine::read_manifest(path));
    } catch (const core_engine::Error& error) {
      rejected = error.code() == core_engine::ErrorCode::InputFile;
    }
    CORE_ASSERT_TRUE(rejected);
  }
}

CORE_ENGINE_TEST(generator_int32_keys_are_unique, "GEN-06") {
  const std::filesystem::path root = test_context.temporary_directory();
  const std::filesystem::path config_path = write_config(
      root, "int.conf",
      "version=1\nnode_count=1\nschema=id:int32,value:string\nkey_field=id\ninput.0=data.tsv\n");
  const core_engine::Config config = core_engine::load_config(config_path);
  const core_engine::Manifest manifest = core_engine::generate(config, 99U, 1000000U, 0U);
  std::ifstream input(*config.input_path(0U));
  std::set<std::string> keys;
  std::string line;
  while (std::getline(input, line)) {
    keys.insert(line.substr(0U, line.find('\t')));
  }
  CORE_ASSERT_EQ(keys.size(), static_cast<std::size_t>(manifest.lines));
  CORE_ASSERT_EQ(manifest.lines, manifest.unique);
  CORE_ASSERT_EQ(manifest.duplicates, std::uint64_t{0U});
}
