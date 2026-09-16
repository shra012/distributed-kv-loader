/// Declares the streaming TSV record parser and its per-record callback contract.

#pragma once

#include "core_engine/schema.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

namespace core_engine {

/// Presents one parsed record: packed bytes, canonical key, and 1-based line number. The views
/// are valid only while the callback runs.
struct RecordView final {
  std::string_view record;
  std::string_view canonical_key;
  std::uint64_t line;
};

/// Receives one RecordView per valid line, in file order.
using RecordCallback = std::function<void(const RecordView&)>;

/// Streams path through a read_buffer_bytes buffer and calls callback for each record in order.
/// Throws Error(InputFile) if the file cannot be read, or Error(MalformedRecord) with file and
/// line for any line breaking the TSV format. Requires max_record_bytes <= read_buffer_bytes.
void parse_file(const std::filesystem::path& path, const Schema& schema,
                std::uint32_t max_record_bytes, std::uint32_t read_buffer_bytes,
                const RecordCallback& callback);

}
