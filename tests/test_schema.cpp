/// Verifies schema field syntax, supported types, uniqueness, and key selection.

#include "core_engine/error.hpp"
#include "core_engine/schema.hpp"
#include "test_framework.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

core_engine::ErrorCode schema_error(std::string_view definition, std::string_view key_field) {
  try {
    static_cast<void>(core_engine::Schema::parse(definition, key_field));
  } catch (const core_engine::Error& error) {
    return error.code();
  }
  throw std::runtime_error("expected schema failure");
}

}

CORE_ENGINE_TEST(schema_supported_fields_and_keys, "SCH-01") {
  static_cast<void>(test_context);
  const core_engine::Schema string_key = core_engine::Schema::parse("id:string,age:int32", "id");
  const core_engine::Schema int_key =
      core_engine::Schema::parse("name:string,identifier:int32", "identifier");

  CORE_ASSERT_EQ(string_key.fields().size(), std::size_t{2U});
  CORE_ASSERT_EQ(string_key.fields()[0U].name, "id");
  CORE_ASSERT_EQ(string_key.fields()[0U].type, core_engine::FieldType::String);
  CORE_ASSERT_EQ(string_key.fields()[1U].type, core_engine::FieldType::Int32);
  CORE_ASSERT_EQ(string_key.key_index(), std::size_t{0U});
  CORE_ASSERT_EQ(int_key.key_index(), std::size_t{1U});
  CORE_ASSERT_EQ(int_key.key_field().name, "identifier");
  CORE_ASSERT_EQ(int_key.key_field().type, core_engine::FieldType::Int32);
}

CORE_ENGINE_TEST(schema_structure_validation, "SCH-02") {
  static_cast<void>(test_context);
  const std::vector<std::pair<std::string_view, std::string_view>> invalid{
      {"", "id"},
      {"id:string,id:int32", "id"},
      {"id:string", "missing"},
      {"9id:string", "9id"},
      {"bad-name:string", "bad-name"},
      {"id:string", ""},
  };

  for (const auto& item : invalid) {
    CORE_ASSERT_EQ(schema_error(item.first, item.second), core_engine::ErrorCode::Schema);
  }
}

CORE_ENGINE_TEST(schema_type_validation, "SCH-03") {
  static_cast<void>(test_context);
  const std::vector<std::string_view> invalid{"id:uint32",
                                              "id",
                                              "id:",
                                              ":string",
                                              "id:string:extra",
                                              "id: string",
                                              "id:string,",
                                              ",id:string",
                                              "id:string,,name:string"};

  for (const std::string_view definition : invalid) {
    CORE_ASSERT_EQ(schema_error(definition, "id"), core_engine::ErrorCode::Schema);
  }
}
