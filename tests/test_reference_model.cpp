/// Verifies the reference model's independent routing and duplicate-dropping oracle.

#include "reference_model.hpp"

#include "core_engine/hash.hpp"
#include "core_engine/packed_record.hpp"
#include "core_engine/schema.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path fixture_path(std::string_view name) {
  return std::filesystem::path(__FILE__).parent_path() / "fixtures" / name;
}

core_engine::CanonicalKey key_for(const core_engine::Schema& schema, const std::string& id,
                                  const std::string& name, std::int32_t age) {
  const std::vector<core_engine::FieldValue> values{id, name, age};
  const core_engine::PackedRecord record = core_engine::pack_record(schema, values);
  return core_engine::build_canonical_key(schema, record);
}

std::string value_for(const core_engine::Schema& schema, const std::string& id,
                      const std::string& name, std::int32_t age) {
  const std::vector<core_engine::FieldValue> values{id, name, age};
  return std::string(core_engine::pack_record(schema, values).bytes());
}

}

CORE_ENGINE_TEST(reference_model_01, "REF-01") {
  static_cast<void>(test_context);
  constexpr std::uint32_t node_count = 3U;
  const core_engine::Schema schema =
      core_engine::Schema::parse("id:string,name:string,age:int32", "id");

  core_engine::test::ReferenceModel model(schema, node_count);
  model.load_file(fixture_path("three_node_a.tsv"));
  model.load_file(fixture_path("three_node_b.tsv"));

  CORE_ASSERT_EQ(model.total_parsed(), std::uint64_t{10U});
  CORE_ASSERT_EQ(model.total_unique(), std::uint64_t{8U});
  CORE_ASSERT_EQ(model.total_duplicates(), std::uint64_t{2U});

  struct Record final {
    std::string id;
    std::string name;
    std::int32_t age;
  };
  const std::vector<Record> unique_records{
      {"k1", "Alice", 30}, {"k2", "Bob", 25},   {"k3", "Carol", 40}, {"k4", "Dave", 22},
      {"k5", "Eve", 35},   {"k6", "Frank", 28}, {"k7", "Grace", 31}, {"k8", "Heidi", 45},
  };
  const std::vector<std::string> duplicate_ids{"k1", "k2"};

  std::vector<std::uint64_t> expected_loaded(node_count, 0U);
  std::vector<std::uint64_t> expected_duplicates(node_count, 0U);
  for (const Record& record : unique_records) {
    const core_engine::CanonicalKey key = key_for(schema, record.id, record.name, record.age);
    const std::uint32_t owner = core_engine::owner_of(key.bytes(), node_count);
    ++expected_loaded.at(owner);
  }
  for (const std::string& id : duplicate_ids) {
    const Record* matched = nullptr;
    for (const Record& record : unique_records) {
      if (record.id == id) {
        matched = &record;
        break;
      }
    }
    CORE_ASSERT_TRUE(matched != nullptr);
    const core_engine::CanonicalKey key = key_for(schema, matched->id, matched->name, matched->age);
    const std::uint32_t owner = core_engine::owner_of(key.bytes(), node_count);
    ++expected_duplicates.at(owner);
  }

  for (std::uint32_t node = 0U; node < node_count; ++node) {
    CORE_ASSERT_EQ(model.loaded(node), expected_loaded.at(node));
    CORE_ASSERT_EQ(model.duplicates(node), expected_duplicates.at(node));
  }

  for (const Record& record : unique_records) {
    const core_engine::CanonicalKey key = key_for(schema, record.id, record.name, record.age);
    const std::uint32_t owner = core_engine::owner_of(key.bytes(), node_count);
    const std::string key_bytes(key.bytes());
    const auto& node_map = model.map_for_node(owner);
    const auto found = node_map.find(key_bytes);
    CORE_ASSERT_TRUE(found != node_map.end());
    CORE_ASSERT_EQ(found->second, value_for(schema, record.id, record.name, record.age));
  }
}
