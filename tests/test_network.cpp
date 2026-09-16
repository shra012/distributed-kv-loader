/// Verifies bounded mock socket transport and failure semantics.

#include "core_engine/cancel_token.hpp"
#include "core_engine/mock_network.hpp"
#include "test_framework.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using core_engine::CancelToken;
using core_engine::MockNetwork;
using core_engine::NetworkFault;
using core_engine::NodeId;
using core_engine::ShutdownHow;
using core_engine::SocketError;

struct Connection final {
  int listener;
  int client;
  int server;
};

Connection connect_pair(MockNetwork& network, std::uint8_t address, int backlog = 1) {
  const int listener = network.socket();
  const int client = network.socket();
  CORE_ASSERT_TRUE(listener >= 0);
  CORE_ASSERT_TRUE(client >= 0);
  CORE_ASSERT_EQ(network.bind(listener, NodeId(address)), 0);
  CORE_ASSERT_EQ(network.listen(listener, backlog), 0);
  CORE_ASSERT_EQ(network.connect(client, NodeId(address)), 0);
  const int server = network.accept(listener);
  CORE_ASSERT_TRUE(server >= 0);
  return Connection{listener, client, server};
}

std::vector<std::uint8_t> bytes(std::size_t size, std::uint8_t salt) {
  std::vector<std::uint8_t> result(size);
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::uint8_t>((index + static_cast<std::size_t>(salt)) % 251U);
  }
  return result;
}

void close_connection(MockNetwork& network, const Connection& connection) {
  CORE_ASSERT_EQ(network.close(connection.client), 0);
  CORE_ASSERT_EQ(network.close(connection.server), 0);
  CORE_ASSERT_EQ(network.close(connection.listener), 0);
}

}

CORE_ENGINE_TEST(network_01, "NET-01") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(64U, cancel);
  const Connection connection = connect_pair(network, 1U);
  CORE_ASSERT_EQ(network.last_error(connection.listener), SocketError::None);
  CORE_ASSERT_EQ(network.last_error(connection.client), SocketError::None);
  close_connection(network, connection);
}

CORE_ENGINE_TEST(network_02, "NET-02") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(128U, cancel);
  network.set_one_byte_mode(true);
  const Connection connection = connect_pair(network, 2U);
  const std::string sent = "one-byte full transfer";
  std::string received(sent.size(), '\0');
  CORE_ASSERT_EQ(network.send_all(connection.client, sent.data(), sent.size()),
                 static_cast<std::ptrdiff_t>(sent.size()));
  CORE_ASSERT_EQ(network.recv_exact(connection.server, received.data(), received.size()),
                 static_cast<std::ptrdiff_t>(received.size()));
  CORE_ASSERT_EQ(received, sent);
  close_connection(network, connection);
}

CORE_ENGINE_TEST(network_03, "NET-03") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(7U, cancel);
  network.set_max_operation_bytes(5U);
  const Connection connection = connect_pair(network, 3U);
  const std::vector<std::uint8_t> sent = bytes(4096U, 19U);
  std::vector<std::uint8_t> received(sent.size());
  std::atomic<std::ptrdiff_t> send_result{-2};
  std::thread sender([&] {
    send_result.store(network.send_all(connection.client, sent.data(), sent.size()),
                      std::memory_order_relaxed);
  });
  const std::ptrdiff_t receive_result =
      network.recv_exact(connection.server, received.data(), received.size());
  sender.join();
  CORE_ASSERT_EQ(send_result.load(std::memory_order_relaxed),
                 static_cast<std::ptrdiff_t>(sent.size()));
  CORE_ASSERT_EQ(receive_result, static_cast<std::ptrdiff_t>(received.size()));
  CORE_ASSERT_EQ(received, sent);
  close_connection(network, connection);
}

CORE_ENGINE_TEST(network_04, "NET-04") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(64U, cancel);
  constexpr std::size_t connection_count = 24U;
  const int listener = network.socket();
  CORE_ASSERT_TRUE(listener >= 0);
  CORE_ASSERT_EQ(network.bind(listener, NodeId(4U)), 0);
  CORE_ASSERT_EQ(network.listen(listener, static_cast<int>(connection_count)), 0);
  std::vector<int> clients;
  std::vector<int> servers;
  for (std::size_t index = 0U; index < connection_count; ++index) {
    const int client = network.socket();
    CORE_ASSERT_TRUE(client >= 0);
    CORE_ASSERT_EQ(network.connect(client, NodeId(4U)), 0);
    clients.push_back(client);
  }
  for (std::size_t index = 0U; index < connection_count; ++index) {
    const int server = network.accept(listener);
    CORE_ASSERT_TRUE(server >= 0);
    servers.push_back(server);
  }
  for (std::size_t index = 0U; index < connection_count; ++index) {
    const std::array<std::uint8_t, 3U> sent{static_cast<std::uint8_t>(index),
                                            static_cast<std::uint8_t>(index + 1U),
                                            static_cast<std::uint8_t>(index + 2U)};
    std::array<std::uint8_t, 3U> received{};
    CORE_ASSERT_EQ(network.send_all(clients[index], sent.data(), sent.size()), 3);
    CORE_ASSERT_EQ(network.recv_exact(servers[index], received.data(), received.size()), 3);
    CORE_ASSERT_EQ(received, sent);
  }
  for (const int client : clients) {
    CORE_ASSERT_EQ(network.close(client), 0);
  }
  for (const int server : servers) {
    CORE_ASSERT_EQ(network.close(server), 0);
  }
  CORE_ASSERT_EQ(network.close(listener), 0);
}

CORE_ENGINE_TEST(network_05, "NET-05") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(1U, cancel);
  const Connection first = connect_pair(network, 5U);
  CORE_ASSERT_EQ(network.close(first.server), 0);
  const std::uint8_t value = 7U;
  CORE_ASSERT_EQ(network.send(first.client, &value, 1U), -1);
  CORE_ASSERT_EQ(network.last_error(first.client), SocketError::BrokenPipe);
  std::uint8_t output = 0U;
  CORE_ASSERT_EQ(network.recv(first.client, &output, 1U), 0);
  CORE_ASSERT_EQ(network.close(first.client), 0);
  CORE_ASSERT_EQ(network.close(first.listener), 0);

  const Connection second = connect_pair(network, 6U);
  CORE_ASSERT_EQ(network.send(second.client, &value, 1U), 1);
  std::atomic<std::ptrdiff_t> blocked_result{-2};
  std::thread blocked_sender([&] {
    blocked_result.store(network.send(second.client, &value, 1U), std::memory_order_relaxed);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CORE_ASSERT_EQ(network.close(second.server), 0);
  blocked_sender.join();
  CORE_ASSERT_EQ(blocked_result.load(std::memory_order_relaxed), -1);
  CORE_ASSERT_EQ(network.last_error(second.client), SocketError::BrokenPipe);
  CORE_ASSERT_EQ(network.close(second.client), 0);
  CORE_ASSERT_EQ(network.close(second.listener), 0);

  const Connection third = connect_pair(network, 7U);
  CORE_ASSERT_EQ(network.send(third.client, &value, 1U), 1);
  CORE_ASSERT_EQ(network.shutdown(third.client, ShutdownHow::Write), 0);
  CORE_ASSERT_EQ(network.recv(third.server, &output, 1U), 1);
  CORE_ASSERT_EQ(output, value);
  CORE_ASSERT_EQ(network.recv(third.server, &output, 1U), 0);
  close_connection(network, third);
}

CORE_ENGINE_TEST(network_06, "NET-06") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(8U, cancel);
  CORE_ASSERT_EQ(network.last_error(999), SocketError::BadFileDescriptor);
  CORE_ASSERT_EQ(network.send(999, nullptr, 0U), -1);

  const int unbound_client = network.socket();
  CORE_ASSERT_EQ(network.connect(unbound_client, NodeId(8U)), -1);
  CORE_ASSERT_EQ(network.last_error(unbound_client), SocketError::ConnectionRefused);

  const int listener = network.socket();
  const int duplicate = network.socket();
  CORE_ASSERT_EQ(network.bind(listener, NodeId(9U)), 0);
  CORE_ASSERT_EQ(network.bind(duplicate, NodeId(9U)), -1);
  CORE_ASSERT_EQ(network.last_error(duplicate), SocketError::ConnectionRefused);
  CORE_ASSERT_EQ(network.listen(listener, 1), 0);
  const std::uint8_t value = 1U;
  CORE_ASSERT_EQ(network.send(listener, &value, 1U), -1);
  CORE_ASSERT_EQ(network.last_error(listener), SocketError::NotConnected);

  const int first_client = network.socket();
  const int overflow_client = network.socket();
  CORE_ASSERT_EQ(network.connect(first_client, NodeId(9U)), 0);
  CORE_ASSERT_EQ(network.connect(overflow_client, NodeId(9U)), -1);
  CORE_ASSERT_EQ(network.last_error(overflow_client), SocketError::ConnectionRefused);
  const int server = network.accept(listener);
  CORE_ASSERT_TRUE(server >= 0);
  CORE_ASSERT_EQ(network.close(server), 0);
  CORE_ASSERT_EQ(network.close(first_client), 0);
  CORE_ASSERT_EQ(network.close(overflow_client), 0);
  CORE_ASSERT_EQ(network.close(duplicate), 0);
  CORE_ASSERT_EQ(network.close(unbound_client), 0);
  CORE_ASSERT_EQ(network.close(listener), 0);
}

CORE_ENGINE_TEST(network_07, "NET-07") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(16U, cancel);
  const std::uint8_t original = 0x5AU;
  std::uint8_t received = 0U;

  const Connection send_failure = connect_pair(network, 10U);
  CORE_ASSERT_EQ(network.inject_fault(send_failure.client, NetworkFault::FailSend, 1U), 0);
  CORE_ASSERT_EQ(network.send(send_failure.client, &original, 1U), -1);
  CORE_ASSERT_EQ(network.last_error(send_failure.client), SocketError::Injected);
  close_connection(network, send_failure);

  const Connection recv_failure = connect_pair(network, 11U);
  CORE_ASSERT_EQ(network.inject_fault(recv_failure.server, NetworkFault::FailRecv, 1U), 0);
  CORE_ASSERT_EQ(network.send(recv_failure.client, &original, 1U), 1);
  CORE_ASSERT_EQ(network.recv(recv_failure.server, &received, 1U), -1);
  CORE_ASSERT_EQ(network.last_error(recv_failure.server), SocketError::Injected);
  close_connection(network, recv_failure);

  const Connection corruption = connect_pair(network, 12U);
  CORE_ASSERT_EQ(network.inject_fault(corruption.client, NetworkFault::CorruptSend, 1U), 0);
  CORE_ASSERT_EQ(network.send(corruption.client, &original, 1U), 1);
  CORE_ASSERT_EQ(network.recv(corruption.server, &received, 1U), 1);
  CORE_ASSERT_NE(received, original);
  close_connection(network, corruption);

  const Connection dropped = connect_pair(network, 13U);
  CORE_ASSERT_EQ(network.inject_fault(dropped.client, NetworkFault::DropConnection, 1U), 0);
  CORE_ASSERT_EQ(network.send(dropped.client, &original, 1U), -1);
  CORE_ASSERT_EQ(network.last_error(dropped.client), SocketError::ConnectionReset);
  CORE_ASSERT_EQ(network.recv(dropped.server, &received, 1U), -1);
  CORE_ASSERT_EQ(network.last_error(dropped.server), SocketError::ConnectionReset);
  close_connection(network, dropped);
}

CORE_ENGINE_TEST(network_08, "NET-08") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(1U, cancel);
  const Connection connection = connect_pair(network, 14U);
  const std::uint8_t value = 4U;
  CORE_ASSERT_EQ(network.send(connection.client, &value, 1U), 1);
  std::promise<void> started;
  std::atomic<std::ptrdiff_t> result{-2};
  std::thread sender([&] {
    started.set_value();
    result.store(network.send(connection.client, &value, 1U), std::memory_order_relaxed);
  });
  started.get_future().wait();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CORE_ASSERT_TRUE(cancel.cancel());
  sender.join();
  CORE_ASSERT_EQ(result.load(std::memory_order_relaxed), -1);
  CORE_ASSERT_EQ(network.last_error(connection.client), SocketError::Cancelled);
}

CORE_ENGINE_TEST(network_09, "NET-09") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(1U, cancel);
  const Connection connection = connect_pair(network, 15U);
  std::uint8_t value = 0U;
  std::promise<void> started;
  std::atomic<std::ptrdiff_t> result{-2};
  std::thread receiver([&] {
    started.set_value();
    result.store(network.recv(connection.server, &value, 1U), std::memory_order_relaxed);
  });
  started.get_future().wait();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CORE_ASSERT_TRUE(cancel.cancel());
  receiver.join();
  CORE_ASSERT_EQ(result.load(std::memory_order_relaxed), -1);
  CORE_ASSERT_EQ(network.last_error(connection.server), SocketError::Cancelled);
}

CORE_ENGINE_TEST(network_10, "NET-10") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(31U, cancel);
  network.set_max_operation_bytes(7U);
  constexpr std::size_t node_count = 5U;
  constexpr std::size_t iterations = 100U;
  std::array<int, node_count> listeners{};
  for (std::size_t node = 0U; node < node_count; ++node) {
    listeners[node] = network.socket();
    CORE_ASSERT_EQ(network.bind(listeners[node], NodeId(static_cast<std::uint8_t>(node))), 0);
    CORE_ASSERT_EQ(network.listen(listeners[node], static_cast<int>(node_count - 1U)), 0);
  }

  struct Link final {
    std::size_t source;
    std::size_t destination;
    int client;
    int server;
  };
  std::vector<Link> links;
  for (std::size_t destination = 0U; destination < node_count; ++destination) {
    for (std::size_t source = 0U; source < node_count; ++source) {
      if (source == destination) {
        continue;
      }
      const int client = network.socket();
      CORE_ASSERT_EQ(network.connect(client, NodeId(static_cast<std::uint8_t>(destination))), 0);
      links.push_back(Link{source, destination, client, -1});
    }
  }
  for (std::size_t destination = 0U; destination < node_count; ++destination) {
    for (Link& link : links) {
      if (link.destination == destination) {
        link.server = network.accept(listeners[destination]);
        CORE_ASSERT_TRUE(link.server >= 0);
      }
    }
  }

  std::atomic<bool> valid{true};
  std::vector<std::thread> threads;
  threads.reserve(links.size() * 2U);
  for (const Link& link : links) {
    threads.emplace_back([&, link] {
      for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        const std::array<std::uint8_t, 3U> message{static_cast<std::uint8_t>(link.source),
                                                   static_cast<std::uint8_t>(link.destination),
                                                   static_cast<std::uint8_t>(iteration)};
        if (network.send_all(link.client, message.data(), message.size()) != 3) {
          valid.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
    threads.emplace_back([&, link] {
      for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        std::array<std::uint8_t, 3U> message{};
        const std::array<std::uint8_t, 3U> expected{static_cast<std::uint8_t>(link.source),
                                                    static_cast<std::uint8_t>(link.destination),
                                                    static_cast<std::uint8_t>(iteration)};
        if (network.recv_exact(link.server, message.data(), message.size()) != 3 ||
            message != expected) {
          valid.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  CORE_ASSERT_TRUE(valid.load(std::memory_order_relaxed));
  for (const Link& link : links) {
    CORE_ASSERT_EQ(network.close(link.client), 0);
    CORE_ASSERT_EQ(network.close(link.server), 0);
  }
  for (const int listener : listeners) {
    CORE_ASSERT_EQ(network.close(listener), 0);
  }
}

CORE_ENGINE_TEST(network_11, "NET-11") {
  static_cast<void>(test_context);
  CancelToken cancel;
  MockNetwork network(8U, cancel);
  constexpr std::size_t connection_count = 4U;
  const int listener = network.socket();
  CORE_ASSERT_EQ(network.bind(listener, NodeId(16U)), 0);
  CORE_ASSERT_EQ(network.listen(listener, static_cast<int>(connection_count)), 0);
  std::array<int, connection_count> clients{};
  for (int& client : clients) {
    client = network.socket();
    CORE_ASSERT_EQ(network.connect(client, NodeId(16U)), 0);
  }
  std::array<int, connection_count> servers{};
  for (int& server : servers) {
    server = network.accept(listener);
    CORE_ASSERT_TRUE(server >= 0);
  }
  for (const int client : clients) {
    CORE_ASSERT_EQ(network.close(client), 0);
  }
  for (const int server : servers) {
    CORE_ASSERT_EQ(network.close(server), 0);
  }
  CORE_ASSERT_EQ(network.close(listener), 0);
}
