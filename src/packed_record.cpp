/// Implements the packed record format and canonical keys for validated schemas.

#include "core_engine/packed_record.hpp"

#include "core_engine/error.hpp"

#include <limits>
#include <utility>

namespace core_engine {

RecordLayout scan_record(const Schema& schema, std::string_view bytes) {
  const std::vector<Field>& fields = schema.fields();
  const std::size_t key_index = schema.key_index();
  RecordLayout layout{0U, 0U, 0U, kStringKeyTag};
  std::size_t offset = 0U;
  for (std::size_t index = 0U; index < fields.size(); ++index) {
    std::size_t value_offset = offset;
    std::size_t value_length = 4U;
    if (fields[index].type == FieldType::String) {
      if (offset + 4U > bytes.size()) {
        throw Error(ErrorCode::InternalInvariant, "packed record truncated at string length");
      }
      value_offset = offset + 4U;
      value_length = read_u32_be(bytes, offset);
    }
    if (value_offset > bytes.size() || value_length > bytes.size() - value_offset) {
      throw Error(ErrorCode::InternalInvariant, "packed record truncated at field bytes");
    }
    if (index == key_index) {
      layout.key_offset = value_offset;
      layout.key_length = value_length;
      layout.key_tag = fields[index].type == FieldType::String ? kStringKeyTag : kInt32KeyTag;
    }
    offset = value_offset + value_length;
  }
  layout.length = offset;
  return layout;
}

PackedRecord pack_record(const Schema& schema, const std::vector<FieldValue>& values) {
  const std::vector<Field>& fields = schema.fields();
  if (values.size() != fields.size()) {
    throw Error(ErrorCode::InternalInvariant, "field value count does not match schema");
  }
  std::string bytes;
  for (std::size_t index = 0U; index < fields.size(); ++index) {
    if (fields[index].type == FieldType::String) {
      const auto* text = std::get_if<std::string>(&values[index]);
      if (text == nullptr || text->size() > std::numeric_limits<std::uint32_t>::max()) {
        throw Error(ErrorCode::InternalInvariant, "invalid string field value");
      }
      static_cast<void>(append_string_field(bytes, *text));
    } else {
      const auto* number = std::get_if<std::int32_t>(&values[index]);
      if (number == nullptr) {
        throw Error(ErrorCode::InternalInvariant, "invalid int32 field value");
      }
      static_cast<void>(append_int32_field(bytes, *number));
    }
  }
  return PackedRecord(std::move(bytes));
}

CanonicalKey build_canonical_key(const Schema& schema, const PackedRecord& record) {
  const std::string_view bytes = record.bytes();
  const RecordLayout layout = scan_record(schema, bytes);
  if (layout.length != bytes.size()) {
    throw Error(ErrorCode::InternalInvariant, "packed record has trailing bytes");
  }
  std::string key;
  assign_canonical_key(key, layout.key_tag, bytes.substr(layout.key_offset, layout.key_length));
  return CanonicalKey(std::move(key));
}

}
