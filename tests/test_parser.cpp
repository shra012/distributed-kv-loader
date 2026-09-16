/// Verifies record packing byte layout and the streaming TSV parser's data-format contract.

#include "core_engine/error.hpp"
#include "core_engine/packed_record.hpp"
#include "core_engine/parser.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

CORE_ENGINE_TEST(parser_01, "PAR-01") {
  static_cast<void>(test_context);
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,age:int32", "id");

  const std::vector<core_engine::FieldValue> values{std::string("hello"), std::int32_t{42}};
  const core_engine::PackedRecord record = core_engine::pack_record(schema, values);

  std::string expected;
  expected.push_back(static_cast<char>(0x00));
  expected.push_back(static_cast<char>(0x00));
  expected.push_back(static_cast<char>(0x00));
  expected.push_back(static_cast<char>(0x05));
  expected += "hello";
  expected.push_back(static_cast<char>(0x00));
  expected.push_back(static_cast<char>(0x00));
  expected.push_back(static_cast<char>(0x00));
  expected.push_back(static_cast<char>(0x2a));

  CORE_ASSERT_EQ(std::string(record.bytes()), expected);
}

namespace {

std::filesystem::path write_input(core_engine::test::TestContext& context, std::string_view name,
                                  std::string_view content) {
  const std::filesystem::path path = context.temporary_directory() / name;
  std::ofstream output(path, std::ios::binary);
  if (!output.is_open()) {
    throw std::runtime_error("cannot create test input file");
  }
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.close();
  if (!output.good()) {
    throw std::runtime_error("cannot write test input file");
  }
  return path;
}

struct OwnedRecord final {
  core_engine::PackedRecord record;
  core_engine::CanonicalKey key;
  std::uint64_t line;
};

std::vector<OwnedRecord> run_parse(const std::filesystem::path& path,
                                   const core_engine::Schema& schema,
                                   std::uint32_t max_record_bytes = 1U << 20U,
                                   std::uint32_t read_buffer_bytes = 1U << 20U) {
  std::vector<OwnedRecord> results;
  core_engine::parse_file(
      path, schema, max_record_bytes, read_buffer_bytes,
      [&results](const core_engine::RecordView& view) {
        results.push_back(OwnedRecord{core_engine::PackedRecord(std::string(view.record)),
                                      core_engine::CanonicalKey(std::string(view.canonical_key)),
                                      view.line});
      });
  return results;
}

std::uint64_t expect_malformed(const std::filesystem::path& path, const core_engine::Schema& schema,
                               std::uint32_t max_record_bytes = 1U << 20U,
                               std::uint32_t read_buffer_bytes = 1U << 20U) {
  try {
    static_cast<void>(run_parse(path, schema, max_record_bytes, read_buffer_bytes));
  } catch (const core_engine::Error& error) {
    if (error.code() != core_engine::ErrorCode::MalformedRecord) {
      throw std::runtime_error("expected MalformedRecord, got a different error code");
    }
    if (!error.line().has_value()) {
      throw std::runtime_error("expected a line number on the malformed record error");
    }
    return *error.line();
  }
  throw std::runtime_error("expected parse_file to reject the input");
}

core_engine::PackedRecord expected_record(const core_engine::Schema& schema,
                                          std::vector<core_engine::FieldValue> values) {
  return core_engine::pack_record(schema, values);
}

}

CORE_ENGINE_TEST(parser_02, "PAR-02") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,note:string", "id");
  const std::filesystem::path path = write_input(test_context, "par02.tsv", "abc\t\n");
  const std::vector<OwnedRecord> results = run_parse(path, schema);
  CORE_ASSERT_EQ(results.size(), static_cast<std::size_t>(1));
  const core_engine::PackedRecord expected =
      expected_record(schema, {std::string("abc"), std::string("")});
  CORE_ASSERT_EQ(std::string(results[0].record.bytes()), std::string(expected.bytes()));
  CORE_ASSERT_EQ(results[0].line, static_cast<std::uint64_t>(1));
}

CORE_ENGINE_TEST(parser_03, "PAR-03") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,note:string", "id");
  const std::filesystem::path path =
      write_input(test_context, "par03.tsv", "k\\t1\\n2\\r3\\\\4\tv\\t1\\n2\\r3\\\\4\n");
  const std::vector<OwnedRecord> results = run_parse(path, schema);
  CORE_ASSERT_EQ(results.size(), static_cast<std::size_t>(1));
  const std::string key_decoded = "k\t1\n2\r3\\4";
  const std::string note_decoded = "v\t1\n2\r3\\4";
  const core_engine::PackedRecord expected = expected_record(schema, {key_decoded, note_decoded});
  CORE_ASSERT_EQ(std::string(results[0].record.bytes()), std::string(expected.bytes()));
}

CORE_ENGINE_TEST(parser_04, "PAR-04") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,age:int32", "id");
  const core_engine::PackedRecord expected = expected_record(schema, {std::string("a"), 1});

  const std::filesystem::path lf = write_input(test_context, "par04-lf.tsv", "a\t1\nb\t2\n");
  const std::vector<OwnedRecord> lf_results = run_parse(lf, schema);
  CORE_ASSERT_EQ(lf_results.size(), static_cast<std::size_t>(2));
  CORE_ASSERT_EQ(std::string(lf_results[0].record.bytes()), std::string(expected.bytes()));

  const std::filesystem::path crlf =
      write_input(test_context, "par04-crlf.tsv", "a\t1\r\nb\t2\r\n");
  const std::vector<OwnedRecord> crlf_results = run_parse(crlf, schema);
  CORE_ASSERT_EQ(crlf_results.size(), static_cast<std::size_t>(2));
  CORE_ASSERT_EQ(std::string(crlf_results[0].record.bytes()), std::string(expected.bytes()));

  const std::filesystem::path unterminated =
      write_input(test_context, "par04-unterminated.tsv", "a\t1\nb\t2");
  const std::vector<OwnedRecord> unterminated_results = run_parse(unterminated, schema);
  CORE_ASSERT_EQ(unterminated_results.size(), static_cast<std::size_t>(2));
  CORE_ASSERT_EQ(std::string(unterminated_results[0].record.bytes()),
                 std::string(expected.bytes()));
  const core_engine::PackedRecord expected_second = expected_record(schema, {std::string("b"), 2});
  CORE_ASSERT_EQ(std::string(unterminated_results[1].record.bytes()),
                 std::string(expected_second.bytes()));
}

CORE_ENGINE_TEST(parser_05, "PAR-05") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,value:int32", "id");
  const std::filesystem::path path =
      write_input(test_context, "par05.tsv", "a\t-2147483648\nb\t0\nc\t2147483647\n");
  const std::vector<OwnedRecord> results = run_parse(path, schema);
  CORE_ASSERT_EQ(results.size(), static_cast<std::size_t>(3));
  const core_engine::PackedRecord expected_min =
      expected_record(schema, {std::string("a"), std::numeric_limits<std::int32_t>::min()});
  const core_engine::PackedRecord expected_zero = expected_record(schema, {std::string("b"), 0});
  const core_engine::PackedRecord expected_max =
      expected_record(schema, {std::string("c"), std::numeric_limits<std::int32_t>::max()});
  CORE_ASSERT_EQ(std::string(results[0].record.bytes()), std::string(expected_min.bytes()));
  CORE_ASSERT_EQ(std::string(results[1].record.bytes()), std::string(expected_zero.bytes()));
  CORE_ASSERT_EQ(std::string(results[2].record.bytes()), std::string(expected_max.bytes()));
}

CORE_ENGINE_TEST(parser_06, "PAR-06") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,value:int32", "id");
  const std::vector<std::string> bad_values{"2147483648", "-2147483649", "+1",  "1 2",
                                            "",           "007",         "-007"};
  for (const std::string& value : bad_values) {
    const std::filesystem::path path =
        write_input(test_context, "par06-" + value + ".tsv", "a\t" + value + "\n");
    static_cast<void>(expect_malformed(path, schema));
  }
}

CORE_ENGINE_TEST(parser_07, "PAR-07") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,note:string", "id");
  const std::filesystem::path empty_key = write_input(test_context, "par07-empty-key.tsv", "\tv\n");
  static_cast<void>(expect_malformed(empty_key, schema));

  const std::filesystem::path bad_escape =
      write_input(test_context, "par07-bad-escape.tsv", "a\tv\\x\n");
  static_cast<void>(expect_malformed(bad_escape, schema));

  const std::filesystem::path nul_byte =
      write_input(test_context, "par07-nul.tsv", std::string("a\tv") + '\0' + "\n");
  static_cast<void>(expect_malformed(nul_byte, schema));
}

CORE_ENGINE_TEST(parser_08, "PAR-08") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,age:int32", "id");
  const std::filesystem::path missing = write_input(test_context, "par08-missing.tsv", "a\t1\nb\n");
  CORE_ASSERT_EQ(expect_malformed(missing, schema), static_cast<std::uint64_t>(2));

  const std::filesystem::path extra =
      write_input(test_context, "par08-extra.tsv", "a\t1\nb\t2\textra\n");
  CORE_ASSERT_EQ(expect_malformed(extra, schema), static_cast<std::uint64_t>(2));
}

CORE_ENGINE_TEST(parser_09, "PAR-09") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string", "id");
  constexpr std::uint32_t max_record_bytes = 64U;
  const std::string exact_key(max_record_bytes, 'x');
  const std::filesystem::path exact =
      write_input(test_context, "par09-exact.tsv", exact_key + "\n");
  const std::vector<OwnedRecord> exact_results =
      run_parse(exact, schema, max_record_bytes, max_record_bytes);
  CORE_ASSERT_EQ(exact_results.size(), static_cast<std::size_t>(1));

  const std::string over_key(max_record_bytes + 1U, 'x');
  const std::filesystem::path over = write_input(test_context, "par09-over.tsv", over_key + "\n");
  static_cast<void>(expect_malformed(over, schema, max_record_bytes, max_record_bytes));
}

CORE_ENGINE_TEST(parser_10, "PAR-10") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,note:string", "id");
  const std::string long_value(100U, 'y');
  const std::string content = "a\t" + long_value + "\nb\tsecond\n";
  const std::filesystem::path path = write_input(test_context, "par10.tsv", content);

  const std::vector<OwnedRecord> small_buffer_results = run_parse(path, schema, 4096U, 16U);
  const std::vector<OwnedRecord> large_buffer_results = run_parse(path, schema, 4096U, 4096U);

  CORE_ASSERT_EQ(small_buffer_results.size(), static_cast<std::size_t>(2));
  CORE_ASSERT_EQ(small_buffer_results.size(), large_buffer_results.size());
  for (std::size_t index = 0U; index < small_buffer_results.size(); ++index) {
    CORE_ASSERT_EQ(std::string(small_buffer_results[index].record.bytes()),
                   std::string(large_buffer_results[index].record.bytes()));
  }
}

CORE_ENGINE_TEST(parser_11, "PAR-11") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string", "id");
  const std::filesystem::path path = write_input(test_context, "par11.tsv", "");
  const std::vector<OwnedRecord> results = run_parse(path, schema);
  CORE_ASSERT_EQ(results.size(), static_cast<std::size_t>(0));
}

CORE_ENGINE_TEST(parser_12, "PAR-12") {
  const core_engine::Schema schema = core_engine::Schema::parse("id:string,note:string", "id");
  const std::string raw_bytes = "\xC3\x28\xFF";
  const std::filesystem::path path =
      write_input(test_context, "par12.tsv", "a\t" + raw_bytes + "\n");
  const std::vector<OwnedRecord> results = run_parse(path, schema);
  CORE_ASSERT_EQ(results.size(), static_cast<std::size_t>(1));
  const core_engine::PackedRecord expected = expected_record(schema, {std::string("a"), raw_bytes});
  CORE_ASSERT_EQ(std::string(results[0].record.bytes()), std::string(expected.bytes()));
}
