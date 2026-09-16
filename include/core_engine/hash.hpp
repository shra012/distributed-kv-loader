/// Declares stable hashing and ownership selection for canonical key bytes.

#pragma once

#include <cstdint>
#include <string_view>

namespace core_engine {

/// Returns the FNV-1a 64-bit hash of the supplied bytes.
[[nodiscard]] std::uint64_t fnv1a_64(std::string_view bytes) noexcept;

/// Returns the Jump Consistent Hash bucket; bucket_count must be in [1, 5].
[[nodiscard]] std::uint32_t jump_consistent_hash(std::uint64_t hash, std::uint32_t bucket_count);

/// Returns the owner for canonical key bytes; node_count must be in [1, 5].
[[nodiscard]] std::uint32_t owner_of(std::string_view canonical_key, std::uint32_t node_count);

}
