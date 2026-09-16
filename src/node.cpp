/// Implements one simulated cluster node's sockets, threads, store, and counters.

#include "core_engine/node.hpp"

#include "core_engine/batch.hpp"
#include "core_engine/bounded_queue.hpp"
#include "core_engine/codec.hpp"
#include "core_engine/hash.hpp"
#include "core_engine/packed_record.hpp"
#include "core_engine/parser.hpp"

#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace core_engine {
namespace {

struct InboundBatch final {
  std::uint32_t source;
  bool is_end;
  std::string bytes;
  std::size_t payload_offset;
  std::uint32_t record_count;
  std::vector<std::uint64_t> lines;
};

}

struct Node::Impl final {
  Impl(NodeId node_id, const Config& node_config, MockNetwork& node_network,
       CancelToken& node_cancel_token, FirstFailure& node_first_failure)
      : id(node_id), config(node_config), network(node_network), cancel_token(node_cancel_token),
        first_failure(node_first_failure),
        store(static_cast<std::uint32_t>(node_id.value()), node_config.node_count()),
        inbound_queue(node_config.inbound_queue_batches(), node_cancel_token),
        outbound_fds(node_config.node_count(), -1), inbound_fds(node_config.node_count(), -1),
        sequence_numbers(node_config.node_count(), 0U) {}

  [[nodiscard]] std::uint32_t self() const noexcept {
    return static_cast<std::uint32_t>(id.value());
  }

  [[nodiscard]] Error transport_error(std::string detail) const {
    return Error(ErrorCode::Transport, std::move(detail), self());
  }

  template <typename Body> void guarded(Body body) {
    try {
      body();
    } catch (const Error& error) {
      report_failure(error);
    } catch (const std::exception& error) {
      report_failure(Error(ErrorCode::InternalInvariant, error.what(), self()));
    }
  }

  void report_failure(Error error) {
    if (!cancel_token.is_cancelled()) {
      switch (error.code()) {
      case ErrorCode::MalformedRecord:
        counters.add(Counter::Malformed);
        break;
      case ErrorCode::Transport:
        counters.add(Counter::Transport);
        break;
      case ErrorCode::Codec:
        counters.add(Counter::Codec);
        break;
      case ErrorCode::ConflictingDuplicate:
        counters.add(Counter::Conflict);
        break;
      default:
        break;
      }
      first_failure.report(std::move(error));
      cancel_token.cancel();
    }
  }

  void push_inbound(InboundBatch batch) { static_cast<void>(inbound_queue.push(std::move(batch))); }

  [[nodiscard]] bool send_exactly(int fd, std::string_view bytes) {
    return bytes.empty() || network.send_all(fd, bytes.data(), bytes.size()) ==
                                static_cast<std::ptrdiff_t>(bytes.size());
  }

  void send_frame(std::uint32_t peer, const Batch& batch, FrameType type) {
    const int fd = outbound_fds.at(peer);
    const std::string_view payload =
        type == FrameType::End ? std::string_view{} : std::string_view(batch.payload);
    if (!send_exactly(fd, encode_frame_header(batch, type)) || !send_exactly(fd, payload)) {
      throw transport_error("failed to send frame to peer");
    }
    counters.add(Counter::FramesOut);
    counters.add(Counter::BytesOut, payload.size());
  }

  void flush(std::uint32_t destination, BatchBuilder& builder,
             std::vector<std::uint64_t>& local_lines) {
    if (destination != self()) {
      send_frame(destination, builder.take(sequence_numbers[destination]++), FrameType::Data);
      return;
    }
    Batch batch = builder.take(0U);
    push_inbound(InboundBatch{self(), false, std::move(batch.payload), 0U, batch.record_count,
                              std::exchange(local_lines, {})});
  }

  [[nodiscard]] std::uint32_t route_owner(std::uint32_t owner) {
    if (!misroute_pending) {
      return owner;
    }
    for (std::uint32_t candidate = 0U; candidate < config.node_count(); ++candidate) {
      if (candidate != owner && candidate != self()) {
        misroute_pending = false;
        return candidate;
      }
    }
    return owner;
  }

  void read_input(std::vector<std::optional<BatchBuilder>>& builders,
                  std::vector<std::uint64_t>& local_lines) {
    const std::optional<std::filesystem::path>& input = config.input_path(self());
    if (!input.has_value()) {
      return;
    }
    const std::uint32_t node_count = config.node_count();
    std::uint64_t local = 0U;
    std::uint64_t remote = 0U;
    parse_file(*input, config.schema(), config.max_record_bytes(), config.read_buffer_bytes(),
               [&](const RecordView& view) {
                 const std::uint32_t owner = node_count == 1U
                                                 ? 0U
                                                 : route_owner(jump_consistent_hash(
                                                       fnv1a_64(view.canonical_key), node_count));
                 BatchBuilder& builder = *builders[owner];
                 if (!builder.fits(view.record.size())) {
                   flush(owner, builder, local_lines);
                 }
                 builder.append(view.record);
                 if (owner == self()) {
                   local_lines.push_back(view.line);
                   ++local;
                 } else {
                   ++remote;
                 }
               });
    counters.add(Counter::LinesRead, local + remote);
    counters.add(Counter::Parsed, local + remote);
    counters.add(Counter::Local, local);
    counters.add(Counter::RemoteOut, remote);
  }

  void reader_loop() {
    std::vector<std::optional<BatchBuilder>> builders(config.node_count());
    for (std::uint32_t node = 0U; node < config.node_count(); ++node) {
      if (node == self() || outbound_fds[node] >= 0) {
        builders[node].emplace(static_cast<std::uint8_t>(self()), static_cast<std::uint8_t>(node),
                               config.batch_bytes());
      }
    }
    std::vector<std::uint64_t> local_lines;
    read_input(builders, local_lines);
    for (std::uint32_t node = 0U; node < config.node_count(); ++node) {
      if (builders[node].has_value() && !builders[node]->empty()) {
        flush(node, *builders[node], local_lines);
      }
    }
    for (std::uint32_t peer = 0U; peer < config.node_count(); ++peer) {
      if (outbound_fds[peer] >= 0) {
        const Batch end{static_cast<std::uint8_t>(self()),
                        static_cast<std::uint8_t>(peer),
                        sequence_numbers[peer]++,
                        0U,
                        {}};
        send_frame(peer, end, FrameType::End);
        static_cast<void>(network.close(outbound_fds[peer]));
      }
    }
    push_inbound(InboundBatch{self(), true, {}, 0U, 0U, {}});
  }

  void receive_exactly(int fd, char* destination, std::size_t length) {
    if (length > 0U &&
        network.recv_exact(fd, destination, length) != static_cast<std::ptrdiff_t>(length)) {
      throw transport_error("connection closed before an END frame arrived");
    }
  }

  void receiver_loop(std::uint32_t source, int fd) {
    std::uint64_t expected_sequence = 0U;
    std::string frame;
    while (true) {
      frame.resize(kFrameHeaderBytes);
      receive_exactly(fd, frame.data(), kFrameHeaderBytes);
      const std::uint32_t payload_size = peek_payload_size(frame);
      if (payload_size > config.batch_bytes()) {
        throw Error(ErrorCode::Codec, "frame payload exceeds the configured batch size", self());
      }
      frame.resize(kFrameHeaderBytes + payload_size);
      receive_exactly(fd, frame.data() + kFrameHeaderBytes, payload_size);
      const DecodedFrame decoded = decode_frame(frame, config.batch_bytes());
      if (decoded.source != source || decoded.destination != self() ||
          decoded.sequence != expected_sequence++) {
        throw Error(ErrorCode::Codec, "unexpected frame endpoint or sequence", self());
      }
      counters.add(Counter::FramesIn);
      counters.add(Counter::BytesIn, payload_size);
      if (decoded.type == FrameType::End) {
        static_cast<void>(network.close(fd));
        push_inbound(InboundBatch{source, true, {}, 0U, 0U, {}});
        return;
      }
      counters.add(Counter::RemoteIn, decoded.record_count);
      if (inbound_queue.push(InboundBatch{
              source, false, std::move(frame), kFrameHeaderBytes, decoded.record_count, {}}) !=
          QueueStatus::Success) {
        return;
      }
      frame = std::string();
    }
  }

  void store_batch(const InboundBatch& batch, std::string_view local_file, std::string& key) {
    const bool is_local = batch.source == self();
    std::string_view payload = std::string_view(batch.bytes).substr(batch.payload_offset);
    for (std::uint32_t index = 0U; index < batch.record_count; ++index) {
      const RecordLayout layout = scan_record(config.schema(), payload);
      const std::string_view record = payload.substr(0U, layout.length);
      assign_canonical_key(key, layout.key_tag,
                           record.substr(layout.key_offset, layout.key_length));
      if (is_local) {
        store.insert(key, record, std::nullopt, local_file, batch.lines[index]);
      } else {
        store.insert(key, record);
      }
      payload.remove_prefix(layout.length);
    }
    if (!payload.empty()) {
      throw Error(ErrorCode::InternalInvariant, "payload has trailing bytes after declared records",
                  self());
    }
  }

  void store_worker_loop() {
    const std::optional<std::filesystem::path>& input = config.input_path(self());
    const std::string local_file = input.has_value() ? input->string() : "";
    std::string key;
    std::size_t end_count = 0U;
    while (end_count < config.node_count()) {
      InboundBatch batch;
      if (inbound_queue.pop(batch) != QueueStatus::Success) {
        return;
      }
      if (batch.is_end) {
        ++end_count;
        counters.add(Counter::EndMarkersReceived);
        continue;
      }
      store_batch(batch, local_file, key);
      counters.add(Counter::Ingested, batch.record_count);
    }
    counters.add(Counter::Loaded, store.size());
    counters.add(Counter::Duplicates, store.duplicate_count());
  }

  NodeId id;
  const Config& config;
  MockNetwork& network;
  CancelToken& cancel_token;
  FirstFailure& first_failure;
  Store store;
  NodeCounters counters;
  BoundedQueue<InboundBatch> inbound_queue;
  int listener_fd{-1};
  std::vector<int> outbound_fds;
  std::vector<int> inbound_fds;
  std::vector<std::uint64_t> sequence_numbers;
  std::vector<std::thread> threads;
  bool joined{false};
  bool misroute_pending{false};
};

Node::Node(NodeId id, const Config& config, MockNetwork& network, CancelToken& cancel_token,
           FirstFailure& first_failure)
    : impl_(std::make_unique<Impl>(id, config, network, cancel_token, first_failure)) {}

Node::~Node() { join(); }

void Node::bind_and_listen() {
  impl_->listener_fd = impl_->network.socket();
  if (impl_->listener_fd < 0) {
    throw impl_->transport_error("cannot create listening socket");
  }
  if (impl_->network.bind(impl_->listener_fd, impl_->id) != 0) {
    throw impl_->transport_error("cannot bind listening socket");
  }
  const std::uint32_t peer_count = impl_->config.node_count() - 1U;
  const int backlog = static_cast<int>(peer_count > 0U ? peer_count : 1U);
  if (impl_->network.listen(impl_->listener_fd, backlog) != 0) {
    throw impl_->transport_error("cannot listen on socket");
  }
}

void Node::connect_to(NodeId peer) {
  const int fd = impl_->network.socket();
  if (fd < 0) {
    throw impl_->transport_error("cannot create outbound socket");
  }
  if (impl_->network.connect(fd, peer) != 0) {
    throw impl_->transport_error("cannot connect to peer");
  }
  impl_->outbound_fds.at(peer.value()) = fd;
}

void Node::accept_from(NodeId source) {
  const int fd = impl_->network.accept(impl_->listener_fd);
  if (fd < 0) {
    throw impl_->transport_error("cannot accept connection from peer");
  }
  impl_->inbound_fds.at(source.value()) = fd;
}

void Node::start() {
  const auto spawn = [this](auto body) {
    impl_->threads.emplace_back([this, body] { impl_->guarded(body); });
  };
  spawn([this] { impl_->reader_loop(); });
  for (std::uint32_t source = 0U; source < impl_->config.node_count(); ++source) {
    const int fd = impl_->inbound_fds[source];
    if (fd >= 0) {
      spawn([this, source, fd] { impl_->receiver_loop(source, fd); });
    }
  }
  spawn([this] { impl_->store_worker_loop(); });
}

void Node::join() {
  if (impl_->joined) {
    return;
  }
  impl_->joined = true;
  for (std::thread& thread : impl_->threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  if (impl_->listener_fd >= 0) {
    static_cast<void>(impl_->network.close(impl_->listener_fd));
    impl_->listener_fd = -1;
  }
  for (const std::vector<int>* descriptors : {&impl_->outbound_fds, &impl_->inbound_fds}) {
    for (const int fd : *descriptors) {
      if (fd >= 0) {
        static_cast<void>(impl_->network.close(fd));
      }
    }
  }
}

NodeStats Node::stats() const { return impl_->counters.snapshot(); }

const Store& Node::store() const noexcept { return impl_->store; }

NodeId Node::id() const noexcept { return impl_->id; }

void Node::inject_transport_fault(NodeId peer, bool inbound, NetworkFault fault,
                                  std::size_t occurrence) {
  const std::vector<int>& descriptors = inbound ? impl_->inbound_fds : impl_->outbound_fds;
  const std::size_t peer_index = peer.value();
  if (peer_index >= descriptors.size() || descriptors[peer_index] < 0 ||
      impl_->network.inject_fault(descriptors[peer_index], fault, occurrence) != 0) {
    throw impl_->transport_error("cannot inject transport fault");
  }
}

void Node::misroute_next_record() noexcept { impl_->misroute_pending = true; }

void Node::reserve_store(std::size_t hint) { impl_->store.reserve(hint); }

}
