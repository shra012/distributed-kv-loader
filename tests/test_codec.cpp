/// Verifies frame encode/decode round trips, validation order, and checksum enforcement.

#include "core_engine/batch.hpp"
#include "core_engine/codec.hpp"
#include "core_engine/error.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>

namespace {

using core_engine::Batch;
using core_engine::BatchBuilder;
using core_engine::decode_frame;
using core_engine::DecodedFrame;
using core_engine::encode_frame;
using core_engine::Error;
using core_engine::ErrorCode;
using core_engine::FrameType;
using core_engine::kFrameHeaderBytes;

constexpr std::size_t kMagicOffset = 0U;
constexpr std::size_t kVersionOffset = 4U;
constexpr std::size_t kTypeOffset = 5U;
constexpr std::size_t kSourceOffset = 6U;
constexpr std::size_t kDestinationOffset = 7U;
constexpr std::size_t kRecordCountOffset = 16U;
constexpr std::size_t kPayloadSizeOffset = 20U;
constexpr std::size_t kChecksumOffset = 24U;

void write_u32_be(std::string& out, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t shift = (3U - index) * 8U;
    out[offset + index] = static_cast<char>((value >> shift) & 0xFFU);
  }
}

void expect_codec_error(const std::string& frame) {
  bool threw = false;
  try {
    static_cast<void>(decode_frame(frame));
  } catch (const Error& error) {
    threw = true;
    CORE_ASSERT_TRUE(error.code() == ErrorCode::Codec);
  }
  CORE_ASSERT_TRUE(threw);
}

Batch make_batch(std::uint8_t source, std::uint8_t destination, std::uint64_t sequence,
                 std::string payload, std::uint32_t record_count) {
  return Batch{source, destination, sequence, record_count, std::move(payload)};
}

void assert_round_trip(const Batch& batch, FrameType type) {
  const std::string frame = encode_frame(batch, type);
  const DecodedFrame decoded = decode_frame(frame);
  CORE_ASSERT_TRUE(decoded.type == type);
  CORE_ASSERT_EQ(decoded.source, batch.source);
  CORE_ASSERT_EQ(decoded.destination, batch.destination);
  CORE_ASSERT_EQ(decoded.sequence, batch.sequence);
  if (type == FrameType::End) {
    CORE_ASSERT_EQ(decoded.record_count, 0U);
    CORE_ASSERT_TRUE(decoded.payload.empty());
  } else {
    CORE_ASSERT_EQ(decoded.record_count, batch.record_count);
    CORE_ASSERT_TRUE(decoded.payload == batch.payload);
  }
}

}

CORE_ENGINE_TEST(codec_01, "COD-01") {
  static_cast<void>(test_context);
  assert_round_trip(make_batch(0U, 1U, 1U, std::string{}, 0U), FrameType::Data);
  assert_round_trip(make_batch(1U, 2U, 7U, "single-record-payload", 1U), FrameType::Data);
  assert_round_trip(make_batch(2U, 3U, 9U, std::string{}, 0U), FrameType::End);

  BatchBuilder builder(0U, 4U, 16U);
  builder.append("0123456789012345");
  CORE_ASSERT_FALSE(builder.fits(1U));
  const Batch full_batch = builder.take(3U);
  CORE_ASSERT_EQ(full_batch.payload.size(), 16U);
  assert_round_trip(full_batch, FrameType::Data);
}

CORE_ENGINE_TEST(codec_02, "COD-02") {
  static_cast<void>(test_context);
  std::string payload;
  payload.push_back('\0');
  payload.append("string-field");
  payload.push_back('\0');
  payload.push_back('\0');
  payload.push_back('\0');
  payload.push_back(static_cast<char>(0x2AU));
  payload.push_back(static_cast<char>(0xFFU));
  payload.push_back(static_cast<char>(0x80U));
  assert_round_trip(make_batch(3U, 0U, 42U, payload, 2U), FrameType::Data);
}

CORE_ENGINE_TEST(codec_03, "COD-03") {
  static_cast<void>(test_context);
  const std::string valid = encode_frame(make_batch(0U, 1U, 1U, "payload", 1U), FrameType::Data);

  std::string bad_magic = valid;
  bad_magic[kMagicOffset] = static_cast<char>(0x00U);
  expect_codec_error(bad_magic);

  std::string bad_version = valid;
  bad_version[kVersionOffset] = static_cast<char>(0x02U);
  expect_codec_error(bad_version);

  std::string bad_type = valid;
  bad_type[kTypeOffset] = static_cast<char>(0x03U);
  expect_codec_error(bad_type);

  std::string bad_source = valid;
  bad_source[kSourceOffset] = static_cast<char>(0x05U);
  expect_codec_error(bad_source);

  std::string bad_destination = valid;
  bad_destination[kDestinationOffset] = static_cast<char>(0x05U);
  expect_codec_error(bad_destination);

  std::string nonempty_end = valid;
  nonempty_end[kTypeOffset] = static_cast<char>(FrameType::End);
  expect_codec_error(nonempty_end);
}

CORE_ENGINE_TEST(codec_04, "COD-04") {
  static_cast<void>(test_context);
  const std::string valid = encode_frame(make_batch(0U, 1U, 1U, "payload", 1U), FrameType::Data);

  const std::string truncated_header = valid.substr(0U, kFrameHeaderBytes - 1U);
  expect_codec_error(truncated_header);

  const std::string truncated_payload = valid.substr(0U, valid.size() - 1U);
  expect_codec_error(truncated_payload);
}

CORE_ENGINE_TEST(codec_05, "COD-05") {
  static_cast<void>(test_context);
  std::string overflow_payload_size =
      encode_frame(make_batch(0U, 1U, 1U, "payload", 1U), FrameType::Data);
  write_u32_be(overflow_payload_size, kPayloadSizeOffset, 0xFFFFFFFFU);
  expect_codec_error(overflow_payload_size);

  std::string overflow_record_count =
      encode_frame(make_batch(0U, 1U, 1U, "payload", 1U), FrameType::Data);
  write_u32_be(overflow_record_count, kRecordCountOffset, 0xFFFFFFFFU);
  expect_codec_error(overflow_record_count);

  std::string trailing_bytes = encode_frame(make_batch(0U, 1U, 1U, "payload", 1U), FrameType::Data);
  trailing_bytes.push_back('Z');
  expect_codec_error(trailing_bytes);
}

CORE_ENGINE_TEST(codec_06, "COD-06") {
  static_cast<void>(test_context);
  std::string flipped_payload =
      encode_frame(make_batch(0U, 1U, 1U, "payload", 1U), FrameType::Data);
  flipped_payload[kFrameHeaderBytes] =
      static_cast<char>(static_cast<unsigned char>(flipped_payload[kFrameHeaderBytes]) ^ 0xFFU);
  expect_codec_error(flipped_payload);

  std::string flipped_checksum =
      encode_frame(make_batch(0U, 1U, 1U, "payload", 1U), FrameType::Data);
  flipped_checksum[kChecksumOffset] =
      static_cast<char>(static_cast<unsigned char>(flipped_checksum[kChecksumOffset]) ^ 0xFFU);
  expect_codec_error(flipped_checksum);
}

CORE_ENGINE_TEST(codec_07, "COD-07") {
  std::mt19937_64 generator(test_context.seed("round-trip"));
  std::uniform_int_distribution<std::uint32_t> length_distribution(0U, 64U);
  std::uniform_int_distribution<int> byte_distribution(0, 255);
  std::uniform_int_distribution<std::uint32_t> node_distribution(0U, 4U);
  std::uniform_int_distribution<std::uint32_t> type_distribution(0U, 1U);

  for (std::size_t iteration = 0U; iteration < 10000U; ++iteration) {
    std::string payload;
    const std::uint32_t length = length_distribution(generator);
    payload.reserve(length);
    for (std::uint32_t index = 0U; index < length; ++index) {
      payload.push_back(static_cast<char>(byte_distribution(generator)));
    }
    const FrameType type = type_distribution(generator) == 0U ? FrameType::Data : FrameType::End;
    const Batch batch = make_batch(static_cast<std::uint8_t>(node_distribution(generator)),
                                   static_cast<std::uint8_t>(node_distribution(generator)),
                                   generator(), std::move(payload), length == 0U ? 0U : 1U);
    assert_round_trip(batch, type);
  }
}
