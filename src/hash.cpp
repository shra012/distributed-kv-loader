/// Implements stable hashing and ownership selection for canonical key bytes.

#include "core_engine/hash.hpp"

#include <stdexcept>

namespace core_engine {

std::uint64_t fnv1a_64(std::string_view bytes) noexcept {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const char byte : bytes) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

std::uint32_t jump_consistent_hash(std::uint64_t hash, std::uint32_t bucket_count) {
  if (bucket_count == 0U || bucket_count > 5U) {
    throw std::invalid_argument("bucket count must be in [1, 5]");
  }

  std::int64_t bucket = -1;
  std::int64_t candidate = 0;
  while (candidate < static_cast<std::int64_t>(bucket_count)) {
    bucket = candidate;
    hash = hash * UINT64_C(2862933555777941757) + UINT64_C(1);
    const double numerator = static_cast<double>(bucket + 1) * 2147483648.0;
    const double denominator = static_cast<double>((hash >> 33U) + UINT64_C(1));
    candidate = static_cast<std::int64_t>(numerator / denominator);
  }
  return static_cast<std::uint32_t>(bucket);
}

std::uint32_t owner_of(std::string_view canonical_key, std::uint32_t node_count) {
  return jump_consistent_hash(fnv1a_64(canonical_key), node_count);
}

}
