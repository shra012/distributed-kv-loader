/// Verifies atomic per-node statistics collection.

#include "core_engine/stats.hpp"
#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

CORE_ENGINE_TEST(stats_01, "STATS-01") {
  static_cast<void>(test_context);
  constexpr std::size_t thread_count = 8U;
  constexpr std::size_t increments_per_thread = 10'000U;
  constexpr auto counter_count = static_cast<std::size_t>(core_engine::Counter::Count);
  core_engine::NodeCounters counters;
  std::vector<std::thread> workers;
  workers.reserve(thread_count);

  for (std::size_t thread = 0U; thread < thread_count; ++thread) {
    workers.emplace_back([&counters]() {
      for (std::size_t increment = 0U; increment < increments_per_thread; ++increment) {
        for (std::size_t counter = 0U; counter < counter_count; ++counter) {
          counters.add(static_cast<core_engine::Counter>(counter));
        }
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }

  constexpr std::uint64_t expected =
      static_cast<std::uint64_t>(thread_count * increments_per_thread);
  const core_engine::NodeStats snapshot = counters.snapshot();
  CORE_ASSERT_EQ(snapshot.lines_read, expected);
  CORE_ASSERT_EQ(snapshot.parsed, expected);
  CORE_ASSERT_EQ(snapshot.local, expected);
  CORE_ASSERT_EQ(snapshot.remote_out, expected);
  CORE_ASSERT_EQ(snapshot.frames_out, expected);
  CORE_ASSERT_EQ(snapshot.bytes_out, expected);
  CORE_ASSERT_EQ(snapshot.remote_in, expected);
  CORE_ASSERT_EQ(snapshot.ingested, expected);
  CORE_ASSERT_EQ(snapshot.loaded, expected);
  CORE_ASSERT_EQ(snapshot.duplicates, expected);
  CORE_ASSERT_EQ(snapshot.frames_in, expected);
  CORE_ASSERT_EQ(snapshot.bytes_in, expected);
  CORE_ASSERT_EQ(snapshot.end_markers_received, expected);
  CORE_ASSERT_EQ(snapshot.malformed, expected);
  CORE_ASSERT_EQ(snapshot.transport, expected);
  CORE_ASSERT_EQ(snapshot.codec, expected);
  CORE_ASSERT_EQ(snapshot.conflict, expected);
}
