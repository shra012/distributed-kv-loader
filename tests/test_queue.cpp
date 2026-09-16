/// Verifies bounded queue capacity, closure, cancellation, and waiter wakeups.

#include "core_engine/bounded_queue.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <stdexcept>

namespace {

using core_engine::BoundedQueue;
using core_engine::CancelToken;
using core_engine::QueueStatus;
using namespace std::chrono_literals;

void test_capacity_fifo_and_producer_release() {
  CancelToken zero_capacity_token;
  CORE_ASSERT_THROW(static_cast<void>(BoundedQueue<int>(0U, zero_capacity_token)),
                    std::invalid_argument);

  CancelToken token;
  BoundedQueue<int> queue(2U, token);
  CORE_ASSERT_EQ(queue.capacity(), 2U);
  CORE_ASSERT_EQ(queue.push(10), QueueStatus::Success);
  CORE_ASSERT_EQ(queue.push(20), QueueStatus::Success);
  CORE_ASSERT_EQ(queue.size(), 2U);

  std::promise<void> producer_started;
  auto producer_result = std::async(std::launch::async, [&queue, &producer_started] {
    producer_started.set_value();
    return queue.push(30);
  });
  producer_started.get_future().wait();
  CORE_ASSERT_EQ(producer_result.wait_for(50ms), std::future_status::timeout);

  int value = 0;
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Success);
  CORE_ASSERT_EQ(value, 10);
  CORE_ASSERT_EQ(producer_result.wait_for(1s), std::future_status::ready);
  CORE_ASSERT_EQ(producer_result.get(), QueueStatus::Success);
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Success);
  CORE_ASSERT_EQ(value, 20);
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Success);
  CORE_ASSERT_EQ(value, 30);
}

void test_closed_queue_contract() {
  CancelToken token;
  BoundedQueue<int> queue(1U, token);
  int value = 0;
  CORE_ASSERT_EQ(queue.push(40), QueueStatus::Success);
  CORE_ASSERT_TRUE(queue.close());
  CORE_ASSERT_FALSE(queue.close());
  CORE_ASSERT_TRUE(queue.is_closed());
  CORE_ASSERT_EQ(queue.push(50), QueueStatus::Closed);
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Success);
  CORE_ASSERT_EQ(value, 40);
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Closed);
}

void test_close_wakes_consumer() {
  CancelToken token;
  BoundedQueue<int> queue(1U, token);
  auto result = std::async(std::launch::async, [&queue] {
    int output = 0;
    return queue.pop(output);
  });
  CORE_ASSERT_EQ(result.wait_for(50ms), std::future_status::timeout);
  CORE_ASSERT_TRUE(queue.close());
  CORE_ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
  CORE_ASSERT_EQ(result.get(), QueueStatus::Closed);
}

void test_close_wakes_producer_without_loss() {
  CancelToken token;
  BoundedQueue<int> queue(1U, token);
  CORE_ASSERT_EQ(queue.push(50), QueueStatus::Success);
  auto result = std::async(std::launch::async, [&queue] { return queue.push(60); });
  CORE_ASSERT_EQ(result.wait_for(50ms), std::future_status::timeout);
  CORE_ASSERT_TRUE(queue.close());
  CORE_ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
  CORE_ASSERT_EQ(result.get(), QueueStatus::Closed);
  int value = 0;
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Success);
  CORE_ASSERT_EQ(value, 50);
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Closed);
}

void test_cancel_wakes_consumer() {
  CancelToken token;
  BoundedQueue<int> queue(1U, token);
  auto result = std::async(std::launch::async, [&queue] {
    int output = 0;
    return queue.pop(output);
  });
  CORE_ASSERT_EQ(result.wait_for(50ms), std::future_status::timeout);
  CORE_ASSERT_TRUE(token.cancel());
  CORE_ASSERT_FALSE(token.cancel());
  CORE_ASSERT_TRUE(token.is_cancelled());
  CORE_ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
  CORE_ASSERT_EQ(result.get(), QueueStatus::Cancelled);
}

void test_cancel_wakes_producer() {
  CancelToken token;
  BoundedQueue<int> queue(1U, token);
  CORE_ASSERT_EQ(queue.push(70), QueueStatus::Success);
  auto result = std::async(std::launch::async, [&queue] { return queue.push(80); });
  CORE_ASSERT_EQ(result.wait_for(50ms), std::future_status::timeout);
  CORE_ASSERT_TRUE(token.cancel());
  CORE_ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
  CORE_ASSERT_EQ(result.get(), QueueStatus::Cancelled);
}

void test_pre_cancelled_queue() {
  CancelToken token;
  CORE_ASSERT_TRUE(token.cancel());
  BoundedQueue<int> queue(1U, token);
  int value = 0;
  CORE_ASSERT_EQ(queue.push(90), QueueStatus::Cancelled);
  CORE_ASSERT_EQ(queue.pop(value), QueueStatus::Cancelled);
}

CORE_ENGINE_TEST(queue_01, "QUE-01") {
  CORE_ASSERT_TRUE(std::filesystem::is_directory(test_context.temporary_directory()));
  test_capacity_fifo_and_producer_release();
  test_closed_queue_contract();
  test_close_wakes_consumer();
  test_close_wakes_producer_without_loss();
  test_cancel_wakes_consumer();
  test_cancel_wakes_producer();
  test_pre_cancelled_queue();
}

}
