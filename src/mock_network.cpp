/// Implements bounded in-process full-duplex socket transport.

#include "core_engine/mock_network.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core_engine {

namespace {

enum class SocketKind { Created, Bound, Listener, Connected };

struct Ring final {
  explicit Ring(std::size_t capacity_value) : bytes(capacity_value) {}

  std::mutex mutex;
  std::condition_variable readable;
  std::condition_variable writable;
  std::vector<std::uint8_t> bytes;
  std::size_t read_position{0U};
  std::size_t write_position{0U};
  std::size_t size{0U};
  bool writer_open{true};
  bool reader_open{true};
  bool reset{false};
  bool cancelled{false};
};

struct Endpoint final {
  std::shared_ptr<Ring> incoming;
  std::shared_ptr<Ring> outgoing;
  std::mutex fault_mutex;
  std::size_t send_count{0U};
  std::size_t recv_count{0U};
  std::size_t fail_send{0U};
  std::size_t fail_recv{0U};
  std::size_t corrupt_send{0U};
  std::size_t drop_connection{0U};
};

struct SocketState final {
  std::mutex error_mutex;
  std::condition_variable pending_changed;
  SocketKind kind{SocketKind::Created};
  std::uint8_t address{0U};
  std::size_t backlog{0U};
  std::deque<std::shared_ptr<SocketState>> pending;
  std::shared_ptr<Endpoint> endpoint;
  SocketError error{SocketError::None};
  bool closed{false};
};

void set_error(const std::shared_ptr<SocketState>& socket, SocketError error) {
  const std::lock_guard<std::mutex> lock(socket->error_mutex);
  socket->error = error;
}

SocketError get_error(const std::shared_ptr<SocketState>& socket) {
  const std::lock_guard<std::mutex> lock(socket->error_mutex);
  return socket->error;
}

void wake_ring(Ring& ring) {
  ring.readable.notify_all();
  ring.writable.notify_all();
}

void cancel_ring(const std::shared_ptr<Ring>& ring) {
  {
    const std::lock_guard<std::mutex> lock(ring->mutex);
    ring->cancelled = true;
  }
  wake_ring(*ring);
}

void reset_ring(const std::shared_ptr<Ring>& ring) {
  {
    const std::lock_guard<std::mutex> lock(ring->mutex);
    ring->reset = true;
    ring->writer_open = false;
    ring->reader_open = false;
  }
  wake_ring(*ring);
}

void close_writer(const std::shared_ptr<Ring>& ring) {
  {
    const std::lock_guard<std::mutex> lock(ring->mutex);
    ring->writer_open = false;
  }
  ring->readable.notify_all();
}

void close_reader(const std::shared_ptr<Ring>& ring) {
  {
    const std::lock_guard<std::mutex> lock(ring->mutex);
    ring->reader_open = false;
  }
  ring->writable.notify_all();
}

void close_endpoint(const std::shared_ptr<Endpoint>& endpoint) {
  close_writer(endpoint->outgoing);
  close_reader(endpoint->incoming);
}

bool reaches(std::size_t& counter, std::size_t occurrence) {
  ++counter;
  return occurrence != 0U && counter == occurrence;
}

std::size_t transfer_limit(std::size_t requested, std::size_t available, std::size_t configured) {
  return std::min(requested, std::min(available, configured));
}

void copy_into_ring(Ring& ring, const std::uint8_t* source, std::size_t length, bool corrupt) {
  const std::size_t first = std::min(length, ring.bytes.size() - ring.write_position);
  std::memcpy(ring.bytes.data() + ring.write_position, source, first);
  if (length > first) {
    std::memcpy(ring.bytes.data(), source + first, length - first);
  }
  if (corrupt) {
    ring.bytes[ring.write_position] ^= UINT8_C(1);
  }
  ring.write_position = (ring.write_position + length) % ring.bytes.size();
  ring.size += length;
}

void copy_from_ring(Ring& ring, std::uint8_t* destination, std::size_t length) {
  const std::size_t first = std::min(length, ring.bytes.size() - ring.read_position);
  std::memcpy(destination, ring.bytes.data() + ring.read_position, first);
  if (length > first) {
    std::memcpy(destination + first, ring.bytes.data(), length - first);
  }
  ring.read_position = (ring.read_position + length) % ring.bytes.size();
  ring.size -= length;
}

std::optional<std::ptrdiff_t> reject_transfer(const std::shared_ptr<SocketState>& state,
                                              const void* buffer, std::size_t length) {
  if (state->kind != SocketKind::Connected || !state->endpoint ||
      (length != 0U && buffer == nullptr)) {
    set_error(state, SocketError::NotConnected);
    return -1;
  }
  if (length == 0U) {
    set_error(state, SocketError::None);
    return 0;
  }
  return std::nullopt;
}

std::ptrdiff_t send_on(const std::shared_ptr<SocketState>& state, const void* buffer,
                       std::size_t length, std::size_t operation_limit) {
  if (const std::optional<std::ptrdiff_t> rejected = reject_transfer(state, buffer, length)) {
    return *rejected;
  }
  bool fail = false;
  bool corrupt = false;
  bool drop = false;
  {
    const std::lock_guard<std::mutex> lock(state->endpoint->fault_mutex);
    fail = reaches(state->endpoint->send_count, state->endpoint->fail_send);
    corrupt = state->endpoint->send_count == state->endpoint->corrupt_send;
    drop = state->endpoint->send_count == state->endpoint->drop_connection;
  }
  if (drop) {
    reset_ring(state->endpoint->incoming);
    reset_ring(state->endpoint->outgoing);
    set_error(state, SocketError::ConnectionReset);
    return -1;
  }
  if (fail) {
    set_error(state, SocketError::Injected);
    return -1;
  }
  Ring& ring = *state->endpoint->outgoing;
  std::unique_lock<std::mutex> lock(ring.mutex);
  ring.writable.wait(lock, [&] {
    return ring.cancelled || ring.reset || !ring.reader_open || ring.size < ring.bytes.size();
  });
  if (ring.cancelled) {
    set_error(state, SocketError::Cancelled);
    return -1;
  }
  if (ring.reset) {
    set_error(state, SocketError::ConnectionReset);
    return -1;
  }
  if (!ring.reader_open) {
    set_error(state, SocketError::BrokenPipe);
    return -1;
  }
  const std::size_t count = transfer_limit(length, ring.bytes.size() - ring.size, operation_limit);
  copy_into_ring(ring, static_cast<const std::uint8_t*>(buffer), count, corrupt);
  lock.unlock();
  ring.readable.notify_one();
  set_error(state, SocketError::None);
  return static_cast<std::ptrdiff_t>(count);
}

std::ptrdiff_t recv_on(const std::shared_ptr<SocketState>& state, void* buffer, std::size_t length,
                       std::size_t operation_limit) {
  if (const std::optional<std::ptrdiff_t> rejected = reject_transfer(state, buffer, length)) {
    return *rejected;
  }
  bool fail = false;
  {
    const std::lock_guard<std::mutex> lock(state->endpoint->fault_mutex);
    fail = reaches(state->endpoint->recv_count, state->endpoint->fail_recv);
  }
  if (fail) {
    set_error(state, SocketError::Injected);
    return -1;
  }
  Ring& ring = *state->endpoint->incoming;
  std::unique_lock<std::mutex> lock(ring.mutex);
  ring.readable.wait(
      lock, [&] { return ring.cancelled || ring.reset || ring.size != 0U || !ring.writer_open; });
  if (ring.cancelled) {
    set_error(state, SocketError::Cancelled);
    return -1;
  }
  if (ring.reset) {
    set_error(state, SocketError::ConnectionReset);
    return -1;
  }
  if (ring.size == 0U && !ring.writer_open) {
    set_error(state, SocketError::None);
    return 0;
  }
  const std::size_t count = transfer_limit(length, ring.size, operation_limit);
  copy_from_ring(ring, static_cast<std::uint8_t*>(buffer), count);
  lock.unlock();
  ring.writable.notify_one();
  set_error(state, SocketError::None);
  return static_cast<std::ptrdiff_t>(count);
}

}

class MockNetwork::Impl final {
public:
  explicit Impl(std::size_t channel_bytes_value) : channel_bytes(channel_bytes_value) {}

  std::shared_ptr<SocketState> find(int fd) const {
    const std::lock_guard<std::mutex> lock(mutex);
    const auto entry = sockets.find(fd);
    return entry == sockets.end() ? nullptr : entry->second;
  }

  void cancel() {
    std::vector<std::shared_ptr<SocketState>> states;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (cancelled) {
        return;
      }
      cancelled = true;
      states.reserve(sockets.size());
      for (const auto& entry : sockets) {
        states.push_back(entry.second);
      }
    }
    for (const auto& state : states) {
      state->pending_changed.notify_all();
      if (state->endpoint) {
        cancel_ring(state->endpoint->incoming);
        cancel_ring(state->endpoint->outgoing);
      }
    }
  }

  bool is_cancelled() const {
    const std::lock_guard<std::mutex> lock(mutex);
    return cancelled;
  }

  std::size_t operation_limit() const noexcept {
    if (one_byte.load(std::memory_order_relaxed)) {
      return 1U;
    }
    return max_operation.load(std::memory_order_relaxed);
  }

  mutable std::mutex mutex;
  std::unordered_map<int, std::shared_ptr<SocketState>> sockets;
  std::unordered_map<std::uint8_t, std::shared_ptr<SocketState>> bindings;
  const std::size_t channel_bytes;
  int next_fd{0};
  bool cancelled{false};
  std::atomic<std::size_t> max_operation{std::numeric_limits<std::size_t>::max()};
  std::atomic<bool> one_byte{false};
};

MockNetwork::MockNetwork(std::size_t channel_bytes, CancelToken& cancel_token)
    : impl_(std::make_shared<Impl>(channel_bytes)),
      cancellation_(cancel_token.subscribe([weak_impl = std::weak_ptr<Impl>(impl_)] {
        if (const std::shared_ptr<Impl> impl = weak_impl.lock()) {
          impl->cancel();
        }
      })) {
  if (channel_bytes == 0U) {
    throw std::invalid_argument("channel capacity must be greater than zero");
  }
}

MockNetwork::~MockNetwork() {
  impl_->cancel();
  std::vector<int> descriptors;
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    descriptors.reserve(impl_->sockets.size());
    for (const auto& entry : impl_->sockets) {
      descriptors.push_back(entry.first);
    }
  }
  for (const int fd : descriptors) {
    static_cast<void>(close(fd));
  }
}

int MockNetwork::socket() {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->cancelled || impl_->next_fd == std::numeric_limits<int>::max()) {
    return -1;
  }
  const int fd = impl_->next_fd;
  ++impl_->next_fd;
  impl_->sockets.emplace(fd, std::make_shared<SocketState>());
  return fd;
}

int MockNetwork::bind(int fd, NodeId address) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto entry = impl_->sockets.find(fd);
  if (entry == impl_->sockets.end()) {
    return -1;
  }
  const std::shared_ptr<SocketState>& state = entry->second;
  if (impl_->cancelled) {
    set_error(state, SocketError::Cancelled);
    return -1;
  }
  if (state->kind != SocketKind::Created || impl_->bindings.count(address.value()) != 0U) {
    set_error(state, SocketError::ConnectionRefused);
    return -1;
  }
  state->kind = SocketKind::Bound;
  state->address = address.value();
  impl_->bindings.emplace(address.value(), state);
  set_error(state, SocketError::None);
  return 0;
}

int MockNetwork::listen(int fd, int backlog) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto entry = impl_->sockets.find(fd);
  if (entry == impl_->sockets.end()) {
    return -1;
  }
  const std::shared_ptr<SocketState>& state = entry->second;
  if (impl_->cancelled) {
    set_error(state, SocketError::Cancelled);
    return -1;
  }
  if (state->kind != SocketKind::Bound || backlog <= 0) {
    set_error(state, SocketError::NotConnected);
    return -1;
  }
  state->kind = SocketKind::Listener;
  state->backlog = static_cast<std::size_t>(backlog);
  set_error(state, SocketError::None);
  return 0;
}

int MockNetwork::accept(int fd) {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  const auto entry = impl_->sockets.find(fd);
  if (entry == impl_->sockets.end()) {
    return -1;
  }
  const std::shared_ptr<SocketState> listener = entry->second;
  if (listener->kind != SocketKind::Listener) {
    set_error(listener, SocketError::NotConnected);
    return -1;
  }
  listener->pending_changed.wait(
      lock, [&] { return impl_->cancelled || listener->closed || !listener->pending.empty(); });
  if (impl_->cancelled) {
    set_error(listener, SocketError::Cancelled);
    return -1;
  }
  if (listener->closed) {
    set_error(listener, SocketError::BadFileDescriptor);
    return -1;
  }
  const std::shared_ptr<SocketState> accepted = listener->pending.front();
  listener->pending.pop_front();
  if (impl_->next_fd == std::numeric_limits<int>::max()) {
    set_error(listener, SocketError::ConnectionRefused);
    return -1;
  }
  const int accepted_fd = impl_->next_fd;
  ++impl_->next_fd;
  impl_->sockets.emplace(accepted_fd, accepted);
  set_error(listener, SocketError::None);
  return accepted_fd;
}

int MockNetwork::connect(int fd, NodeId address) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto client_entry = impl_->sockets.find(fd);
  if (client_entry == impl_->sockets.end()) {
    return -1;
  }
  const std::shared_ptr<SocketState>& client = client_entry->second;
  if (impl_->cancelled) {
    set_error(client, SocketError::Cancelled);
    return -1;
  }
  const auto listener_entry = impl_->bindings.find(address.value());
  if (client->kind != SocketKind::Created || listener_entry == impl_->bindings.end() ||
      listener_entry->second->kind != SocketKind::Listener || listener_entry->second->closed ||
      listener_entry->second->pending.size() >= listener_entry->second->backlog) {
    set_error(client, SocketError::ConnectionRefused);
    return -1;
  }
  auto to_server = std::make_shared<Ring>(impl_->channel_bytes);
  auto to_client = std::make_shared<Ring>(impl_->channel_bytes);
  auto client_endpoint = std::make_shared<Endpoint>();
  auto server_endpoint = std::make_shared<Endpoint>();
  client_endpoint->incoming = to_client;
  client_endpoint->outgoing = to_server;
  server_endpoint->incoming = std::move(to_server);
  server_endpoint->outgoing = std::move(to_client);
  client->kind = SocketKind::Connected;
  client->endpoint = std::move(client_endpoint);
  auto server = std::make_shared<SocketState>();
  server->kind = SocketKind::Connected;
  server->endpoint = std::move(server_endpoint);
  listener_entry->second->pending.push_back(std::move(server));
  set_error(client, SocketError::None);
  listener_entry->second->pending_changed.notify_one();
  return 0;
}

std::ptrdiff_t MockNetwork::send(int fd, const void* buffer, std::size_t length) {
  const std::shared_ptr<SocketState> state = impl_->find(fd);
  return state ? send_on(state, buffer, length, impl_->operation_limit()) : -1;
}

std::ptrdiff_t MockNetwork::recv(int fd, void* buffer, std::size_t length) {
  const std::shared_ptr<SocketState> state = impl_->find(fd);
  return state ? recv_on(state, buffer, length, impl_->operation_limit()) : -1;
}

int MockNetwork::shutdown(int fd, ShutdownHow how) {
  const std::shared_ptr<SocketState> state = impl_->find(fd);
  if (!state) {
    return -1;
  }
  if (state->kind != SocketKind::Connected || !state->endpoint) {
    set_error(state, SocketError::NotConnected);
    return -1;
  }
  if (how == ShutdownHow::Read || how == ShutdownHow::Both) {
    close_reader(state->endpoint->incoming);
  }
  if (how == ShutdownHow::Write || how == ShutdownHow::Both) {
    close_writer(state->endpoint->outgoing);
  }
  set_error(state, SocketError::None);
  return 0;
}

int MockNetwork::close(int fd) {
  std::shared_ptr<SocketState> state;
  std::vector<std::shared_ptr<SocketState>> pending;
  {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto entry = impl_->sockets.find(fd);
    if (entry == impl_->sockets.end()) {
      return -1;
    }
    state = entry->second;
    state->closed = true;
    if (state->kind == SocketKind::Bound || state->kind == SocketKind::Listener) {
      impl_->bindings.erase(state->address);
    }
    pending.assign(state->pending.begin(), state->pending.end());
    state->pending.clear();
    impl_->sockets.erase(entry);
  }
  state->pending_changed.notify_all();
  if (state->endpoint) {
    close_endpoint(state->endpoint);
  }
  for (const auto& connection : pending) {
    if (connection->endpoint) {
      reset_ring(connection->endpoint->incoming);
      reset_ring(connection->endpoint->outgoing);
    }
  }
  return 0;
}

SocketError MockNetwork::last_error(int fd) const {
  const std::shared_ptr<SocketState> state = impl_->find(fd);
  return state ? get_error(state) : SocketError::BadFileDescriptor;
}

void MockNetwork::set_max_operation_bytes(std::size_t bytes) {
  if (bytes == 0U) {
    throw std::invalid_argument("operation limit must be greater than zero");
  }
  impl_->max_operation.store(bytes, std::memory_order_relaxed);
}

void MockNetwork::set_one_byte_mode(bool enabled) noexcept {
  impl_->one_byte.store(enabled, std::memory_order_relaxed);
}

int MockNetwork::inject_fault(int fd, NetworkFault fault, std::size_t occurrence) {
  const std::shared_ptr<SocketState> state = impl_->find(fd);
  if (!state) {
    return -1;
  }
  if (state->kind != SocketKind::Connected || !state->endpoint || occurrence == 0U) {
    set_error(state, SocketError::NotConnected);
    return -1;
  }
  const std::lock_guard<std::mutex> lock(state->endpoint->fault_mutex);
  if (fault == NetworkFault::FailSend) {
    state->endpoint->fail_send = occurrence;
  } else if (fault == NetworkFault::FailRecv) {
    state->endpoint->fail_recv = occurrence;
  } else if (fault == NetworkFault::CorruptSend) {
    state->endpoint->corrupt_send = occurrence;
  } else {
    state->endpoint->drop_connection = occurrence;
  }
  set_error(state, SocketError::None);
  return 0;
}

std::ptrdiff_t MockNetwork::send_all(int fd, const void* buffer, std::size_t length) {
  const std::shared_ptr<SocketState> state = impl_->find(fd);
  if (!state) {
    return -1;
  }
  if (length > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
    set_error(state, SocketError::Injected);
    return -1;
  }
  const auto* bytes = static_cast<const std::uint8_t*>(buffer);
  std::size_t sent = 0U;
  while (sent < length) {
    const std::ptrdiff_t result =
        send_on(state, bytes + sent, length - sent, impl_->operation_limit());
    if (result < 0) {
      return -1;
    }
    sent += static_cast<std::size_t>(result);
  }
  return static_cast<std::ptrdiff_t>(sent);
}

std::ptrdiff_t MockNetwork::recv_exact(int fd, void* buffer, std::size_t length) {
  const std::shared_ptr<SocketState> state = impl_->find(fd);
  if (!state) {
    return -1;
  }
  if (length > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
    set_error(state, SocketError::Injected);
    return -1;
  }
  auto* bytes = static_cast<std::uint8_t*>(buffer);
  std::size_t received = 0U;
  while (received < length) {
    const std::ptrdiff_t result =
        recv_on(state, bytes + received, length - received, impl_->operation_limit());
    if (result < 0) {
      return -1;
    }
    if (result == 0) {
      if (received == 0U) {
        return 0;
      }
      set_error(state, SocketError::ConnectionReset);
      return -1;
    }
    received += static_cast<std::size_t>(result);
  }
  return static_cast<std::ptrdiff_t>(received);
}

}
