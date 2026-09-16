/// Defines typed failures, diagnostics, and process exit mappings.

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace core_engine {

/// Identifies a stable Core Engine failure category.
enum class ErrorCode {
  CommandLine,
  Configuration,
  Schema,
  InputFile,
  MalformedRecord,
  ConflictingDuplicate,
  Transport,
  Codec,
  Verification,
  ManifestMismatch,
  InternalInvariant
};

/// Carries a stable code and optional source location without record data.
class Error final : public std::runtime_error {
public:
  /// Creates an error with optional node, file, and line context.
  Error(ErrorCode code, std::string detail, std::optional<std::uint32_t> node = std::nullopt,
        std::string file = {}, std::optional<std::uint64_t> line = std::nullopt);

  /// Returns the stable error category.
  [[nodiscard]] ErrorCode code() const noexcept;

  /// Returns the node context when known.
  [[nodiscard]] const std::optional<std::uint32_t>& node() const noexcept;

  /// Returns the source file context when known.
  [[nodiscard]] const std::string& file() const noexcept;

  /// Returns the source line context when known.
  [[nodiscard]] const std::optional<std::uint64_t>& line() const noexcept;

  /// Returns the diagnostic detail.
  [[nodiscard]] const std::string& detail() const noexcept;

private:
  ErrorCode code_;
  std::optional<std::uint32_t> node_;
  std::string file_;
  std::optional<std::uint64_t> line_;
  std::string detail_;
};

/// Returns the stable uppercase name for an error code.
[[nodiscard]] std::string_view error_code_name(ErrorCode code) noexcept;

/// Maps an error code to its documented process exit status.
[[nodiscard]] int exit_code(ErrorCode code) noexcept;

/// Formats exactly one sanitized ERROR diagnostic line without a trailing newline.
[[nodiscard]] std::string format_error(const Error& error);

/// Thread-safe box holding at most the first error reported to it. Safe to call from any thread;
/// the first report wins and every later report is silently discarded.
class FirstFailure final {
public:
  /// Records error unless a failure was already recorded.
  void report(Error error) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!error_.has_value()) {
      error_.emplace(std::move(error));
    }
  }

  /// Returns true once a failure has been recorded.
  [[nodiscard]] bool has_error() const noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    return error_.has_value();
  }

  /// Returns a copy of the first recorded failure, or nullopt when none was reported.
  [[nodiscard]] std::optional<Error> error() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return error_;
  }

private:
  mutable std::mutex mutex_;
  std::optional<Error> error_;
};

}
