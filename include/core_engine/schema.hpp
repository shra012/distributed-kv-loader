/// Defines validated ordered record schemas and key selection.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace core_engine {

/// Identifies a supported schema field representation.
enum class FieldType { String, Int32 };

/// Describes one named field in schema order.
struct Field final {
  std::string name;
  FieldType type;
};

/// Owns a non-empty validated schema and its selected key field.
class Schema final {
public:
  /// Parses a schema definition and rejects malformed fields or an invalid key selection.
  [[nodiscard]] static Schema parse(std::string_view definition, std::string_view key_field);

  /// Returns fields in their configured order.
  [[nodiscard]] const std::vector<Field>& fields() const noexcept;

  /// Returns the index of the configured key field.
  [[nodiscard]] std::size_t key_index() const noexcept;

  /// Returns the configured key field.
  [[nodiscard]] const Field& key_field() const noexcept;

private:
  Schema(std::vector<Field> fields, std::size_t key_index);

  std::vector<Field> fields_;
  std::size_t key_index_;
};

}
