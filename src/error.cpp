/// Implements stable typed errors, exit mappings, and safe diagnostics.

#include "core_engine/error.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace core_engine {
namespace {

std::string quote(std::string_view value) {
  constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string result;
  result.reserve(value.size() + 2U);
  result.push_back('"');
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (character == '\\' || character == '"') {
      result.push_back('\\');
      result.push_back(character);
    } else if (character == '\n') {
      result.append("\\n");
    } else if (character == '\r') {
      result.append("\\r");
    } else if (character == '\t') {
      result.append("\\t");
    } else if (byte < 0x20U || byte == 0x7FU) {
      result.append("\\x");
      result.push_back(digits[static_cast<std::size_t>(byte >> 4U)]);
      result.push_back(digits[static_cast<std::size_t>(byte & 0x0FU)]);
    } else {
      result.push_back(character);
    }
  }
  result.push_back('"');
  return result;
}

std::string token(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    result.push_back(byte <= 0x20U || byte == 0x7FU ? '_' : character);
  }
  return result;
}

}

Error::Error(ErrorCode code, std::string detail, std::optional<std::uint32_t> node,
             std::string file, std::optional<std::uint64_t> line)
    : std::runtime_error(detail), code_(code), node_(node), file_(std::move(file)), line_(line),
      detail_(std::move(detail)) {}

ErrorCode Error::code() const noexcept { return code_; }

const std::optional<std::uint32_t>& Error::node() const noexcept { return node_; }

const std::string& Error::file() const noexcept { return file_; }

const std::optional<std::uint64_t>& Error::line() const noexcept { return line_; }

const std::string& Error::detail() const noexcept { return detail_; }

std::string_view error_code_name(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::CommandLine:
    return "COMMAND_LINE";
  case ErrorCode::Configuration:
    return "CONFIGURATION";
  case ErrorCode::Schema:
    return "SCHEMA";
  case ErrorCode::InputFile:
    return "INPUT_FILE";
  case ErrorCode::MalformedRecord:
    return "MALFORMED_RECORD";
  case ErrorCode::ConflictingDuplicate:
    return "CONFLICTING_DUPLICATE";
  case ErrorCode::Transport:
    return "TRANSPORT";
  case ErrorCode::Codec:
    return "CODEC";
  case ErrorCode::Verification:
    return "VERIFICATION";
  case ErrorCode::ManifestMismatch:
    return "MANIFEST_MISMATCH";
  case ErrorCode::InternalInvariant:
    return "INTERNAL_INVARIANT";
  }
  return "INTERNAL_INVARIANT";
}

int exit_code(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::CommandLine:
  case ErrorCode::Configuration:
    return 2;
  case ErrorCode::Schema:
  case ErrorCode::InputFile:
  case ErrorCode::MalformedRecord:
  case ErrorCode::ConflictingDuplicate:
    return 3;
  case ErrorCode::Transport:
  case ErrorCode::Codec:
    return 4;
  case ErrorCode::Verification:
  case ErrorCode::ManifestMismatch:
    return 5;
  case ErrorCode::InternalInvariant:
    return 6;
  }
  return 6;
}

std::string format_error(const Error& error) {
  std::string result = "ERROR code=";
  result.append(error_code_name(error.code()));
  if (error.node().has_value()) {
    result.append(" node=");
    result.append(std::to_string(*error.node()));
  }
  if (!error.file().empty()) {
    result.append(" file=");
    result.append(token(error.file()));
  }
  if (error.line().has_value()) {
    result.append(" line=");
    result.append(std::to_string(*error.line()));
  }
  result.append(" detail=");
  result.append(quote(error.detail()));
  return result;
}

}
