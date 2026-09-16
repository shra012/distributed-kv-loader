/// Implements the streaming TSV record parser and its per-record callback contract.

#include "core_engine/parser.hpp"

#include "core_engine/decimal.hpp"
#include "core_engine/error.hpp"
#include "core_engine/packed_record.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core_engine {
namespace {

constexpr std::uint64_t kByteOnes = UINT64_C(0x0101010101010101);
constexpr std::uint64_t kByteHighs = UINT64_C(0x8080808080808080);

[[noreturn]] void parse_error(std::string detail, const std::filesystem::path& path,
                              std::uint64_t line) {
  throw Error(ErrorCode::MalformedRecord, std::move(detail), std::nullopt, path.string(), line);
}

[[nodiscard]] constexpr std::uint64_t zero_byte_mask(std::uint64_t word) noexcept {
  return (word - kByteOnes) & ~word & kByteHighs;
}

[[nodiscard]] constexpr bool is_field_special(char character) noexcept {
  return character == '\t' || character == '\\' || character == '\0';
}

[[nodiscard]] std::size_t find_field_special(std::string_view line, std::size_t from) noexcept {
  std::size_t index = from;
  for (; index + 8U <= line.size(); index += 8U) {
    std::uint64_t word = 0U;
    std::memcpy(&word, line.data() + index, 8U);
    if ((zero_byte_mask(word ^ (kByteOnes * '\t')) | zero_byte_mask(word ^ (kByteOnes * '\\')) |
         zero_byte_mask(word)) != 0U) {
      break;
    }
  }
  while (index < line.size() && !is_field_special(line[index])) {
    ++index;
  }
  return index;
}

[[nodiscard]] bool append_unescaped(std::string_view token, std::string& out) {
  for (std::size_t index = 0U; index < token.size(); ++index) {
    if (token[index] != '\\') {
      out.push_back(token[index]);
      continue;
    }
    if (++index == token.size()) {
      return false;
    }
    switch (token[index]) {
    case '\\':
      out.push_back('\\');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    default:
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::int32_t> parse_int32(std::string_view token) noexcept {
  const bool negative = !token.empty() && token.front() == '-';
  const std::string_view digits = token.substr(negative ? 1U : 0U);
  if (digits.size() > 1U && digits.front() == '0') {
    return std::nullopt;
  }
  const std::optional<std::uint64_t> magnitude = parse_decimal(digits);
  const std::uint64_t limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + (negative ? 1U : 0U);
  if (!magnitude.has_value() || *magnitude > limit) {
    return std::nullopt;
  }
  const auto signed_magnitude = static_cast<std::int64_t>(*magnitude);
  return static_cast<std::int32_t>(negative ? -signed_magnitude : signed_magnitude);
}

[[nodiscard]] std::string_view strip_trailing_cr(std::string_view raw_line) noexcept {
  if (!raw_line.empty() && raw_line.back() == '\r') {
    raw_line.remove_suffix(1U);
  }
  return raw_line;
}

class RecordPacker final {
public:
  RecordPacker(const Schema& schema, const std::filesystem::path& path)
      : fields_(schema.fields()), key_index_(schema.key_index()),
        key_tag_(schema.key_field().type == FieldType::String ? kStringKeyTag : kInt32KeyTag),
        path_(path) {}

  void pack(std::string_view line, std::uint64_t line_number, const RecordCallback& callback) {
    record_.clear();
    std::size_t key_offset = 0U;
    std::size_t key_length = 0U;
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < fields_.size(); ++index) {
      bool escaped = false;
      std::size_t end = find_field_special(line, start);
      while (end < line.size() && line[end] != '\t') {
        if (line[end] == '\0') {
          parse_error("record contains a raw NUL byte", path_, line_number);
        }
        escaped = true;
        end = find_field_special(line, end + 1U);
      }
      if ((index + 1U == fields_.size()) != (end == line.size())) {
        reject_field_count(line, line_number);
      }
      const std::size_t value_offset =
          append_field(fields_[index], line.substr(start, end - start), escaped, line_number);
      if (index == key_index_) {
        key_offset = value_offset;
        key_length = record_.size() - value_offset;
      }
      start = end + 1U;
    }
    const std::string_view key_value = std::string_view(record_).substr(key_offset, key_length);
    if (key_tag_ == kStringKeyTag && key_value.empty()) {
      parse_error("string key field must not be empty", path_, line_number);
    }
    assign_canonical_key(key_, key_tag_, key_value);
    callback(RecordView{record_, key_, line_number});
  }

private:
  std::size_t append_field(const Field& field, std::string_view token, bool escaped,
                           std::uint64_t line_number) {
    if (field.type == FieldType::Int32) {
      const std::optional<std::int32_t> number = parse_int32(token);
      if (!number.has_value()) {
        parse_error("invalid int32 value in field: " + field.name, path_, line_number);
      }
      return append_int32_field(record_, *number);
    }
    if (!escaped) {
      return append_string_field(record_, token);
    }
    unescaped_.clear();
    if (!append_unescaped(token, unescaped_)) {
      parse_error("invalid escape sequence in field: " + field.name, path_, line_number);
    }
    return append_string_field(record_, unescaped_);
  }

  [[noreturn]] void reject_field_count(std::string_view line, std::uint64_t line_number) const {
    const auto tabs = static_cast<std::size_t>(std::count(line.begin(), line.end(), '\t'));
    parse_error("record has " + std::to_string(tabs + 1U) + " fields, schema requires " +
                    std::to_string(fields_.size()),
                path_, line_number);
  }

  const std::vector<Field>& fields_;
  std::size_t key_index_;
  char key_tag_;
  const std::filesystem::path& path_;
  std::string record_;
  std::string unescaped_;
  std::string key_;
};

}

void parse_file(const std::filesystem::path& path, const Schema& schema,
                std::uint32_t max_record_bytes, std::uint32_t read_buffer_bytes,
                const RecordCallback& callback) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw Error(ErrorCode::InputFile, "cannot open input file", std::nullopt, path.string());
  }

  RecordPacker packer(schema, path);
  std::string buffer(read_buffer_bytes, '\0');
  std::string pending;
  std::uint64_t line_number = 0U;

  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read_count = static_cast<std::size_t>(input.gcount());
    if (read_count == 0U) {
      break;
    }
    std::size_t start = 0U;
    while (start < read_count) {
      const std::string_view chunk(buffer.data() + start, read_count - start);
      const std::size_t newline = chunk.find('\n');
      const std::size_t line_bytes = newline == std::string_view::npos ? chunk.size() : newline;
      if (line_bytes > static_cast<std::size_t>(max_record_bytes) - pending.size()) {
        parse_error("record exceeds max_record_bytes", path, line_number + 1U);
      }
      if (newline == std::string_view::npos) {
        pending.append(chunk);
        break;
      }
      ++line_number;
      if (pending.empty()) {
        packer.pack(strip_trailing_cr(chunk.substr(0U, newline)), line_number, callback);
      } else {
        pending.append(chunk.substr(0U, newline));
        packer.pack(strip_trailing_cr(pending), line_number, callback);
        pending.clear();
      }
      start += newline + 1U;
    }
  }
  if (input.bad()) {
    throw Error(ErrorCode::InputFile, "failed while reading input file", std::nullopt,
                path.string());
  }
  if (!pending.empty()) {
    packer.pack(pending, ++line_number, callback);
  }
}

}
