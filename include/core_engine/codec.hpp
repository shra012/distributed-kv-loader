/// Declares the fixed-header wire frame format and its encode/decode functions.

#pragma once

#include "core_engine/batch.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace core_engine {

/// Identifies the wire meaning of a frame's payload.
enum class FrameType : std::uint8_t { Data = 0x01, End = 0x02 };

/// Number of header bytes preceding every frame's payload.
inline constexpr std::size_t kFrameHeaderBytes = 32U;

/// Codec-level ceiling on a frame's payload size, independent of any configured batch_bytes.
inline constexpr std::uint32_t kMaxFramePayloadBytes = 64U * 1024U * 1024U;

/// Largest node identifier accepted on the wire, matching the five-node cluster cap.
inline constexpr std::uint8_t kMaxWireNodeId = 4U;

/// Holds decoded header fields and a payload view borrowed from the encoded frame.
struct DecodedFrame final {
  FrameType type;
  std::uint8_t source;
  std::uint8_t destination;
  std::uint64_t sequence;
  std::uint32_t record_count;
  std::string_view payload;
};

/// Encodes only the fixed header of the frame encode_frame would produce, so a sender can
/// transmit the batch payload in place without copying it into a frame buffer.
[[nodiscard]] std::string encode_frame_header(const Batch& batch, FrameType type);

/// Encodes a batch as one complete frame; an End frame carries an empty payload regardless of
/// the batch's own payload field.
[[nodiscard]] std::string encode_frame(const Batch& batch, FrameType type);

/// Reads the declared payload size from a frame's fixed header alone, before the payload has
/// arrived. Throws Error(Codec) if header is shorter than kFrameHeaderBytes.
[[nodiscard]] std::uint32_t peek_payload_size(std::string_view header);

/// Decodes and fully validates one frame without copying its payload. The returned view remains
/// valid while bytes remains alive and unchanged. max_payload_bytes applies the caller's limit.
[[nodiscard]] DecodedFrame decode_frame(std::string_view bytes,
                                        std::uint32_t max_payload_bytes = kMaxFramePayloadBytes);

}
