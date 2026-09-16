/// Implements schema parsing, field validation, and key selection.

#include "core_engine/schema.hpp"

#include "core_engine/error.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace core_engine {
namespace {

[[noreturn]] void schema_error(std::string detail) {
  throw Error(ErrorCode::Schema, std::move(detail));
}

bool valid_name(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  const auto letter = [](char character) {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
  };
  if (!letter(name.front()) && name.front() != '_') {
    return false;
  }
  return std::all_of(name.cbegin() + 1, name.cend(), [letter](char character) {
    return letter(character) || (character >= '0' && character <= '9') || character == '_';
  });
}

FieldType parse_type(std::string_view value) {
  if (value == "string") {
    return FieldType::String;
  }
  if (value == "int32") {
    return FieldType::Int32;
  }
  schema_error("unsupported field type");
}

Field parse_field(std::string_view definition) {
  const std::size_t separator = definition.find(':');
  if (separator == std::string_view::npos || separator == 0U ||
      separator + 1U >= definition.size() ||
      definition.find(':', separator + 1U) != std::string_view::npos) {
    schema_error("malformed schema field");
  }
  const std::string_view name = definition.substr(0U, separator);
  if (!valid_name(name)) {
    schema_error("invalid schema field name");
  }
  return Field{std::string(name), parse_type(definition.substr(separator + 1U))};
}

std::vector<Field> parse_fields(std::string_view definition) {
  if (definition.empty()) {
    schema_error("schema must contain at least one field");
  }
  std::vector<Field> fields;
  std::unordered_set<std::string> names;
  std::size_t begin = 0U;
  while (begin <= definition.size()) {
    const std::size_t end = definition.find(',', begin);
    const std::size_t length =
        end == std::string_view::npos ? definition.size() - begin : end - begin;
    Field field = parse_field(definition.substr(begin, length));
    if (!names.insert(field.name).second) {
      schema_error("duplicate schema field name");
    }
    fields.push_back(std::move(field));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
  return fields;
}

std::size_t find_key(const std::vector<Field>& fields, std::string_view key_field) {
  if (!valid_name(key_field)) {
    schema_error("invalid key field name");
  }
  const auto match = std::find_if(fields.cbegin(), fields.cend(), [key_field](const Field& field) {
    return field.name == key_field;
  });
  if (match == fields.cend()) {
    schema_error("key field is not present in schema");
  }
  return static_cast<std::size_t>(std::distance(fields.cbegin(), match));
}

}

Schema::Schema(std::vector<Field> fields, std::size_t key_index)
    : fields_(std::move(fields)), key_index_(key_index) {}

Schema Schema::parse(std::string_view definition, std::string_view key_field) {
  std::vector<Field> fields = parse_fields(definition);
  const std::size_t key_index = find_key(fields, key_field);
  return Schema(std::move(fields), key_index);
}

const std::vector<Field>& Schema::fields() const noexcept { return fields_; }

std::size_t Schema::key_index() const noexcept { return key_index_; }

const Field& Schema::key_field() const noexcept { return fields_[key_index_]; }

}
