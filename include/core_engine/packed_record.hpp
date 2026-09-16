/// Declares the packed record format and canonical keys for validated schemas.

#pragma once

#include "core_engine/schema.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace core_engine {

/// Tags a canonical key derived from a string field.
inline constexpr char kStringKeyTag = '\x01';

/// Tags a canonical key derived from an int32 field.
inline constexpr char kInt32KeyTag = '\x02';

/// Appends value as four big-endian bytes.
inline void append_u32_be(std::string& out, std::uint32_t value) {
  const char bytes[4]{static_cast<char>(static_cast<std::uint8_t>(value >> 24U)),
                      static_cast<char>(static_cast<std::uint8_t>(value >> 16U)),
                      static_cast<char>(static_cast<std::uint8_t>(value >> 8U)),
                      static_cast<char>(static_cast<std::uint8_t>(value))};
  out.append(bytes, 4U);
}

/// Appends value as eight big-endian bytes.
inline void append_u64_be(std::string& out, std::uint64_t value) {
  append_u32_be(out, static_cast<std::uint32_t>(value >> 32U));
  append_u32_be(out, static_cast<std::uint32_t>(value));
}

/// Reads four big-endian bytes at offset; requires offset + 4 <= bytes.size().
[[nodiscard]] inline std::uint32_t read_u32_be(std::string_view bytes, std::size_t offset) {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < 4U; ++index) {
    value = (value << 8U) |
            static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + index]));
  }
  return value;
}

/// Reads eight big-endian bytes at offset; requires offset + 8 <= bytes.size().
[[nodiscard]] inline std::uint64_t read_u64_be(std::string_view bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(read_u32_be(bytes, offset)) << 32U) |
         read_u32_be(bytes, offset + 4U);
}

/// Appends a packed string field, a big-endian uint32 length then the bytes, and returns the
/// offset of the value bytes within out.
inline std::size_t append_string_field(std::string& out, std::string_view value) {
  append_u32_be(out, static_cast<std::uint32_t>(value.size()));
  const std::size_t value_offset = out.size();
  out.append(value);
  return value_offset;
}

/// Appends a packed int32 field, four big-endian bytes, and returns their offset within out.
inline std::size_t append_int32_field(std::string& out, std::int32_t value) {
  const std::size_t value_offset = out.size();
  append_u32_be(out, static_cast<std::uint32_t>(value));
  return value_offset;
}

/// Replaces out with the canonical key: tag followed by the key field's packed value bytes.
inline void assign_canonical_key(std::string& out, char tag, std::string_view value) {
  out.assign(1U, tag);
  out.append(value);
}

/// Holds one schema-conformant field value ready for packing.
using FieldValue = std::variant<std::string, std::int32_t>;

/// Owns one packed record: every field in schema order in its packed field form.
class PackedRecord final {
public:
  /// Wraps already-packed bytes without validation.
  explicit PackedRecord(std::string bytes) noexcept : bytes_(std::move(bytes)) {}

  /// Returns the packed bytes.
  [[nodiscard]] std::string_view bytes() const noexcept { return bytes_; }

private:
  std::string bytes_;
};

/// Owns one canonical key: a type tag followed by the key value bytes.
class CanonicalKey final {
public:
  /// Wraps already-tagged key bytes without validation.
  explicit CanonicalKey(std::string bytes) noexcept : bytes_(std::move(bytes)) {}

  /// Returns the tagged key bytes.
  [[nodiscard]] std::string_view bytes() const noexcept { return bytes_; }

  /// Converts implicitly to a view of the tagged bytes.
  operator std::string_view() const noexcept { return bytes_; }

  /// Compares two canonical keys by their tagged bytes.
  friend bool operator==(const CanonicalKey& left, const CanonicalKey& right) noexcept {
    return left.bytes_ == right.bytes_;
  }

private:
  std::string bytes_;
};

/// Locates one packed record at the start of a buffer: total length and key value bytes.
struct RecordLayout final {
  std::size_t length;
  std::size_t key_offset;
  std::size_t key_length;
  char key_tag;
};

/// Walks schema over the record at the start of bytes, which may continue with more records.
/// Throws Error(InternalInvariant) if bytes end before the record does.
[[nodiscard]] RecordLayout scan_record(const Schema& schema, std::string_view bytes);

/// Packs schema-conformant values. Throws Error(InternalInvariant) on a count or type mismatch.
[[nodiscard]] PackedRecord pack_record(const Schema& schema, const std::vector<FieldValue>& values);

/// Builds the canonical key of a record packed for schema. Throws Error(InternalInvariant) if the
/// bytes are not exactly one such record.
[[nodiscard]] CanonicalKey build_canonical_key(const Schema& schema, const PackedRecord& record);

}
