/// Implements fixed-header wire frame encoding, validation, and decoding.

#include "core_engine/codec.hpp"

#include "core_engine/error.hpp"
#include "core_engine/hash.hpp"
#include "core_engine/packed_record.hpp"

#include <array>
#include <cstddef>

namespace core_engine {
namespace {

constexpr std::array<std::uint8_t, 4U> kMagic{0x43U, 0x45U, 0x31U, 0x00U};
constexpr std::uint8_t kVersion = 1U;

constexpr std::size_t kMagicOffset = 0U;
constexpr std::size_t kVersionOffset = 4U;
constexpr std::size_t kTypeOffset = 5U;
constexpr std::size_t kSourceOffset = 6U;
constexpr std::size_t kDestinationOffset = 7U;
constexpr std::size_t kSequenceOffset = 8U;
constexpr std::size_t kRecordCountOffset = 16U;
constexpr std::size_t kPayloadSizeOffset = 20U;
constexpr std::size_t kChecksumOffset = 24U;

[[noreturn]] void reject_codec(std::string detail) {
  throw Error(ErrorCode::Codec, std::move(detail));
}

std::uint8_t read_u8(std::string_view bytes, std::size_t offset) {
  return static_cast<std::uint8_t>(bytes[offset]);
}

}

std::string encode_frame_header(const Batch& batch, FrameType type) {
  const bool is_end = type == FrameType::End;
  const std::string_view payload = is_end ? std::string_view{} : std::string_view{batch.payload};
  const std::uint32_t record_count = is_end ? 0U : batch.record_count;

  std::string header;
  header.reserve(kFrameHeaderBytes);
  for (const std::uint8_t magic_byte : kMagic) {
    header.push_back(static_cast<char>(magic_byte));
  }
  header.push_back(static_cast<char>(kVersion));
  header.push_back(static_cast<char>(static_cast<std::uint8_t>(type)));
  header.push_back(static_cast<char>(batch.source));
  header.push_back(static_cast<char>(batch.destination));
  append_u64_be(header, batch.sequence);
  append_u32_be(header, record_count);
  append_u32_be(header, static_cast<std::uint32_t>(payload.size()));
  append_u64_be(header, fnv1a_64(payload));
  return header;
}

std::string encode_frame(const Batch& batch, FrameType type) {
  std::string frame = encode_frame_header(batch, type);
  if (type != FrameType::End) {
    frame.append(batch.payload);
  }
  return frame;
}

std::uint32_t peek_payload_size(std::string_view header) {
  if (header.size() < kFrameHeaderBytes) {
    reject_codec("frame header shorter than the fixed header");
  }
  return read_u32_be(header, kPayloadSizeOffset);
}

DecodedFrame decode_frame(std::string_view bytes, std::uint32_t max_payload_bytes) {
  if (bytes.size() < kFrameHeaderBytes) {
    reject_codec("frame shorter than the fixed header");
  }
  for (std::size_t index = 0U; index < kMagic.size(); ++index) {
    if (read_u8(bytes, kMagicOffset + index) != kMagic[index]) {
      reject_codec("frame magic mismatch");
    }
  }
  if (read_u8(bytes, kVersionOffset) != kVersion) {
    reject_codec("unsupported frame version");
  }
  const std::uint8_t raw_type = read_u8(bytes, kTypeOffset);
  if (raw_type != static_cast<std::uint8_t>(FrameType::Data) &&
      raw_type != static_cast<std::uint8_t>(FrameType::End)) {
    reject_codec("unknown frame type");
  }
  const std::uint8_t source = read_u8(bytes, kSourceOffset);
  const std::uint8_t destination = read_u8(bytes, kDestinationOffset);
  if (source > kMaxWireNodeId || destination > kMaxWireNodeId) {
    reject_codec("frame endpoint exceeds the maximum node identifier");
  }
  const std::uint64_t sequence = read_u64_be(bytes, kSequenceOffset);
  const std::uint32_t record_count = read_u32_be(bytes, kRecordCountOffset);
  const std::uint32_t payload_size = read_u32_be(bytes, kPayloadSizeOffset);
  const std::uint64_t checksum = read_u64_be(bytes, kChecksumOffset);

  if (payload_size > max_payload_bytes) {
    reject_codec("frame payload size exceeds the configured limit");
  }
  if (record_count > payload_size) {
    reject_codec("frame record count exceeds its declared payload size");
  }
  if (raw_type == static_cast<std::uint8_t>(FrameType::End) &&
      (record_count != 0U || payload_size != 0U)) {
    reject_codec("END frame must have an empty payload and zero records");
  }
  const std::uint64_t declared_total =
      static_cast<std::uint64_t>(kFrameHeaderBytes) + static_cast<std::uint64_t>(payload_size);
  if (declared_total != static_cast<std::uint64_t>(bytes.size())) {
    reject_codec("frame length does not match its declared payload size");
  }
  const std::string_view payload = bytes.substr(kFrameHeaderBytes, payload_size);
  if (fnv1a_64(payload) != checksum) {
    reject_codec("frame checksum mismatch");
  }

  return DecodedFrame{
      static_cast<FrameType>(raw_type), source, destination, sequence, record_count, payload};
}

}
