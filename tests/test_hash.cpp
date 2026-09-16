/// Verifies stable hashing, ownership vectors, distribution, and minimal movement.

#include "core_engine/hash.hpp"
#include "core_engine/packed_record.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t sample_count = 100000U;
constexpr std::uint64_t sample_seed = UINT64_C(0x6a09e667f3bcc909);

struct FnvVector final {
  std::string_view input;
  std::uint64_t expected;
};

struct JumpVector final {
  std::uint64_t hash;
  std::array<std::uint32_t, 5> expected;
};

std::string canonical_key(std::uint64_t value) {
  std::string key(9U, '\0');
  key[0] = '\x01';
  for (std::size_t index = 0; index < 8U; ++index) {
    const std::size_t shift = (7U - index) * 8U;
    key[index + 1U] = static_cast<char>((value >> shift) & UINT64_C(0xff));
  }
  return key;
}

}

CORE_ENGINE_TEST(hash_02, "HSH-02") {
  static_cast<void>(test_context);
  const std::array<FnvVector, 8> vectors{{
      {std::string_view{}, UINT64_C(0xcbf29ce484222325)},
      {std::string_view{"\0", 1U}, UINT64_C(0xaf63bd4c8601b7df)},
      {"a", UINT64_C(0xaf63dc4c8601ec8c)},
      {"b", UINT64_C(0xaf63df4c8601f1a5)},
      {"foo", UINT64_C(0xdcb27518fed9d577)},
      {"foobar", UINT64_C(0x85944171f73967e8)},
      {"chongo was here!\n", UINT64_C(0x46810940eff5f915)},
      {std::string_view{"\xff\0\x7f", 3U}, UINT64_C(0xf920b41be415ae2f)},
  }};

  for (const FnvVector& vector : vectors) {
    CORE_ASSERT_EQ(core_engine::fnv1a_64(vector.input), vector.expected);
  }
}

CORE_ENGINE_TEST(hash_03, "HSH-03") {
  static_cast<void>(test_context);
  const std::array<JumpVector, 8> vectors{{
      {UINT64_C(0x0000000000000000), {0U, 0U, 0U, 0U, 0U}},
      {UINT64_C(0x0000000000000002), {0U, 0U, 0U, 3U, 3U}},
      {UINT64_C(0x0000000000000003), {0U, 0U, 2U, 3U, 3U}},
      {UINT64_C(0x0000000000000004), {0U, 1U, 1U, 1U, 1U}},
      {UINT64_C(0x0000000000000005), {0U, 1U, 1U, 1U, 4U}},
      {UINT64_C(0x000000000000000a), {0U, 1U, 2U, 2U, 2U}},
      {UINT64_C(0xdeadbeefcafebabe), {0U, 1U, 1U, 1U, 4U}},
      {UINT64_C(0x8000000000000000), {0U, 1U, 1U, 3U, 4U}},
  }};

  for (const JumpVector& vector : vectors) {
    for (std::uint32_t bucket_count = 1U; bucket_count <= 5U; ++bucket_count) {
      CORE_ASSERT_EQ(core_engine::jump_consistent_hash(vector.hash, bucket_count),
                     vector.expected[bucket_count - 1U]);
    }
  }

  CORE_ASSERT_EQ(core_engine::owner_of(std::string_view{"\001alpha", 6U}, 5U), 2U);
  CORE_ASSERT_EQ(core_engine::owner_of(std::string_view{"\001beta", 5U}, 5U), 3U);
  CORE_ASSERT_EQ(core_engine::owner_of(std::string_view{"\x02\0\0\0*", 5U}, 5U), 2U);
  CORE_ASSERT_THROW(static_cast<void>(core_engine::jump_consistent_hash(0U, 0U)),
                    std::invalid_argument);
  CORE_ASSERT_THROW(static_cast<void>(core_engine::jump_consistent_hash(0U, 6U)),
                    std::invalid_argument);
  CORE_ASSERT_THROW(static_cast<void>(core_engine::owner_of("key", 0U)), std::invalid_argument);
  CORE_ASSERT_THROW(static_cast<void>(core_engine::owner_of("key", 6U)), std::invalid_argument);
}

CORE_ENGINE_TEST(hash_05, "HSH-05") {
  static_cast<void>(test_context);
  std::mt19937_64 generator(sample_seed);
  std::array<std::array<std::size_t, 5>, 4> counts{};

  for (std::size_t index = 0; index < sample_count; ++index) {
    const std::string key = canonical_key(generator());
    for (std::uint32_t node_count = 2U; node_count <= 5U; ++node_count) {
      const std::uint32_t owner = core_engine::owner_of(key, node_count);
      ++counts[node_count - 2U][owner];
    }
  }

  for (std::uint32_t node_count = 2U; node_count <= 5U; ++node_count) {
    const double mean = static_cast<double>(sample_count) / static_cast<double>(node_count);
    const double tolerance = mean * 0.025;
    for (std::uint32_t node = 0U; node < node_count; ++node) {
      const double deviation = std::abs(static_cast<double>(counts[node_count - 2U][node]) - mean);
      CORE_ASSERT_TRUE(deviation <= tolerance);
    }
  }
}

CORE_ENGINE_TEST(hash_06, "HSH-06") {
  static_cast<void>(test_context);
  std::mt19937_64 generator(sample_seed);
  std::array<std::size_t, 4> moved{};

  for (std::size_t index = 0; index < sample_count; ++index) {
    const std::string key = canonical_key(generator());
    for (std::uint32_t old_count = 1U; old_count < 5U; ++old_count) {
      const std::uint32_t old_owner = core_engine::owner_of(key, old_count);
      const std::uint32_t new_owner = core_engine::owner_of(key, old_count + 1U);
      if (old_owner != new_owner) {
        CORE_ASSERT_EQ(new_owner, old_count);
        ++moved[old_count - 1U];
      }
    }
  }

  for (std::uint32_t old_count = 1U; old_count < 5U; ++old_count) {
    const double expected = 1.0 / static_cast<double>(old_count + 1U);
    const double fraction =
        static_cast<double>(moved[old_count - 1U]) / static_cast<double>(sample_count);
    CORE_ASSERT_TRUE(fraction >= expected * 0.9);
    CORE_ASSERT_TRUE(fraction <= expected * 1.1);
  }
}

CORE_ENGINE_TEST(hash_01, "HSH-01") {
  static_cast<void>(test_context);

  {
    const core_engine::Schema schema = core_engine::Schema::parse("name:string", "name");
    const std::vector<core_engine::FieldValue> values{std::string("alpha")};
    const core_engine::PackedRecord record = core_engine::pack_record(schema, values);
    const core_engine::CanonicalKey key = core_engine::build_canonical_key(schema, record);

    std::string expected;
    expected.push_back('\x01');
    expected += "alpha";
    CORE_ASSERT_EQ(std::string(key.bytes()), expected);
  }

  {
    const core_engine::Schema schema = core_engine::Schema::parse("id:int32", "id");
    const std::vector<core_engine::FieldValue> values{std::int32_t{100}};
    const core_engine::PackedRecord record = core_engine::pack_record(schema, values);
    const core_engine::CanonicalKey key = core_engine::build_canonical_key(schema, record);

    std::string expected;
    expected.push_back('\x02');
    expected.push_back(static_cast<char>(0x00));
    expected.push_back(static_cast<char>(0x00));
    expected.push_back(static_cast<char>(0x00));
    expected.push_back(static_cast<char>(0x64));
    CORE_ASSERT_EQ(std::string(key.bytes()), expected);
  }

  {
    const core_engine::Schema schema = core_engine::Schema::parse("name:string,id:int32", "id");
    const std::vector<core_engine::FieldValue> values{std::string("zzz"), std::int32_t{-5}};
    const core_engine::PackedRecord record = core_engine::pack_record(schema, values);
    const core_engine::CanonicalKey key = core_engine::build_canonical_key(schema, record);

    std::string expected;
    expected.push_back('\x02');
    expected.push_back(static_cast<char>(0xff));
    expected.push_back(static_cast<char>(0xff));
    expected.push_back(static_cast<char>(0xff));
    expected.push_back(static_cast<char>(0xfb));
    CORE_ASSERT_EQ(std::string(key.bytes()), expected);
  }
}

CORE_ENGINE_TEST(hash_04, "HSH-04") {
  static_cast<void>(test_context);

  const core_engine::Schema string_schema = core_engine::Schema::parse("k:string", "k");
  const std::vector<core_engine::FieldValue> string_values{std::string("42")};
  const core_engine::PackedRecord string_record =
      core_engine::pack_record(string_schema, string_values);
  const core_engine::CanonicalKey string_key =
      core_engine::build_canonical_key(string_schema, string_record);

  const core_engine::Schema int_schema = core_engine::Schema::parse("k:int32", "k");
  const std::vector<core_engine::FieldValue> int_values{std::int32_t{42}};
  const core_engine::PackedRecord int_record = core_engine::pack_record(int_schema, int_values);
  const core_engine::CanonicalKey int_key =
      core_engine::build_canonical_key(int_schema, int_record);

  std::string expected_string_key;
  expected_string_key.push_back('\x01');
  expected_string_key += "42";
  CORE_ASSERT_EQ(std::string(string_key.bytes()), expected_string_key);

  std::string expected_int_key;
  expected_int_key.push_back('\x02');
  expected_int_key.push_back(static_cast<char>(0x00));
  expected_int_key.push_back(static_cast<char>(0x00));
  expected_int_key.push_back(static_cast<char>(0x00));
  expected_int_key.push_back(static_cast<char>(0x2a));
  CORE_ASSERT_EQ(std::string(int_key.bytes()), expected_int_key);

  CORE_ASSERT_TRUE(!(string_key == int_key));
}
