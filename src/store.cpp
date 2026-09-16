/// Implements the single-writer per-node record store and its ownership scan.

#include "core_engine/store.hpp"

#include "core_engine/error.hpp"
#include "core_engine/hash.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace core_engine {
namespace {

constexpr std::size_t kMaxReserveHint = 50'000'000U;
constexpr std::size_t kInitialCapacity = 1024U;
constexpr unsigned kInitialBits = 10U;
constexpr std::size_t kArenaBlockBytes = 4U * 1024U * 1024U;
constexpr std::uint64_t kFibonacciMultiplier = UINT64_C(0x9E3779B97F4A7C15);
constexpr std::uint64_t kWordMixer = UINT64_C(0xBF58476D1CE4E5B9);
constexpr std::uint64_t kTailMixer = UINT64_C(0x94D049BB133111EB);

[[nodiscard]] bool exceeds_load(std::size_t entries, std::size_t capacity) noexcept {
  return entries * 10U > capacity * 7U;
}

[[nodiscard]] std::uint64_t table_hash(std::string_view key) noexcept {
  std::uint64_t hash = kFibonacciMultiplier ^ static_cast<std::uint64_t>(key.size());
  std::size_t offset = 0U;
  for (; offset + 8U <= key.size(); offset += 8U) {
    std::uint64_t word = 0U;
    std::memcpy(&word, key.data() + offset, 8U);
    hash = (hash ^ word) * kWordMixer;
    hash ^= hash >> 31U;
  }
  std::uint64_t tail = 0U;
  std::memcpy(&tail, key.data() + offset, key.size() - offset);
  hash = (hash ^ tail) * kTailMixer;
  return hash ^ (hash >> 32U);
}

}

Store::Store(std::uint32_t node_id, std::uint32_t node_count) noexcept
    : node_id_(node_id), node_count_(node_count) {}

void Store::reserve(std::size_t hint) {
  const std::size_t entries = std::min(hint, kMaxReserveHint);
  grow(entries + entries / 2U + 1U);
}

std::size_t Store::home(std::uint64_t hash) const noexcept {
  return static_cast<std::size_t>((hash * kFibonacciMultiplier) >> shift_);
}

std::size_t Store::probe(std::string_view canonical_key, std::uint64_t hash) const noexcept {
  const std::size_t mask = slots_.size() - 1U;
  std::size_t index = home(hash);
  while (true) {
    const Slot& slot = slots_[index];
    if (slot.data == nullptr ||
        (slot.hash == hash && slot.key_length == canonical_key.size() &&
         std::memcmp(slot.data, canonical_key.data(), canonical_key.size()) == 0)) {
      return index;
    }
    index = (index + 1U) & mask;
  }
}

void Store::grow(std::size_t minimum_capacity) {
  std::size_t capacity = kInitialCapacity;
  unsigned bits = kInitialBits;
  while (capacity < minimum_capacity) {
    capacity <<= 1U;
    ++bits;
  }
  if (capacity <= slots_.size()) {
    return;
  }
  const std::vector<Slot> previous = std::exchange(slots_, std::vector<Slot>(capacity));
  shift_ = 64U - bits;
  const std::size_t mask = capacity - 1U;
  for (const Slot& slot : previous) {
    if (slot.data == nullptr) {
      continue;
    }
    std::size_t index = home(slot.hash);
    while (slots_[index].data != nullptr) {
      index = (index + 1U) & mask;
    }
    slots_[index] = slot;
  }
}

const char* Store::copy_to_arena(std::string_view canonical_key, std::string_view record) {
  const std::size_t bytes = canonical_key.size() + record.size();
  if (bytes > arena_remaining_) {
    const std::size_t block_bytes = std::max(kArenaBlockBytes, bytes);
    arena_blocks_.push_back(std::unique_ptr<char[]>(new char[block_bytes]));
    arena_cursor_ = arena_blocks_.back().get();
    arena_remaining_ = block_bytes;
  }
  char* const destination = arena_cursor_;
  std::memcpy(destination, canonical_key.data(), canonical_key.size());
  std::memcpy(destination + canonical_key.size(), record.data(), record.size());
  arena_cursor_ += bytes;
  arena_remaining_ -= bytes;
  return destination;
}

void Store::insert(std::string_view canonical_key, std::string_view record,
                   std::optional<std::uint32_t> node, std::string_view file,
                   std::optional<std::uint64_t> line) {
  constexpr std::size_t length_limit = std::numeric_limits<std::uint32_t>::max();
  if (canonical_key.size() > length_limit || record.size() > length_limit) {
    throw Error(ErrorCode::InternalInvariant, "record exceeds the store length limit", node_id_);
  }
  if (slots_.empty() || exceeds_load(size_ + 1U, slots_.size())) {
    grow(slots_.size() * 2U);
  }
  const std::uint64_t hash = table_hash(canonical_key);
  Slot& slot = slots_[probe(canonical_key, hash)];
  if (slot.data == nullptr) {
    slot = Slot{hash, copy_to_arena(canonical_key, record),
                static_cast<std::uint32_t>(canonical_key.size()),
                static_cast<std::uint32_t>(record.size())};
    ++size_;
    return;
  }
  if (std::string_view(slot.data + slot.key_length, slot.record_length) == record) {
    ++duplicate_count_;
    return;
  }
  throw Error(ErrorCode::ConflictingDuplicate, "key already stored with different values",
              node.has_value() ? node : std::optional<std::uint32_t>(node_id_), std::string(file),
              line);
}

std::size_t Store::size() const noexcept { return size_; }

std::size_t Store::duplicate_count() const noexcept { return duplicate_count_; }

std::optional<std::string_view> Store::find(std::string_view canonical_key) const {
  if (slots_.empty()) {
    return std::nullopt;
  }
  const Slot& slot = slots_[probe(canonical_key, table_hash(canonical_key))];
  if (slot.data == nullptr) {
    return std::nullopt;
  }
  return std::string_view(slot.data + slot.key_length, slot.record_length);
}

Store::OwnershipScanResult Store::scan_ownership() const {
  OwnershipScanResult result{0U, 0U};
  for (const Slot& slot : slots_) {
    if (slot.data == nullptr) {
      continue;
    }
    ++result.scanned;
    if (owner_of(std::string_view(slot.data, slot.key_length), node_count_) != node_id_) {
      ++result.misowned;
    }
  }
  return result;
}

}
