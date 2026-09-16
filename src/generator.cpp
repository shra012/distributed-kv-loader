/// Implements deterministic input generation and manifest serialization.

#include "core_engine/generator.hpp"

#include "core_engine/decimal.hpp"
#include "core_engine/error.hpp"
#include "core_engine/hash.hpp"
#include "core_engine/packed_record.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace core_engine {
namespace {

constexpr std::size_t max_duplicate_pool = 100'000U;
constexpr std::size_t flush_threshold_bytes = 65536U;
constexpr std::size_t min_string_length = 4U;
constexpr std::size_t max_string_length = 32U;
constexpr std::array<char, 62> alphanumeric_charset{
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};

struct GeneratedValue final {
  bool is_string;
  std::string text;
  std::int32_t number{0};
};

[[noreturn]] void generator_error(std::string detail, const std::filesystem::path& path) {
  throw Error(ErrorCode::InputFile, std::move(detail), std::nullopt, path.string());
}

std::string random_string(std::mt19937_64& rng) {
  std::uniform_int_distribution<std::size_t> length_dist(min_string_length, max_string_length);
  std::uniform_int_distribution<std::size_t> char_dist(0U, alphanumeric_charset.size() - 1U);
  std::string value(length_dist(rng), '\0');
  for (char& character : value) {
    character = alphanumeric_charset[char_dist(rng)];
  }
  return value;
}

std::int32_t random_int32(std::mt19937_64& rng) {
  std::uniform_int_distribution<std::int32_t> dist(std::numeric_limits<std::int32_t>::min(),
                                                   std::numeric_limits<std::int32_t>::max());
  return dist(rng);
}

GeneratedValue random_field(FieldType type, std::mt19937_64& rng) {
  if (type == FieldType::String) {
    return GeneratedValue{true, random_string(rng), 0};
  }
  return GeneratedValue{false, {}, random_int32(rng)};
}

std::string format_field(const GeneratedValue& value) {
  return value.is_string ? value.text : std::to_string(value.number);
}

std::string canonical_key_bytes(const GeneratedValue& key_value) {
  std::string value = key_value.text;
  if (!key_value.is_string) {
    static_cast<void>(append_int32_field(value, key_value.number));
  }
  std::string key;
  assign_canonical_key(key, key_value.is_string ? kStringKeyTag : kInt32KeyTag, value);
  return key;
}

std::string format_line(const std::vector<GeneratedValue>& fields) {
  std::string line;
  for (std::size_t index = 0U; index < fields.size(); ++index) {
    if (index != 0U) {
      line.push_back('\t');
    }
    line.append(format_field(fields[index]));
  }
  line.push_back('\n');
  return line;
}

class RecordGenerator final {
public:
  RecordGenerator(const Schema& schema, std::uint64_t seed, std::uint32_t node_count)
      : schema_(schema), seed_(seed), rng_(seed), expected_per_node_(node_count, 0U) {}

  std::string generate_new_line() {
    std::vector<GeneratedValue> fields;
    fields.reserve(schema_.fields().size());
    for (const Field& field : schema_.fields()) {
      fields.push_back(random_field(field.type, rng_));
    }
    fields[schema_.key_index()] = unique_key(schema_.key_field().type);
    const std::string canonical = canonical_key_bytes(fields[schema_.key_index()]);
    std::mt19937_64 value_rng(fnv1a_64(canonical) ^ seed_);
    for (std::size_t index = 0U; index < fields.size(); ++index) {
      if (index == schema_.key_index()) {
        continue;
      }
      fields[index] = random_field(schema_.fields()[index].type, value_rng);
    }
    const std::uint32_t owner =
        owner_of(canonical, static_cast<std::uint32_t>(expected_per_node_.size()));
    ++expected_per_node_[owner];
    ++unique_count_;
    const std::string line = format_line(fields);
    remember(line);
    return line;
  }

  bool has_duplicate_candidate() const noexcept { return !reservoir_.empty(); }

  const std::string& pick_duplicate_line(std::mt19937_64& rng) {
    std::uniform_int_distribution<std::size_t> index_dist(0U, reservoir_.size() - 1U);
    return reservoir_[index_dist(rng)];
  }

  [[nodiscard]] std::uint64_t unique_count() const noexcept { return unique_count_; }

  [[nodiscard]] const std::vector<std::uint64_t>& expected_per_node() const noexcept {
    return expected_per_node_;
  }

private:
  [[nodiscard]] GeneratedValue unique_key(FieldType type) const {
    if (type == FieldType::String) {
      return GeneratedValue{true, "k" + std::to_string(seed_) + "_" + std::to_string(unique_count_),
                            0};
    }
    constexpr std::uint64_t range = UINT64_C(1) << 32U;
    if (unique_count_ >= range) {
      throw Error(ErrorCode::InternalInvariant, "int32 key space exhausted");
    }
    const std::uint64_t offset = (seed_ + unique_count_) % range;
    const std::int64_t signed_value =
        static_cast<std::int64_t>(offset) + std::numeric_limits<std::int32_t>::min();
    return GeneratedValue{false, {}, static_cast<std::int32_t>(signed_value)};
  }

  void remember(const std::string& line) {
    if (reservoir_.size() < max_duplicate_pool) {
      reservoir_.push_back(line);
      return;
    }
    std::uniform_int_distribution<std::uint64_t> replace_dist(0U, unique_count_ - 1U);
    const std::uint64_t slot = replace_dist(rng_);
    if (slot < max_duplicate_pool) {
      reservoir_[static_cast<std::size_t>(slot)] = line;
    }
  }

  const Schema& schema_;
  std::uint64_t seed_;
  std::mt19937_64 rng_;
  std::vector<std::string> reservoir_;
  std::vector<std::uint64_t> expected_per_node_;
  std::uint64_t unique_count_{0U};
};

class BufferedWriter final {
public:
  explicit BufferedWriter(const std::filesystem::path& path) : path_(path), stream_(path) {
    if (!stream_.is_open()) {
      generator_error("cannot open generated input file for writing", path);
    }
  }

  void append(const std::string& line) {
    buffer_.append(line);
    if (buffer_.size() >= flush_threshold_bytes) {
      flush();
    }
  }

  void finish() {
    flush();
    stream_.close();
    if (stream_.fail()) {
      generator_error("failed while closing generated input file", path_);
    }
  }

private:
  void flush() {
    if (buffer_.empty()) {
      return;
    }
    stream_.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    if (stream_.fail()) {
      generator_error("failed while writing generated input file", path_);
    }
    buffer_.clear();
  }

  std::filesystem::path path_;
  std::ofstream stream_;
  std::string buffer_;
};

}

Manifest generate(const Config& config, std::uint64_t seed, std::uint64_t bytes_per_file,
                  std::uint32_t duplicate_percent) {
  if (duplicate_percent > 100U) {
    throw Error(ErrorCode::InternalInvariant, "duplicate_percent must not exceed 100");
  }

  std::vector<std::pair<std::uint32_t, std::filesystem::path>> active_nodes;
  for (std::uint32_t node = 0U; node < config.node_count(); ++node) {
    const std::optional<std::filesystem::path>& path = config.input_path(node);
    if (path.has_value()) {
      active_nodes.emplace_back(node, *path);
    }
  }

  RecordGenerator generator(config.schema(), seed, config.node_count());
  std::mt19937_64 selection_rng(seed ^ UINT64_C(0x9e3779b97f4a7c15));
  std::uniform_int_distribution<std::uint32_t> percent_dist(0U, 99U);
  std::uint64_t duplicates = 0U;
  std::uint64_t lines = 0U;

  for (const auto& [node, path] : active_nodes) {
    std::error_code directory_error;
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path(), directory_error);
    }
    if (directory_error) {
      generator_error("cannot create generated input directory", path);
    }
    BufferedWriter writer(path);
    std::uint64_t bytes_written = 0U;
    while (bytes_written < bytes_per_file) {
      const bool duplicate = duplicate_percent > 0U && generator.has_duplicate_candidate() &&
                             percent_dist(selection_rng) < duplicate_percent;
      std::string line;
      if (duplicate) {
        line = generator.pick_duplicate_line(selection_rng);
        ++duplicates;
      } else {
        line = generator.generate_new_line();
      }
      bytes_written += line.size();
      ++lines;
      writer.append(line);
    }
    writer.finish();
  }

  return Manifest{1U,
                  seed,
                  bytes_per_file,
                  duplicate_percent,
                  static_cast<std::uint32_t>(active_nodes.size()),
                  lines,
                  generator.unique_count(),
                  duplicates,
                  generator.expected_per_node()};
}

void write_manifest(const Manifest& manifest, const std::filesystem::path& path) {
  std::uint64_t expected_total = 0U;
  for (const std::uint64_t value : manifest.expected_per_node) {
    if (expected_total > std::numeric_limits<std::uint64_t>::max() - value) {
      generator_error("manifest expected total overflow", path);
    }
    expected_total += value;
  }
  if (manifest.version != 1U || manifest.duplicate_percent > 100U || manifest.files > 5U ||
      manifest.lines < manifest.unique || manifest.lines - manifest.unique != manifest.duplicates ||
      manifest.expected_per_node.empty() || manifest.expected_per_node.size() > 5U ||
      expected_total != manifest.unique) {
    generator_error("cannot write inconsistent manifest", path);
  }
  std::error_code directory_error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), directory_error);
  }
  if (directory_error) {
    generator_error("cannot create manifest directory", path);
  }
  std::ofstream stream(path);
  if (!stream.is_open()) {
    generator_error("cannot open manifest file for writing", path);
  }
  stream << "version=" << manifest.version << '\n';
  stream << "seed=" << manifest.seed << '\n';
  stream << "bytes_per_file=" << manifest.bytes_per_file << '\n';
  stream << "duplicate_percent=" << manifest.duplicate_percent << '\n';
  stream << "files=" << manifest.files << '\n';
  stream << "lines=" << manifest.lines << '\n';
  stream << "unique=" << manifest.unique << '\n';
  stream << "duplicates=" << manifest.duplicates << '\n';
  for (std::size_t node = 0U; node < manifest.expected_per_node.size(); ++node) {
    stream << "expected." << node << '=' << manifest.expected_per_node[node] << '\n';
  }
  if (stream.fail()) {
    generator_error("failed while writing manifest file", path);
  }
}

namespace {

std::uint64_t parse_manifest_value(const std::string& text, const std::filesystem::path& path) {
  const std::optional<std::uint64_t> value = parse_decimal(text);
  if (!value.has_value()) {
    generator_error("invalid manifest value", path);
  }
  return *value;
}

void set_manifest_value(std::optional<std::uint64_t>& target, const std::string& text,
                        const std::filesystem::path& path) {
  if (target.has_value()) {
    generator_error("duplicate manifest key", path);
  }
  target = parse_manifest_value(text, path);
}

}

Manifest read_manifest(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    generator_error("cannot open manifest file for reading", path);
  }

  std::optional<std::uint64_t> version;
  std::optional<std::uint64_t> seed;
  std::optional<std::uint64_t> bytes_per_file;
  std::optional<std::uint64_t> duplicate_percent;
  std::optional<std::uint64_t> files;
  std::optional<std::uint64_t> lines;
  std::optional<std::uint64_t> unique;
  std::optional<std::uint64_t> duplicates;
  std::vector<std::optional<std::uint64_t>> expected;

  std::string line;
  while (std::getline(stream, line)) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      generator_error("malformed manifest line", path);
    }
    const std::string key = line.substr(0U, separator);
    const std::string value_text = line.substr(separator + 1U);
    if (key == "version") {
      set_manifest_value(version, value_text, path);
    } else if (key == "seed") {
      set_manifest_value(seed, value_text, path);
    } else if (key == "bytes_per_file") {
      set_manifest_value(bytes_per_file, value_text, path);
    } else if (key == "duplicate_percent") {
      set_manifest_value(duplicate_percent, value_text, path);
    } else if (key == "files") {
      set_manifest_value(files, value_text, path);
    } else if (key == "lines") {
      set_manifest_value(lines, value_text, path);
    } else if (key == "unique") {
      set_manifest_value(unique, value_text, path);
    } else if (key == "duplicates") {
      set_manifest_value(duplicates, value_text, path);
    } else if (key.rfind("expected.", 0U) == 0U) {
      const std::uint64_t node = parse_manifest_value(key.substr(9U), path);
      if (node >= 5U) {
        generator_error("manifest node index exceeds limit", path);
      }
      if (node >= expected.size()) {
        expected.resize(node + 1U);
      }
      if (expected[node].has_value()) {
        generator_error("duplicate expected.N manifest key", path);
      }
      expected[node] = parse_manifest_value(value_text, path);
    } else {
      generator_error("unknown manifest key", path);
    }
  }

  if (stream.bad()) {
    generator_error("failed while reading manifest file", path);
  }

  if (!version.has_value() || !seed.has_value() || !bytes_per_file.has_value() ||
      !duplicate_percent.has_value() || !files.has_value() || !lines.has_value() ||
      !unique.has_value() || !duplicates.has_value() || expected.empty()) {
    generator_error("manifest missing required keys", path);
  }
  if (*version != 1U || *duplicate_percent > 100U || *files > 5U || *lines < *unique ||
      *lines - *unique != *duplicates) {
    generator_error("inconsistent manifest totals", path);
  }
  std::vector<std::uint64_t> expected_per_node;
  expected_per_node.reserve(expected.size());
  for (const std::optional<std::uint64_t>& value : expected) {
    if (!value.has_value()) {
      generator_error("non-contiguous expected.N manifest keys", path);
    }
    expected_per_node.push_back(*value);
  }
  std::uint64_t expected_total = 0U;
  for (const std::uint64_t value : expected_per_node) {
    if (expected_total > std::numeric_limits<std::uint64_t>::max() - value) {
      generator_error("manifest expected total overflow", path);
    }
    expected_total += value;
  }
  if (expected_total != *unique) {
    generator_error("manifest expected totals do not equal unique", path);
  }

  return Manifest{static_cast<std::uint32_t>(*version),
                  *seed,
                  *bytes_per_file,
                  static_cast<std::uint32_t>(*duplicate_percent),
                  static_cast<std::uint32_t>(*files),
                  *lines,
                  *unique,
                  *duplicates,
                  std::move(expected_per_node)};
}

}
