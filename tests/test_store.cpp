/// Verifies duplicate handling, conflict detection, and ownership scanning for the store.

#include "core_engine/batch.hpp"
#include "core_engine/codec.hpp"
#include "core_engine/error.hpp"
#include "core_engine/hash.hpp"
#include "core_engine/packed_record.hpp"
#include "core_engine/schema.hpp"
#include "core_engine/store.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

core_engine::CanonicalKey string_key(const core_engine::Schema& schema, const std::string& value) {
  const std::vector<core_engine::FieldValue> values{value};
  const core_engine::PackedRecord record = core_engine::pack_record(schema, values);
  return core_engine::build_canonical_key(schema, record);
}

core_engine::PackedRecord string_record(const core_engine::Schema& schema,
                                        const std::string& value) {
  const std::vector<core_engine::FieldValue> values{value};
  return core_engine::pack_record(schema, values);
}

}

CORE_ENGINE_TEST(store_01, "STO-01") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string", "id");
  core_engine::Store store(0U, 1U);

  store.insert(string_key(schema, "alpha"), string_record(schema, "alpha").bytes());
  store.insert(string_key(schema, "beta"), string_record(schema, "beta").bytes());
  store.insert(string_key(schema, "gamma"), string_record(schema, "gamma").bytes());

  CORE_ASSERT_EQ(store.size(), std::size_t{3U});
  CORE_ASSERT_EQ(store.duplicate_count(), std::size_t{0U});
  CORE_ASSERT_TRUE(store.find(string_key(schema, "alpha")).has_value());
  CORE_ASSERT_FALSE(store.find(string_key(schema, "missing")).has_value());
}

CORE_ENGINE_TEST(store_02, "STO-02") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string", "id");
  core_engine::Store store(0U, 1U);

  store.insert(string_key(schema, "alpha"), string_record(schema, "alpha").bytes());
  store.insert(string_key(schema, "alpha"), string_record(schema, "alpha").bytes());

  CORE_ASSERT_EQ(store.size(), std::size_t{1U});
  CORE_ASSERT_EQ(store.duplicate_count(), std::size_t{1U});
  const std::optional<std::string_view> found = store.find(string_key(schema, "alpha"));
  CORE_ASSERT_TRUE(found.has_value());
  CORE_ASSERT_EQ(std::string(*found), std::string(string_record(schema, "alpha").bytes()));
}

CORE_ENGINE_TEST(store_03, "STO-03") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string", "id");
  core_engine::Store store(0U, 1U);

  store.insert(string_key(schema, "alpha"), string_record(schema, "alpha").bytes(), 0U,
               "file_a.tsv", 1U);
  store.insert(string_key(schema, "alpha"), string_record(schema, "alpha").bytes(), 2U,
               "file_b.tsv", 7U);

  CORE_ASSERT_EQ(store.size(), std::size_t{1U});
  CORE_ASSERT_EQ(store.duplicate_count(), std::size_t{1U});
}

CORE_ENGINE_TEST(store_04, "STO-04") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,value:string", "id");
  core_engine::Store store(0U, 1U);

  const std::vector<core_engine::FieldValue> first_values{std::string("alpha"), std::string("one")};
  const core_engine::PackedRecord first_record = core_engine::pack_record(schema, first_values);
  const core_engine::CanonicalKey first_key =
      core_engine::build_canonical_key(schema, first_record);
  store.insert(first_key, first_record.bytes());

  const std::vector<core_engine::FieldValue> second_values{std::string("alpha"),
                                                           std::string("two")};
  const core_engine::PackedRecord second_record = core_engine::pack_record(schema, second_values);
  const core_engine::CanonicalKey second_key =
      core_engine::build_canonical_key(schema, second_record);

  bool threw = false;
  try {
    store.insert(second_key, second_record.bytes(), 3U, "conflict.tsv", 9U);
  } catch (const core_engine::Error& error) {
    threw = true;
    CORE_ASSERT_TRUE(error.code() == core_engine::ErrorCode::ConflictingDuplicate);
    CORE_ASSERT_EQ(error.file(), std::string("conflict.tsv"));
  }
  CORE_ASSERT_TRUE(threw);

  CORE_ASSERT_EQ(store.size(), std::size_t{1U});
  const std::optional<std::string_view> found = store.find(first_key);
  CORE_ASSERT_TRUE(found.has_value());
  CORE_ASSERT_EQ(std::string(*found), std::string(first_record.bytes()));
}

CORE_ENGINE_TEST(store_05, "STO-05") {
  static_cast<void>(test_context);
  const core_engine::Schema int_schema = core_engine::Schema::parse("id:int32", "id");
  const core_engine::Schema string_schema = core_engine::Schema::parse("id:string", "id");
  core_engine::Store store(0U, 1U);

  const std::vector<core_engine::FieldValue> int_values{std::int32_t{0}};
  const core_engine::PackedRecord int_record = core_engine::pack_record(int_schema, int_values);
  const core_engine::CanonicalKey int_key =
      core_engine::build_canonical_key(int_schema, int_record);
  CORE_ASSERT_EQ(int_key.bytes().size(), std::size_t{5U});

  const std::string zero_bytes(4U, '\0');
  const std::vector<core_engine::FieldValue> string_values{zero_bytes};
  const core_engine::PackedRecord string_record_value =
      core_engine::pack_record(string_schema, string_values);
  const core_engine::CanonicalKey string_key_value =
      core_engine::build_canonical_key(string_schema, string_record_value);

  CORE_ASSERT_TRUE(!(int_key == string_key_value));

  store.insert(int_key, int_record.bytes());
  store.insert(string_key_value, string_record_value.bytes());

  CORE_ASSERT_EQ(store.size(), std::size_t{2U});
}

CORE_ENGINE_TEST(store_06, "STO-06") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string", "id");
  core_engine::Store store(1U, 3U);

  CORE_ASSERT_EQ(store.size(), std::size_t{0U});
  CORE_ASSERT_EQ(store.duplicate_count(), std::size_t{0U});
  const core_engine::Store::OwnershipScanResult empty_scan = store.scan_ownership();
  CORE_ASSERT_EQ(empty_scan.scanned, std::size_t{0U});
  CORE_ASSERT_EQ(empty_scan.misowned, std::size_t{0U});
  CORE_ASSERT_FALSE(store.find(string_key(schema, "anything")).has_value());

  const std::string large_value(8U * 1024U * 1024U, 'x');
  const core_engine::CanonicalKey key = string_key(schema, "large");
  const core_engine::PackedRecord record = string_record(schema, large_value);
  store.insert(key, record.bytes());

  for (std::uint32_t index = 0U; index < 5000U; ++index) {
    const std::string value = "grow-" + std::to_string(index);
    store.insert(string_key(schema, value), string_record(schema, value).bytes());
  }

  const std::optional<std::string_view> found = store.find(key);
  CORE_ASSERT_TRUE(found.has_value());
  CORE_ASSERT_EQ(std::string(*found), std::string(record.bytes()));
  CORE_ASSERT_EQ(store.size(), std::size_t{5001U});
  const std::optional<std::string_view> grown = store.find(string_key(schema, "grow-4999"));
  CORE_ASSERT_TRUE(grown.has_value());
}

CORE_ENGINE_TEST(store_07, "STO-07") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string", "id");
  constexpr std::uint32_t node_id = 0U;
  constexpr std::uint32_t node_count = 3U;
  core_engine::Store store(node_id, node_count);

  std::size_t owned_inserted = 0U;
  for (std::uint32_t candidate = 0U; owned_inserted < 3U; ++candidate) {
    const std::string value = "owned-" + std::to_string(candidate);
    const core_engine::CanonicalKey key = string_key(schema, value);
    if (core_engine::owner_of(key.bytes(), node_count) == node_id) {
      store.insert(key, string_record(schema, value).bytes());
      ++owned_inserted;
    }
  }

  bool injected = false;
  for (std::uint32_t candidate = 0U; !injected; ++candidate) {
    const std::string value = "foreign-" + std::to_string(candidate);
    const core_engine::CanonicalKey key = string_key(schema, value);
    if (core_engine::owner_of(key.bytes(), node_count) != node_id) {
      store.insert(key, string_record(schema, value).bytes());
      injected = true;
    }
  }

  const core_engine::Store::OwnershipScanResult scan = store.scan_ownership();
  CORE_ASSERT_EQ(scan.scanned, std::size_t{4U});
  CORE_ASSERT_EQ(scan.misowned, std::size_t{1U});
}

CORE_ENGINE_TEST(codec_08, "COD-08") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,age:int32", "id");
  const std::vector<core_engine::FieldValue> first_values{std::string("alpha"), std::int32_t{7}};
  const std::vector<core_engine::FieldValue> second_values{std::string("beta"), std::int32_t{-3}};
  const core_engine::PackedRecord local_first = core_engine::pack_record(schema, first_values);
  const core_engine::PackedRecord local_second = core_engine::pack_record(schema, second_values);
  const core_engine::CanonicalKey key_first = core_engine::build_canonical_key(schema, local_first);
  const core_engine::CanonicalKey key_second =
      core_engine::build_canonical_key(schema, local_second);

  core_engine::BatchBuilder builder(1U, 0U, 4096U);
  builder.append(local_first.bytes());
  builder.append(local_second.bytes());
  const core_engine::Batch batch = builder.take(1U);
  const std::string frame = core_engine::encode_frame(batch, core_engine::FrameType::Data);
  const core_engine::DecodedFrame decoded = core_engine::decode_frame(frame);

  std::vector<std::string_view> remote_records;
  std::string_view payload = decoded.payload;
  while (!payload.empty()) {
    const core_engine::RecordLayout layout = core_engine::scan_record(schema, payload);
    remote_records.push_back(payload.substr(0U, layout.length));
    payload.remove_prefix(layout.length);
  }
  CORE_ASSERT_EQ(remote_records.size(), std::size_t{decoded.record_count});

  core_engine::Store store(0U, 2U);
  store.insert(key_first.bytes(), remote_records[0]);
  store.insert(key_second.bytes(), remote_records[1]);

  const std::optional<std::string_view> stored_first = store.find(key_first);
  const std::optional<std::string_view> stored_second = store.find(key_second);
  CORE_ASSERT_TRUE(stored_first.has_value());
  CORE_ASSERT_TRUE(stored_second.has_value());
  CORE_ASSERT_EQ(std::string(*stored_first), std::string(local_first.bytes()));
  CORE_ASSERT_EQ(std::string(*stored_second), std::string(local_second.bytes()));
}
