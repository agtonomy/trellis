/*
 * Copyright (C) 2025 Agtonomy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef TRELLIS_CORE_PUBLISHER_HPP_
#define TRELLIS_CORE_PUBLISHER_HPP_

#include <fmt/format.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "trellis/core/constraints.hpp"
#include "trellis/core/converters.hpp"
#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/ipc/in_process_bus.hpp"
#include "trellis/core/ipc/shm/shm_writer.hpp"
#include "trellis/core/logging.hpp"
#include "trellis/core/statistics/frequency_calculator.hpp"
#include "trellis/core/time.hpp"

namespace trellis::core {

/**
 * @brief Schema information for dynamic publishers.
 *
 * Contains the serialized FileDescriptorSet and message type name needed to
 * register a dynamic publisher with discovery without waiting to learn
 * the schema from subscribers.
 */
struct DynamicPublisherSchema {
  std::string tdesc;  ///< Serialized FileDescriptorSet describing the message type
  std::string tname;  ///< Full protobuf message type name (e.g., "trellis.core.test.Test")
};

/**
 * @brief Publisher implementation that broadcasts messages to subscribers over the in-process transport, shared
 *        memory, or both.
 *
 * This class handles the publication of protobuf messages. A subscriber that's part of this process is reached through
 * ipc::InProcessBus; one in any other process through shared memory. It integrates with the discovery system to
 * broadcast presence and discover subscribers dynamically.
 *
 * This class supports opt-in automatic conversion from native C++ types to protobuf messages. Callers may specify a
 * native message type that is convertible to the serializable type. By default, ADL is used to find a `ToProto`
 * function that performs the conversion from the native type to the serializable type. Alternately, the caller may
 * specify the converter type and provide a concrete converter to the constructor. Free functions or functors can be
 * used; the type of free function `Foo` can be deduced easily via `decltype(&Foo)`.
 *
 * @tparam SerializableT The serializable message type (typically a protobuf message).
 * @tparam MsgT The message type (typically a native struct).
 * @tparam ConverterT The converter type (a free function or functor).
 */
template <typename SerializableT, typename MsgT = SerializableT,
          typename ConverterT = converters::DefaultToProto<MsgT, SerializableT>>
  requires constraints::_IsSerializable<SerializableT> &&
           (constraints::_IsDynamic<SerializableT, MsgT> || constraints::_IsConverter<ConverterT, MsgT, SerializableT>)
class PublisherImpl {
 public:
  static constexpr size_t kDefaultNumWriterBuffers = 5u;
  static constexpr size_t kDefaultInitialBufferSize = 1024 * 10;
  static constexpr size_t kDefaultMaxBufferSize = std::numeric_limits<size_t>::max();
  static constexpr unsigned kDefaultStatisticsUpdateIntervalMs = 2000u;

  /**
   * @brief Constructor.
   *
   * Initializes the shared memory writer and registers the publisher with the discovery service.
   * Also subscribes to subscriber discovery callbacks to track which subscribers exist.
   *
   * @param loop Event loop for I/O and timers.
   * @param topic The name of the topic to publish to.
   * @param discovery Shared pointer to the discovery service instance.
   * @param config The configuration tree to optionally pull values from
   * @param converter The function to convert from the message to serializable message
   * @param schema Optional schema for dynamic publishers. When provided, the publisher registers
   *               immediately with discovery instead of waiting to learn the schema from subscribers.
   * @param application_name an optional name of the application creating this publisher
   */
  PublisherImpl(trellis::core::EventLoop loop, const std::string& topic,
                std::shared_ptr<discovery::Discovery> discovery, const trellis::core::Config& config,
                ConverterT converter = {}, std::optional<DynamicPublisherSchema> schema = std::nullopt,
                std::string_view application_name = "")
      : topic_{topic},
        num_write_buffers_{config.GetConfigAttributeForTopic<size_t>(topic, "num_buffers", /* is_publisher = */ true,
                                                                     kDefaultNumWriterBuffers)},
        initial_buffer_size_{config.GetConfigAttributeForTopic<size_t>(
            topic, "initial_buffer_size", /* is_publisher = */ true, kDefaultInitialBufferSize)},
        // Initialized before the data members below so that we prevent leaking the discovery registrations in the event
        // that we throw an exception
        max_buffer_size_{[&]() {
          const auto max_buffer_size = config.GetConfigAttributeForTopic<size_t>(
              topic, "max_buffer_size", /* is_publisher = */ true, kDefaultMaxBufferSize);
          if (initial_buffer_size_ > max_buffer_size) {
            throw std::invalid_argument(
                fmt::format("PublisherImpl initial_buffer_size {} exceeds max_buffer_size {} for topic {}",
                            initial_buffer_size_, max_buffer_size, topic));
          }
          return max_buffer_size;
        }()},
        statistics_update_interval_ms_{config.GetConfigAttributeForTopic<unsigned>(
            topic, "statistics_update_interval_ms", true, kDefaultStatisticsUpdateIntervalMs)},
        writer_{[&]() -> std::optional<ipc::shm::ShmWriter> {
          // Loopback discovery never sees another process, so there can be no shared memory reader and the mapping
          // would go unread. Every other configuration builds one, as a remote subscriber may appear at any time.
          if (discovery->GetConfig().loopback_enabled) {
            return std::nullopt;
          }
          const std::string_view writer_name = application_name.empty() ? "trellis_publisher" : application_name;
          return std::optional<ipc::shm::ShmWriter>(std::in_place, writer_name, loop, ::getpid(), num_write_buffers_, 0,
                                                    config);
        }()},
        discovery_{discovery},
        discovery_handle_{[&]() {
          if constexpr (constraints::_IsDynamic<SerializableT, MsgT>) {
            if (schema.has_value()) {
              return discovery_->RegisterDynamicPublisher(topic, ShmFilePrefix(), ShmBufferCount(), schema->tdesc,
                                                          schema->tname);
            }
            return discovery::Discovery::kInvalidRegistrationHandle;
          } else {
            return discovery_->RegisterPublisher<SerializableT>(topic, ShmFilePrefix(), ShmBufferCount());
          }
        }()},
        callback_handle_{discovery->AsyncReceiveSubscribers(
            [this](discovery::Discovery::EventType event, const discovery::Sample& sample) {
              ReceiveSubscriber(event, sample);
            })},
        frequency_calculator_{statistics_update_interval_ms_},
        converter_{std::move(converter)} {}

  /**
   * @brief Destructor.
   *
   * Unregisters from discovery and stops receiving subscriber events.
   */
  ~PublisherImpl() {
    discovery_->StopReceive(callback_handle_);
    discovery_->Unregister(discovery_handle_);
  }

  /**
   * @brief Send a message immediately using the current time as the timestamp.
   *
   * Converts from the message type to the serializable message type.
   *
   * @param msg The message to send.
   * @return The timestamp used.
   */
  trellis::core::time::TimePoint Send(const MsgT& msg) { return Send(msg, trellis::core::time::Now()); }

  /**
   * @brief Send a message at a specific timestamp.
   *
   * First converts from the message type to the serialized message type, then routes it to whichever transports have
   * subscribers: the in-process transport, shared memory, or both. A shared memory buffer is sized to the message
   * before it is acquired, growing automatically if needed.
   *
   * @note At most one thread may send on a given publisher at a time. That thread need not be the node's event loop
   * thread -- publishing from a dedicated worker thread is an established pattern -- but two concurrent senders race
   * on the converter and on the send statistics. This holds on either transport: the in-process transport posts each
   * delivery onto the subscriber's loop, so a send never runs a subscriber callback on the sending thread.
   *
   * @param msg The message to send.
   * @param now The timestamp to associate with the message.
   * @return The timestamp used.
   */
  trellis::core::time::TimePoint Send(const MsgT& msg, const trellis::core::time::TimePoint& now) {
    if constexpr (std::is_same_v<MsgT, SerializableT>) {
      return SendSerializable(msg, now);
    } else {
      // The conversion runs before the shared memory slot is acquired, so user supplied converters never execute
      // while holding a write lock that readers contend on
      return SendSerializable(converter_(msg), now);
    }
  }

  /**
   * @brief Send raw bytes directly without protobuf serialization.
   *
   * @note Intended use case is for log replay, where the publishers don't need to know the message type and rely on
   * subscribers to properly deserialize the data.
   *
   * @note Carries the same one-sender-at-a-time constraint as `Send`.
   *
   * @param data Pointer to the raw bytes
   * @param size Size of the data in bytes
   * @param now The timestamp to associate with the message
   * @return The timestamp used
   */
  trellis::core::time::TimePoint SendBytes(const void* data, size_t size, const trellis::core::time::TimePoint& now) {
    return SendInternal(now, size, [data, size](ipc::shm::ShmFile::WriteInfo& write_info) {
      std::memcpy(write_info.data, data, size);
      return true;
    });
  }

  /**
   * @brief Send raw bytes directly using current time.
   */
  trellis::core::time::TimePoint SendBytes(const void* data, size_t size) {
    return SendBytes(data, size, trellis::core::time::Now());
  }

  /**
   * @brief Whether any subscriber this publisher knows of lives outside this process.
   *
   * Reflects what discovery has reported, so it only becomes meaningful once discovery has run. False while an
   * in-process subscriber exists means the in-process transport carries the message alone.
   *
   * @note Takes the send lock, so this is safe to call from any thread. Unlike SubscriberImpl::IsInProcess it needs
   * no separate atomic: the set it reports on is already covered by a lock this class takes on every send.
   *
   * @return True while at least one subscriber outside this process is registered on this topic.
   */
  bool HasRemoteSubscribers() const {
    std::lock_guard guard(mutex_);
    return !remote_subscriber_ids_.empty();
  }

 private:
  /**
   * @brief Serializes an already converted message into shared memory and publishes it.
   *
   * @param msg The message to serialize.
   * @param now The timestamp to associate with the message.
   * @return The timestamp used.
   */
  trellis::core::time::TimePoint SendSerializable(const SerializableT& msg, const trellis::core::time::TimePoint& now) {
    const size_t required_size = msg.ByteSizeLong();
    return SendInternal(now, required_size, [&msg](ipc::shm::ShmFile::WriteInfo& write_info) {
      return msg.SerializeToArray(write_info.data, write_info.size);
    });
  }

  using WriteFunc = std::function<bool(ipc::shm::ShmFile::WriteInfo&)>;

  /// @brief A message serialized for the in-process transport: the bytes, and the header delivered alongside them.
  struct InProcessMessage {
    std::shared_ptr<const std::vector<uint8_t>> payload;
    ipc::shm::ShmFile::SMemFileHeader header;
  };

  /**
   * @brief Internal send function
   *
   * Routes one message to the transports that currently hold a subscriber. One in this process is reached over the
   * in-process transport, one outside it through shared memory, and both can hold at once.
   *
   * Serialized exactly once either way. With no in-process subscriber the message goes straight into the shared
   * memory slot; otherwise it goes into a heap payload for the in-process transport, and a slot that needs the same
   * bytes copies them from there.
   *
   * @param now The timestamp to associate with the message.
   * @param required_size Exact number of bytes `write_fn` will write.
   * @param write_fn Serializes the message into the buffer it is given. Returns false if it could not, which is
   * fatal to the send.
   * @return The timestamp used.
   */
  trellis::core::time::TimePoint SendInternal(const trellis::core::time::TimePoint& now, const size_t required_size,
                                              WriteFunc write_fn) {
    // Checked before any transport is chosen: an oversized message is an error the caller must see whether or not
    // anyone is currently subscribed. max_buffer_size_ is const, so this needs no lock.
    if (required_size > max_buffer_size_) {
      throw std::runtime_error(
          fmt::format("PublisherImpl::Send message of {} bytes for topic {} exceeds the max buffer size {}",
                      required_size, topic_, max_buffer_size_));
    }

    // InProcessBus's own route count is the exact, local answer to whether this process holds a subscriber. See
    // InProcessBus::GetRouteCount for why discovery cannot be asked instead. A subscriber that appears after this
    // read is picked up by the next send.
    const bool to_in_process = inproc_route_count_->load(std::memory_order_relaxed) > 0;

    std::optional<InProcessMessage> inproc_message;
    {
      // The lock covers this publisher's own state and the shared memory slot. It ends before the in-process publish
      // and before UpdateStatistics, each of which takes a lock of its own; UpdateStatistics reaches the discovery
      // layer.
      std::lock_guard guard(mutex_);

      // Shared memory is written whenever it might be read: for a subscriber outside this process, and, when the
      // in-process transport is not in play, unconditionally, since a publisher with no subscribers at all writes as
      // it always has.
      const bool to_shared_memory = writer_.has_value() && (!to_in_process || !remote_subscriber_ids_.empty());

      if (to_in_process) {
        // Serialized once. A slot that needs the same bytes copies them below, which costs far less than a second
        // serialization pass.
        inproc_message = SerializeForInProcess(now, required_size, write_fn);
        if (to_shared_memory) {
          const std::vector<uint8_t>& payload = *inproc_message->payload;
          WriteToSharedMemory(now, payload.size(), [&payload](ipc::shm::ShmFile::WriteInfo& write_info) {
            std::memcpy(write_info.data, payload.data(), payload.size());
            return true;
          });
        }
      } else if (to_shared_memory) {
        // No intermediate buffer: write_fn serializes into the slot itself, which is what keeps the cross-process
        // path copy-free.
        WriteToSharedMemory(now, required_size, write_fn);
      }
    }

    if (inproc_message.has_value()) {
      inproc_bus_->Publish(topic_, inproc_message->header, std::move(inproc_message->payload));
    }
    UpdateStatistics(now);
    return now;
  }

  /**
   * @brief Serialize the message into a heap payload and synthesize the header the in-process transport delivers
   * with it.
   *
   * @note The caller must hold `mutex_`, which covers the sequence counter.
   *
   * @param now The send time, recorded in the header so a sim clock follower steps identically.
   * @param required_size Exact number of bytes `write_fn` will write.
   * @param write_fn Serializes the message into the buffer it is given.
   * @return The payload and its header.
   */
  InProcessMessage SerializeForInProcess(const trellis::core::time::TimePoint& now, const size_t required_size,
                                         const WriteFunc& write_fn) {
    auto payload = std::make_shared<std::vector<uint8_t>>(required_size);
    ipc::shm::ShmFile::WriteInfo write_info{payload->data(), payload->size()};
    if (!write_fn(write_info)) {
      throw std::runtime_error(
          fmt::format("PublisherImpl::Send failed to serialize {} bytes for topic {}", required_size, topic_));
    }
    ipc::shm::ShmFile::SMemFileHeader header{};
    header.data_size = required_size;
    header.sequence = ++inproc_sequence_;
    header.clock = time::TimePointToNanoseconds(now);
    header.writer_id = inproc_writer_id_;
    return InProcessMessage{std::move(payload), header};
  }

  /**
   * @brief Acquire a shared memory slot, fill it, and commit it.
   *
   * The only place a slot is written, whether the bytes are serialized into it directly or copied from a payload the
   * in-process transport is carrying too.
   *
   * @note The caller must hold `mutex_`, which covers `buffer_size_` and the writer's slot, and `writer_` must hold
   * a value.
   *
   * @param now The timestamp to associate with the message.
   * @param size Exact number of bytes `fill` will write.
   * @param fill Writes `size` bytes into the slot it is given. Returns false if it could not, which is fatal to the
   * send.
   */
  void WriteToSharedMemory(const trellis::core::time::TimePoint& now, const size_t size, const WriteFunc& fill) {
    // Ask for exactly what this message needs rather than growing by a geometric factor. The size is known here, so
    // overshooting only inflates a mapping that `ShmFile::Resize` can never shrink back; Resize rounds up to whole
    // pages on its own. `max` keeps the buffer monotonic, as Resize rejects shrinking.
    buffer_size_ = std::max(buffer_size_, std::min(size, max_buffer_size_));

    ipc::shm::ShmFile::WriteInfo write_info = writer_->GetWriteAccess(buffer_size_);
    if (write_info.data == nullptr) {
      // A null pointer means no slot was acquired, so there is no write lock to release here
      throw std::runtime_error("PublisherImpl::Send Failed to obtain write access!");
    }
    ipc::shm::ShmWriter::WriteAccessGuard write_guard{*writer_, now};

    if (write_info.size < size) {
      throw std::logic_error(
          fmt::format("PublisherImpl::Send buffer too small after acquiring write access. "
                      "Required = {} actual = {}",
                      size, write_info.size));
    }

    if (!fill(write_info)) {
      throw std::runtime_error(
          fmt::format("PublisherImpl::Send failed to serialize {} bytes into a {} byte buffer for topic {}", size,
                      write_info.size, topic_));
    }
    write_guard.Commit(size);
  }

  /// @brief The shared memory file prefix to advertise, empty when there is no writer.
  std::string ShmFilePrefix() const { return writer_.has_value() ? writer_->GetMemoryFilePrefix() : std::string{}; }

  /// @brief The shared memory buffer count to advertise, zero when there is no writer.
  uint32_t ShmBufferCount() const { return writer_.has_value() ? writer_->GetBufferCount() : 0u; }

  /**
   * @brief Handle notifications from discovery about new or removed subscribers.
   *
   * Records only the subscribers that shared memory has to reach, so each gets a reader and a send knows whether it
   * must also write to shared memory. A subscriber in this address space is left out: the in-process transport
   * reaches it by topic and counts its own routes, which is both cheaper and sound -- discovery could stop reporting
   * that subscriber while its in-process route is still live. A publisher can hold both kinds at once, and
   * `SendInternal` routes to whichever are present.
   *
   * @param event Whether the subscriber was registered or unregistered.
   * @param sample The discovery sample for the subscriber.
   */
  void ReceiveSubscriber(discovery::Discovery::EventType event, const discovery::Sample& sample) {
    std::lock_guard guard(mutex_);
    if (sample.topic().tname() != topic_) {
      return;
    }
    if (event == discovery::Discovery::EventType::kNewRegistration) {
      // In the case of dynamic publishers, we have to learn our metadata from subscribers,
      // so we delay registration until we receive this data.
      if constexpr (constraints::_IsDynamic<SerializableT, MsgT>) {
        if (discovery_handle_ == discovery::Discovery::kInvalidRegistrationHandle) {
          const std::string desc = discovery_->ResolveTopicDescriptor(sample);
          const auto& name = sample.topic().tdatatype().name();
          const bool sample_has_schema = !desc.empty() && !name.empty();
          if (sample_has_schema) {
            discovery_handle_ =
                discovery_->RegisterDynamicPublisher(topic_, ShmFilePrefix(), ShmBufferCount(), desc, name);
          }
        }
      }
      // Only the shared memory side needs per-subscriber bookkeeping. A subscriber in this process is reached over
      // the in-process transport, which routes by topic and tracks its own routes, so it is recorded nowhere here.
      if (!discovery::utils::SharesThisProcess(sample)) {
        remote_subscriber_ids_.insert(sample.id());
        if (writer_.has_value()) {
          writer_->AddReader(sample.id());
        }
      }
    } else if (event == discovery::Discovery::EventType::kNewUnregistration) {
      if (remote_subscriber_ids_.erase(sample.id()) > 0 && writer_.has_value()) {
        writer_->RemoveReader(sample.id());
      }
    }
  }
  void UpdateStatistics(const trellis::core::time::TimePoint& now) {
    frequency_calculator_.IncrementCount();
    if (frequency_calculator_.UpdateFrequency(now)) {
      if (discovery_handle_ != discovery::Discovery::kInvalidRegistrationHandle) {
        discovery_->UpdatePubSubStats({.send_receive_count = frequency_calculator_.GetTotalCount(),
                                       .measured_frequency_hz = frequency_calculator_.GetFrequencyHz(),
                                       .max_burst_size = 0,
                                       .message_drops = 0},
                                      discovery_handle_);
      }
    }
  }
  const std::string topic_;                                    ///< Topic name
  const size_t num_write_buffers_;                             ///< Number of buffers for the writer to use
  const size_t initial_buffer_size_;                           ///< Initial buffer size before any serialization attempt
  const size_t max_buffer_size_;                               ///< Maximum buffer size to attempt to use
  const unsigned statistics_update_interval_ms_;               ///< Interval for statistics calculations
  std::optional<ipc::shm::ShmWriter> writer_;                  ///< Shared memory writer, absent under loopback
  std::shared_ptr<discovery::Discovery> discovery_;            ///< Discovery service
  discovery::Discovery::RegistrationHandle discovery_handle_;  ///< Publisher registration handle
  discovery::Discovery::CallbackHandle callback_handle_;       ///< Subscriber callback handle
  size_t buffer_size_{initial_buffer_size_};                   ///< Buffer size for message serialization
  /// Serializes the sender against ReceiveSubscriber, which mutates writer_ from the event loop thread. This does not
  /// make concurrent sends safe: the converter and UpdateStatistics both run outside it. Mutable so that the const
  /// HasRemoteSubscribers can take it.
  mutable std::mutex mutex_;
  statistics::FrequencyCalculator frequency_calculator_;  ///< Frequency calculation utility
  ConverterT converter_;                                  ///< Function to convert to serialized message type
  /// Held for this publisher's lifetime so a send never pays to look it up. Any publisher may acquire a same-process
  /// subscriber, so this is unconditional. Until one does, InProcessBus holds only the empty topic entry that the
  /// counter below creates.
  const std::shared_ptr<ipc::InProcessBus> inproc_bus_{ipc::InProcessBus::Instance()};
  /// Live subscriber count on this topic across the whole process, maintained by InProcessBus. Read on the send path
  /// to decide whether an in-process payload is worth building. Declared after inproc_bus_, which it is obtained
  /// from.
  const ipc::InProcessBus::RouteCount inproc_route_count_{inproc_bus_->GetRouteCount(topic_)};
  /// Discovery ids of the subscribers outside this process, which decide whether a send also reaches shared memory.
  /// Guarded by mutex_.
  std::unordered_set<std::string> remote_subscriber_ids_;
  /// Process-unique, so a subscriber tracks each publisher on a topic separately. In-process path only.
  const uint64_t inproc_writer_id_{[]() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
  }()};
  uint64_t inproc_sequence_{0};  ///< Monotonic per-message sequence, in-process path only
};

// Type aliases for shared ownership and dynamic use
template <typename SerializableT, typename MsgT = SerializableT,
          typename ConverterT = converters::DefaultToProto<MsgT, SerializableT>>
using Publisher = std::shared_ptr<PublisherImpl<SerializableT, MsgT, ConverterT>>;

using DynamicPublisher = Publisher<google::protobuf::Message>;

}  // namespace trellis::core

#endif  // TRELLIS_CORE_PUBLISHER_HPP_
