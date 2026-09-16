/// Implements strict configuration parsing, path resolution, and budget validation.

#include "core_engine/config.hpp"

#include "core_engine/decimal.hpp"
#include "core_engine/error.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core_engine {
namespace {

constexpr std::uint32_t default_batch_bytes = 262144U;
constexpr std::uint32_t default_channel_bytes = 1048576U;
constexpr std::uint32_t default_inbound_queue_batches = 8U;
constexpr std::uint32_t default_max_record_bytes = 65536U;
constexpr std::uint32_t default_read_buffer_bytes = 1048576U;
constexpr std::uint64_t maximum_temporary_memory_bytes = UINT64_C(1073741824);
constexpr std::uint64_t frame_header_bytes = 32U;

struct Entry final {
  std::string value;
  std::uint64_t line;
};

using Entries = std::unordered_map<std::string, Entry>;

[[noreturn]] void config_error(std::string detail, const std::filesystem::path& path,
                               std::optional<std::uint64_t> line = std::nullopt) {
  throw Error(ErrorCode::Configuration, std::move(detail), std::nullopt, path.string(), line);
}

std::string_view trim(std::string_view value) {
  const auto whitespace = [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  };
  while (!value.empty() && whitespace(value.front())) {
    value.remove_prefix(1U);
  }
  while (!value.empty() && whitespace(value.back())) {
    value.remove_suffix(1U);
  }
  return value;
}

bool fixed_key(std::string_view key) {
  return key == "version" || key == "node_count" || key == "schema" || key == "key_field" ||
         key == "batch_bytes" || key == "channel_bytes" || key == "inbound_queue_batches" ||
         key == "max_record_bytes" || key == "read_buffer_bytes";
}

std::optional<std::uint32_t> input_id(std::string_view key) {
  constexpr std::string_view prefix = "input.";
  if (key.size() < prefix.size() || key.compare(0U, prefix.size(), prefix) != 0) {
    return std::nullopt;
  }
  const std::string_view suffix = key.substr(prefix.size());
  const std::optional<std::uint64_t> value = parse_decimal(suffix);
  if (!value.has_value() || (suffix.size() > 1U && suffix.front() == '0') ||
      *value > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(*value);
}

void add_entry(Entries& entries, std::string_view line, std::uint64_t line_number,
               const std::filesystem::path& path) {
  const std::size_t separator = line.find('=');
  if (separator == std::string_view::npos ||
      line.find('=', separator + 1U) != std::string_view::npos) {
    config_error("configuration line must contain exactly one '='", path, line_number);
  }
  const std::string_view key = trim(line.substr(0U, separator));
  const std::string_view value = trim(line.substr(separator + 1U));
  if (key.empty() || value.empty()) {
    config_error("configuration key and value must be non-empty", path, line_number);
  }
  if (!fixed_key(key) && !input_id(key).has_value()) {
    config_error("unknown configuration key", path, line_number);
  }
  const auto inserted = entries.emplace(std::string(key), Entry{std::string(value), line_number});
  if (!inserted.second) {
    config_error("duplicate configuration key", path, line_number);
  }
}

Entries read_entries(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    config_error("cannot open configuration file", path);
  }
  Entries entries;
  std::string line;
  std::uint64_t line_number = 0U;
  while (std::getline(input, line)) {
    if (line_number == std::numeric_limits<std::uint64_t>::max()) {
      config_error("configuration has too many lines", path);
    }
    ++line_number;
    const std::string_view content = trim(line);
    if (!content.empty() && content.front() != '#') {
      add_entry(entries, content, line_number, path);
    }
  }
  if (input.bad()) {
    config_error("failed while reading configuration file", path);
  }
  return entries;
}

const Entry& required(const Entries& entries, std::string_view key,
                      const std::filesystem::path& path) {
  const auto match = entries.find(std::string(key));
  if (match == entries.cend()) {
    config_error("missing required configuration key: " + std::string(key), path);
  }
  return match->second;
}

std::uint32_t positive_uint32(const Entry& entry, std::string_view key,
                              const std::filesystem::path& path) {
  const std::optional<std::uint64_t> value = parse_decimal(entry.value);
  if (!value.has_value()) {
    config_error("invalid positive integer for " + std::string(key), path, entry.line);
  }
  if (*value > std::numeric_limits<std::uint32_t>::max()) {
    config_error("integer overflow for " + std::string(key), path, entry.line);
  }
  if (*value == 0U) {
    config_error("value must be positive for " + std::string(key), path, entry.line);
  }
  return static_cast<std::uint32_t>(*value);
}

std::uint32_t optional_size(const Entries& entries, std::string_view key,
                            std::uint32_t default_value, const std::filesystem::path& path) {
  const auto match = entries.find(std::string(key));
  return match == entries.cend() ? default_value : positive_uint32(match->second, key, path);
}

std::uint64_t bounded_product(std::initializer_list<std::uint64_t> factors,
                              const std::filesystem::path& path) {
  std::uint64_t product = 1U;
  for (const std::uint64_t factor : factors) {
    if (factor != 0U && product > maximum_temporary_memory_bytes / factor) {
      config_error("temporary memory budget exceeds 1 GiB", path);
    }
    product *= factor;
  }
  return product;
}

void add_budget(std::uint64_t& total, std::uint64_t value, const std::filesystem::path& path) {
  if (value > maximum_temporary_memory_bytes - total) {
    config_error("temporary memory budget exceeds 1 GiB", path);
  }
  total += value;
}

std::uint64_t memory_budget(std::uint32_t node_count, std::uint32_t read_buffer_bytes,
                            std::uint32_t batch_bytes, std::uint32_t channel_bytes,
                            std::uint32_t inbound_queue_batches,
                            const std::filesystem::path& path) {
  const std::uint64_t nodes = node_count;
  const std::uint64_t peers = nodes - 1U;
  std::uint64_t total = 0U;
  add_budget(total, bounded_product({nodes, read_buffer_bytes}, path), path);
  add_budget(total, bounded_product({nodes, nodes, batch_bytes}, path), path);
  add_budget(total, bounded_product({nodes, peers, 2U, channel_bytes}, path), path);
  add_budget(total, bounded_product({nodes, peers, frame_header_bytes + batch_bytes}, path), path);
  add_budget(total, bounded_product({nodes, inbound_queue_batches, batch_bytes}, path), path);
  add_budget(total, bounded_product({nodes, batch_bytes}, path), path);
  return total;
}

std::filesystem::path absolute_path(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::path result = std::filesystem::absolute(path, error);
  if (error) {
    config_error("cannot resolve configuration path", path);
  }
  return result.lexically_normal();
}

std::vector<std::optional<std::filesystem::path>>
parse_inputs(const Entries& entries, std::uint32_t node_count,
             const std::filesystem::path& config_path) {
  for (const auto& item : entries) {
    const std::optional<std::uint32_t> id = input_id(item.first);
    if (id.has_value() && *id >= node_count) {
      config_error("input node ID is outside configured range", config_path, item.second.line);
    }
  }
  std::vector<std::optional<std::filesystem::path>> inputs;
  inputs.reserve(node_count);
  for (std::uint32_t node = 0U; node < node_count; ++node) {
    const std::string key = "input." + std::to_string(node);
    const Entry& entry = required(entries, key, config_path);
    if (entry.value == "-") {
      inputs.emplace_back(std::nullopt);
    } else {
      const std::filesystem::path input(entry.value);
      const std::filesystem::path resolved =
          input.is_absolute() ? input.lexically_normal()
                              : (config_path.parent_path() / input).lexically_normal();
      inputs.emplace_back(resolved);
    }
  }
  return inputs;
}

Schema parse_config_schema(const Entries& entries, const std::filesystem::path& path) {
  const Entry& definition = required(entries, "schema", path);
  const Entry& key_field = required(entries, "key_field", path);
  try {
    return Schema::parse(definition.value, key_field.value);
  } catch (const Error& error) {
    throw Error(error.code(), error.detail(), std::nullopt, path.string(), definition.line);
  }
}

}

Config::Config(std::uint32_t version, std::uint32_t node_count, Schema schema,
               std::vector<std::optional<std::filesystem::path>> input_paths,
               std::uint32_t batch_bytes, std::uint32_t channel_bytes,
               std::uint32_t inbound_queue_batches, std::uint32_t max_record_bytes,
               std::uint32_t read_buffer_bytes, std::uint64_t temporary_memory_bytes)
    : version_(version), node_count_(node_count), schema_(std::move(schema)),
      input_paths_(std::move(input_paths)), batch_bytes_(batch_bytes),
      channel_bytes_(channel_bytes), inbound_queue_batches_(inbound_queue_batches),
      max_record_bytes_(max_record_bytes), read_buffer_bytes_(read_buffer_bytes),
      temporary_memory_bytes_(temporary_memory_bytes) {}

std::uint32_t Config::version() const noexcept { return version_; }

std::uint32_t Config::node_count() const noexcept { return node_count_; }

const Schema& Config::schema() const noexcept { return schema_; }

const std::vector<std::optional<std::filesystem::path>>& Config::input_paths() const noexcept {
  return input_paths_;
}

const std::optional<std::filesystem::path>& Config::input_path(std::uint32_t node) const {
  return input_paths_.at(static_cast<std::size_t>(node));
}

std::uint32_t Config::batch_bytes() const noexcept { return batch_bytes_; }

std::uint32_t Config::channel_bytes() const noexcept { return channel_bytes_; }

std::uint32_t Config::inbound_queue_batches() const noexcept { return inbound_queue_batches_; }

std::uint32_t Config::max_record_bytes() const noexcept { return max_record_bytes_; }

std::uint32_t Config::read_buffer_bytes() const noexcept { return read_buffer_bytes_; }

std::uint64_t Config::temporary_memory_bytes() const noexcept { return temporary_memory_bytes_; }

Config load_config(const std::filesystem::path& path) {
  const std::filesystem::path resolved_path = absolute_path(path);
  const Entries entries = read_entries(resolved_path);
  const std::uint32_t version =
      positive_uint32(required(entries, "version", resolved_path), "version", resolved_path);
  if (version != 1U) {
    config_error("unsupported configuration version", resolved_path,
                 required(entries, "version", resolved_path).line);
  }
  const std::uint32_t node_count =
      positive_uint32(required(entries, "node_count", resolved_path), "node_count", resolved_path);
  if (node_count > 5U) {
    config_error("node_count must be in the range 1 through 5", resolved_path,
                 required(entries, "node_count", resolved_path).line);
  }
  Schema schema = parse_config_schema(entries, resolved_path);
  auto inputs = parse_inputs(entries, node_count, resolved_path);
  const std::uint32_t batch_bytes =
      optional_size(entries, "batch_bytes", default_batch_bytes, resolved_path);
  const std::uint32_t channel_bytes =
      optional_size(entries, "channel_bytes", default_channel_bytes, resolved_path);
  const std::uint32_t inbound_queue_batches =
      optional_size(entries, "inbound_queue_batches", default_inbound_queue_batches, resolved_path);
  const std::uint32_t max_record_bytes =
      optional_size(entries, "max_record_bytes", default_max_record_bytes, resolved_path);
  const std::uint32_t read_buffer_bytes =
      optional_size(entries, "read_buffer_bytes", default_read_buffer_bytes, resolved_path);
  if (max_record_bytes > batch_bytes || batch_bytes > channel_bytes ||
      max_record_bytes > read_buffer_bytes) {
    config_error("size ordering requires max_record_bytes <= batch_bytes <= channel_bytes and "
                 "max_record_bytes <= read_buffer_bytes",
                 resolved_path);
  }
  const std::uint64_t budget = memory_budget(node_count, read_buffer_bytes, batch_bytes,
                                             channel_bytes, inbound_queue_batches, resolved_path);
  return Config(version, node_count, std::move(schema), std::move(inputs), batch_bytes,
                channel_bytes, inbound_queue_batches, max_record_bytes, read_buffer_bytes, budget);
}

}
