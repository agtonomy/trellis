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

#ifndef TRELLIS_CORE_SUBSCRIBER_V2_HPP_
#define TRELLIS_CORE_SUBSCRIBER_V2_HPP_

#include <fmt/format.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>

#include "trellis/core/constraints.hpp"
#include "trellis/core/converters.hpp"
#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/ipc/in_process_bus.hpp"
#include "trellis/core/ipc/proto/dynamic_message_cache.hpp"
#include "trellis/core/ipc/shm/shm_reader.hpp"
#include "trellis/core/logging.hpp"
#include "trellis/core/statistics/frequency_calculator.hpp"
#include "trellis/core/statistics/latency_calculator.hpp"
#include "trellis/core/subscriber_base.hpp"
#include "trellis/core/timer.hpp"

namespace trellis::core {

/**
 * @brief Implementation of a protobuf subscriber that receives messages over the in-process transport, shared
 *        memory, or both.
 *
 * This class handles discovery of publishers, registering one ipc::InProcessBus route for the publishers in this
 * process, connecting to shared memory regions for the rest, and deserializing received messages (both statically
 * and dynamically typed).
 *
 * This class supports opt-in automatic conversion from serializable types to native C++ types. Callers may specify a
 * native message type that is convertible from the serializable type. By default, ADL is used to find a `FromProto`
 * function that performs the conversion from the serializable type to the native type. Alternately, the caller may
 * specify the converter type and provide a concrete converter to the constructor. Free functions or functors can be
 * used; the type of free function `Foo` can be deduced easily via `decltype(&Foo)`.
 *
 * @tparam SerializableT The serializable message type (typically a protobuf message).
 * @tparam MsgT The message type (typically a native struct).
 * @tparam ConverterT The converter type (a free function or functor).
 */
template <typename SerializableT, typename MsgT = SerializableT,
          typename ConverterT = converters::DefaultFromProto<SerializableT, MsgT>>
  requires constraints::_IsSerializable<SerializableT> && (constraints::_IsDynamic<SerializableT, MsgT> ||
                                                           constraints::_IsConverter<ConverterT, SerializableT, MsgT>)
class SubscriberImpl : public SubscriberBase,
                       public std::enable_shared_from_this<SubscriberImpl<SerializableT, MsgT, ConverterT>> {
 public:
  static constexpr unsigned kDefaultStatisticsUpdateIntervalMs = 1000u;

  /// @brief The message pointer type used in the callback
  using MsgTypePtr = std::unique_ptr<MsgT>;

  /**
   * @brief Callback type for fully parsed messages.
   * @param now The time the message was received.
   * @param msgtime The time the message was sent (embedded in header).
   * @param msg The deserialized message.
   */
  using Callback = std::function<void(const time::TimePoint& now, const time::TimePoint& msgtime, MsgTypePtr msg)>;

  /**
   * @brief Callback type for raw, unparsed messages.
   * @param now The time the message was received.
   * @param msgtime The time the message was sent (embedded in header).
   * @param data Pointer to raw message bytes.
   * @param size Length of the message.
   */
  using RawCallback =
      std::function<void(const time::TimePoint& now, const time::TimePoint& msgtime, const uint8_t*, size_t)>;

  /// @brief Optional function to update a simulated clock when a message is received.
  using UpdateSimulatedClockFunction = std::function<void(const time::TimePoint&)>;

  /**
   * @brief Creates a subscriber and starts it. This is the only way to construct one.
   *
   * The watchdog and throttle are applied before the subscriber registers with discovery, so neither is changed while
   * the loop may already be delivering to it.
   *
   * @param loop The event loop for posting callbacks.
   * @param topic The name of the topic to subscribe to.
   * @param callback Callback invoked on receiving a parsed message (can be nullptr).
   * @param raw_callback Callback invoked on receiving raw bytes (can be nullptr).
   * @param update_sim_fn Function to update a simulation clock (can be nullptr).
   * @param discovery Pointer to the discovery service.
   * @param config The configuration tree to optionally pull values from.
   * @param watchdog_timeout_ms Optional watchdog timeout. Both it and watchdog_callback are needed for a watchdog.
   * @param watchdog_callback Optional callback for when no message arrives within the timeout. It only fires once a
   * message has been received.
   * @param max_frequency Optional maximum frequency to throttle the callback to.
   * @param converter The function to convert from the serializable message
   * @param before_start Optional hook run with the subscriber after it is configured and before it starts, for a
   * caller that must record the subscriber before any message can reach it.
   * @return the started subscriber
   */
  static std::shared_ptr<SubscriberImpl> Create(
      trellis::core::EventLoop loop, std::string topic, Callback callback, RawCallback raw_callback,
      UpdateSimulatedClockFunction update_sim_fn, std::shared_ptr<discovery::Discovery> discovery,
      const trellis::core::Config& config, std::optional<unsigned> watchdog_timeout_ms = {},
      TimerImpl::Callback watchdog_callback = {}, std::optional<double> max_frequency = {}, ConverterT converter = {},
      const std::function<void(const std::shared_ptr<SubscriberImpl>&)>& before_start = {}) {
    // Not make_shared: it cannot reach the private constructor.
    const std::shared_ptr<SubscriberImpl> subscriber{
        new SubscriberImpl(std::move(loop), std::move(topic), std::move(callback), std::move(raw_callback),
                           std::move(update_sim_fn), std::move(discovery), config, std::move(converter))};
    if (max_frequency.has_value()) {
      subscriber->SetMaxFrequencyThrottle(max_frequency.value());
    }
    if (watchdog_timeout_ms.has_value() && watchdog_callback != nullptr) {
      // Built here rather than by the caller because it needs a weak_ptr to the subscriber. It is an application timer
      // on the caller's loop, the same as one made by Node::CreateTimer.
      subscriber->watchdog_timer_ = std::make_shared<OneShotTimerImpl>(
          subscriber->loop_,
          [watchdog_callback = std::move(watchdog_callback),
           weak_self = std::weak_ptr<SubscriberImpl>(subscriber)](const time::TimePoint& now) {
            // Fire only if messages were previously received.
            const auto self = weak_self.lock();
            if (self && self->DidReceive()) {
              watchdog_callback(now);
            }
          },
          watchdog_timeout_ms.value());
    }
    if (before_start) {
      before_start(subscriber);
    }
    subscriber->Start();
    return subscriber;
  }

  /**
   * @brief Stops the subscriber, then hands its timers and shared memory readers to the loop to be destroyed there.
   *
   * Timers and readers must be stopped and destroyed on the loop's thread, or while the loop is not running (see
   * TimerImpl). The last reference to a subscriber is often dropped on another thread.
   */
  ~SubscriberImpl() {
    Stop();
    // On the loop's thread, or with the loop stopped, the members can simply be destroyed with the rest.
    if ((*loop_).get_executor().running_in_this_thread() || (*loop_).stopped()) {
      return;
    }
    // Nothing on the loop can still be using these. Every path into ReceiveData() and UpdateStatistics() locks a
    // weak_ptr to this subscriber first, and the discovery callback was removed by Stop(). If the loop never runs
    // again, asio destroys this handler, and what it owns, when the io_context is destroyed.
    asio::post(*loop_, [statistics_timer = std::move(statistics_timer_), watchdog_timer = std::move(watchdog_timer_),
                        readers = std::move(readers_)]() {});
  }

  /// @brief unregisters from discovery and stops callbacks.
  void Stop() {
    discovery_->StopReceive(callback_handle_);
    discovery_->Unregister(discovery_handle_);
    discovery_handle_ = discovery::Discovery::kInvalidRegistrationHandle;
    if (statistics_timer_) {
      statistics_timer_->Stop();
    }
    if (watchdog_timer_) {
      watchdog_timer_->Stop();
    }

    for (const auto& r : readers_ | std::views::values) {
      r->Stop();
    }

    UnsubscribeInProcess();
    inproc_publisher_ids_.clear();
  }

  SubscriberImpl(const SubscriberImpl&) = delete;
  SubscriberImpl& operator=(const SubscriberImpl&) = delete;
  SubscriberImpl(SubscriberImpl&&) = delete;
  SubscriberImpl& operator=(SubscriberImpl&&) = delete;

  /// @return True if a message has ever been received.
  bool DidReceive() const { return did_receive_; }

  /// @brief Get the topic name this subscriber is subscribed to.
  const std::string& GetTopic() const override { return topic_; }

  /**
   * @brief Whether this subscriber is being served by the in-process transport rather than shared memory.
   *
   * Reflects what the discovered publishers advertised, so it only becomes meaningful once discovery has run.
   *
   * @note Backed by its own atomic rather than by inproc_bus_, which the discovery thread mutates, so that this is
   * safe to call from any thread.
   *
   * @return True while at least one same-process publisher is routing this topic through the in-process transport.
   */
  bool IsInProcess() const { return inproc_active_.load(std::memory_order_relaxed); }

  /// @return The protobuf descriptor of the dynamic message type, if available.
  const google::protobuf::Descriptor* GetDescriptor() const {
    if (dynamic_message_cache_ == nullptr) {
      return nullptr;
    }
    const auto& msg = dynamic_message_cache_->Get();
    return msg->GetDescriptor();
  }

  /// @brief Throttles the callback to a maximum frequency in Hz.
  void SetMaxFrequencyThrottle(double frequency_hz) {
    if (frequency_hz != 0.0) {
      const unsigned interval_ms = static_cast<unsigned>(1000 / frequency_hz);
      if (interval_ms != 0) {
        rate_throttle_interval_ms_ = interval_ms;
      }
    }
  }

  /// @brief Gets the latency stats since last time it was called and resets the stats
  /// @return latency stats, with min, mean, max latency in microseconds
  statistics::LatencyCalculator::Stats GetLatestLatencyStats() override { return latency_calculator_.GetAndReset(); }

 private:
  /**
   * @brief Construct a subscriber. Private: Create() constructs one and then starts it.
   *
   * @param loop The event loop for posting callbacks.
   * @param topic The name of the topic to subscribe to.
   * @param callback Callback invoked on receiving a parsed message (can be nullptr).
   * @param raw_callback Callback invoked on receiving raw bytes (can be nullptr).
   * @param update_sim_fn Function to update a simulation clock (can be nullptr).
   * @param discovery Pointer to the discovery service.
   * @param config The configuration tree to optionally pull values from.
   * @param converter The function to convert from the serializable message
   */
  SubscriberImpl(trellis::core::EventLoop loop, std::string topic, Callback callback, RawCallback raw_callback,
                 UpdateSimulatedClockFunction update_sim_fn, std::shared_ptr<discovery::Discovery> discovery,
                 const trellis::core::Config& config, ConverterT converter = {})
      : loop_{loop},
        topic_{topic},
        config_{config},
        callback_{std::move(callback)},
        raw_callback_{std::move(raw_callback)},
        update_sim_fn_{std::move(update_sim_fn)},
        statistics_update_interval_ms_{config.GetConfigAttributeForTopic<unsigned>(
            topic, "statistics_update_interval_ms", /* is_publisher = */ false, kDefaultStatisticsUpdateIntervalMs)},
        discovery_{std::move(discovery)},
        discovery_handle_{discovery_->RegisterSubscriber<SerializableT>(topic)},
        subscriber_id_{discovery_->GetSampleId(discovery_handle_)},
        callback_handle_{discovery::Discovery::kInvalidCallbackHandle},
        frequency_calculator_{statistics_update_interval_ms_},
        converter_{std::move(converter)} {}

  /**
   * @brief Register for discovery notifications and start the statistics timer.
   *
   * Called once by Create(), after a std::shared_ptr owns the subscriber and it is configured.
   *
   * Registration cannot happen in the constructor. Discovery may deliver to the callback on the loop thread as soon
   * as it is registered, before a std::shared_ptr owns the subscriber and weak_from_this() is usable. The statistics
   * timer fires immediately and has the same constraint.
   */
  void Start() {
    statistics_timer_ = std::make_shared<PeriodicTimerImpl>(
        loop_,
        [weak_self = this->weak_from_this()](const time::TimePoint& now) {
          if (const auto self = weak_self.lock()) {
            self->UpdateStatistics(now);
          }
        },
        statistics_update_interval_ms_, 0, TimerKind::kManagement);
    // `this` is safe: Discovery runs this under its callback lock, which Stop()'s StopReceive() waits on and removes.
    callback_handle_ = discovery_->AsyncReceivePublishers(
        [this](const discovery::Discovery::EventType event, const discovery::Sample& sample) {
          ReceivePublisher(event, sample);
        });
  }

  using SerializableTypePtr = std::unique_ptr<SerializableT>;

  /// @brief True when this subscriber can parse into a reusable concrete member scratch: it converts a concrete
  /// serializable type into a distinct MsgT. False for the pass-through case (SerializableT == MsgT, which hands its
  /// owning pointer to the callback) and the dynamic subscriber (abstract google::protobuf::Message, which cannot be
  /// held by value), neither of which can hold a reusable concrete scratch message.
  static constexpr bool kCanUseScratch =
      !std::same_as<SerializableT, MsgT> && !std::same_as<SerializableT, google::protobuf::Message>;

  /// @brief Type of the reused intermediate parse buffer. A concrete SerializableT when we can use a scratch,
  /// otherwise an empty std::monostate
  using ConverterScratch = std::conditional_t<kCanUseScratch, SerializableT, std::monostate>;

  /**
   * @brief Handles discovery events for publishers.
   *
   * Publishers advertising the in-process layer from this process share a single in-process route, registered when
   * the first appears and dropped when the last goes away. Every other publisher gets a shared memory reader, connected
   * on registration and disconnected when it drops.
   */
  void ReceivePublisher(const discovery::Discovery::EventType event, const discovery::Sample& sample) {
    const auto& topic = sample.topic().tname();
    const auto& topic_id = sample.id();

    if (topic != topic_) {
      return;
    }

    if (dynamic_message_cache_ == nullptr) {
      // Dynamic publishers may not contain the appropriate metadata, so we must check for existence
      const std::string desc = discovery_->ResolveTopicDescriptor(sample);
      const auto& name = sample.topic().tdatatype().name();
      if (!desc.empty() && !name.empty()) {
        dynamic_message_cache_ = std::make_unique<ipc::proto::DynamicMessageCache>(desc);
        dynamic_message_cache_->Create(name);
      }
    }
    if (discovery::utils::SharesThisProcess(sample)) {
      // The in-process transport delivers by topic, so every same-process publisher shares one route and none of them
      // gets an shm reader. Track which publishers are in process so the route outlives exactly as long as one of
      // them does.
      if (event == discovery::Discovery::EventType::kNewRegistration) {
        inproc_publisher_ids_.insert(topic_id);
        SubscribeInProcess();
      } else if (event == discovery::Discovery::EventType::kNewUnregistration) {
        inproc_publisher_ids_.erase(topic_id);
        if (inproc_publisher_ids_.empty()) {
          UnsubscribeInProcess();
        }
      }
      return;
    }

    if (readers_.contains(topic_id)) {
      if (event == discovery::Discovery::EventType::kNewUnregistration) {
        readers_.erase(topic_id);
      }
    } else {
      if (event == discovery::Discovery::EventType::kNewRegistration) {
        for (const auto& layer : sample.topic().tlayer()) {
          if (layer.type() == discovery::tl_shm) {
            const std::string& memory_file_prefix = layer.par_layer().layer_par_shm().memory_file_prefix();
            const uint32_t buffer_count = layer.par_layer().layer_par_shm().buffer_count();

            if (buffer_count == 0) {
              return;
            }

            // Generate memory file list from prefix and count
            std::vector<std::string> memory_file_list;
            for (uint32_t i = 0; i < buffer_count; ++i) {
              memory_file_list.push_back(fmt::format("{}_{:03}", memory_file_prefix, i));
            }

            std::weak_ptr<SubscriberImpl> weak_self = this->weak_from_this();
            auto reader = ipc::shm::ShmReader::Create(
                loop_, subscriber_id_, memory_file_list,
                [weak_self](ipc::shm::ShmFile::SMemFileHeader header, const void* data, size_t len) {
                  if (auto self = weak_self.lock()) {
                    self->ReceiveData(header, data, len);
                  }
                },
                config_);
            // Only add the reader to the container if it was properly initialized
            if (reader && reader->IsInitialized()) {
              readers_.emplace(topic_id, std::move(reader));
            } else {
              trellis::core::Log::Warn("Failed to initialize ShmReader for topic {}. Did the publisher go offline?",
                                       topic_);
            }
          }
        }
      }
    }
  }

  /// @brief Register this subscriber's single per-topic route with InProcessBus, if one is not registered.
  void SubscribeInProcess() {
    // inproc_bus_ is the registered flag, so the second same-process publisher on this topic lands here as a no-op.
    if (inproc_bus_ != nullptr) {
      return;
    }

    const std::weak_ptr<SubscriberImpl> weak_self = this->weak_from_this();
    inproc_bus_ = ipc::InProcessBus::Instance();
    inproc_bus_handle_ = inproc_bus_->Subscribe(
        topic_, loop_, [weak_self](const ipc::shm::ShmFile::SMemFileHeader& header, const void* data, size_t len) {
          const std::shared_ptr<SubscriberImpl> self = weak_self.lock();
          if (self != nullptr) {
            self->ReceiveData(header, data, len);
          }
        });
    inproc_active_.store(true, std::memory_order_relaxed);
  }

  /// @brief Drop the in-process route, if one is registered.
  void UnsubscribeInProcess() {
    if (inproc_bus_ == nullptr) {
      return;
    }
    inproc_bus_->Unsubscribe(topic_, inproc_bus_handle_);
    inproc_bus_handle_ = ipc::InProcessBus::kNoHandle;
    inproc_bus_.reset();
    inproc_active_.store(false, std::memory_order_relaxed);
  }

  /**
   * @brief Called when either transport delivers new data: an InProcessBus route or a shared memory reader.
   *
   * Handles throttling, parsing, and dispatching to user callbacks.
   */
  void ReceiveData(ipc::shm::ShmFile::SMemFileHeader header, const void* data, size_t len) {
    {
      did_receive_ = true;

      auto& last_seq = sequence_numbers_[header.writer_id];
      if (last_seq != 0 && (header.sequence != (last_seq + 1))) {
        // Track dropped messages based on sequence number gaps
        const auto dropped = header.sequence - last_seq - 1;
        dropped_message_count_ += dropped;
        trellis::core::Log::Warn(
            "Sequence number jump on topic {} from writer_id {}. Current = {} last = {} delta = {} dropped = {}",
            topic_, header.writer_id, header.sequence, last_seq, header.sequence - last_seq, dropped);
      }
      last_seq = header.sequence;
    }

    if (watchdog_timer_) watchdog_timer_->Reset();

    const auto send_time = time::NanosecondsToTimePoint(header.clock);
    const unsigned interval_ms = rate_throttle_interval_ms_.load();
    if (interval_ms) {
      const bool enough_time_elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(send_time - last_callback_time_).count() > interval_ms;
      if (enough_time_elapsed) {
        last_callback_time_ = send_time;
      } else {
        return;
      }
    }

    // owned_msg holds a per-message owning pointer; msg is a non-owning view of whatever object we parse into,
    // wherever it lives. Both stay null/empty unless a parsed callback_ is registered.
    SerializableTypePtr owned_msg;
    SerializableT* msg = nullptr;

    // Only materialize/parse a message when a parsed callback is registered. Raw subscribers forward the delivered
    // bytes directly and need neither the message nor the schema.
    if (callback_) {
      if constexpr (kCanUseScratch) {
        // Converter subscribers parse into a reused scratch proto to avoid per msg allocs that are thrown away
        msg = &serializable_scratch_;
      } else {
        // Normal proto and dynamic subscribers acquire a fresh pointer for the callback.
        owned_msg = GetSerializableMessagePointer();
        if (owned_msg == nullptr) {
          // We may hit this case if we're a dynamic subscriber and we don't yet have the message schema
          return;
        }
        msg = owned_msg.get();
      }

      if (!msg->ParseFromArray(data, len)) {
        throw std::runtime_error(
            fmt::format("Failed to parse proto from topic {} and writer_id {}", topic_, header.writer_id));
      }
    }

    // Step the simulated clock forward to this message's send_time BEFORE recording receive_time and
    // invoking callbacks. Under simulated time the clock is driven by inbound messages, so send_time is
    // "now" at the instant of delivery. Advancing first means:
    //   - receive_time == send_time, so latency is non-negative (rather than a message that appears to
    //     arrive before it was sent),
    //   - application logic calling time::Now() inside the callback observes the message's time, and
    //   - any timers due up to send_time fire first, preserving event-time order.
    // The subscriber runs on the event loop thread, so update_sim_fn_ steps the clock synchronously here
    // (unlike Node::UpdateSimulatedClock, which defers to the loop for off-thread callers). This is a
    // no-op outside simulated-time mode, leaving wall-clock behavior unchanged.
    if (update_sim_fn_) update_sim_fn_(send_time);

    const auto receive_time = trellis::core::time::Now();

    // Track message receive statistics
    frequency_calculator_.IncrementCount();
    latency_calculator_.RecordLatency(receive_time, send_time);

    if (raw_callback_) {
      raw_callback_(receive_time, send_time, static_cast<const uint8_t*>(data), len);
    }

    if (callback_) {
      if constexpr (std::same_as<SerializableT, MsgT>) {
        callback_(receive_time, send_time, std::move(owned_msg));
      } else {
        callback_(receive_time, send_time, std::make_unique<MsgT>(converter_(*msg)));
      }
    }
  }

  /// @brief Creates a new message instance for statically typed messages.
  template <class FOO = SerializableT,
            std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  SerializableTypePtr GetSerializableMessagePointer() {
    return std::make_unique<SerializableT>();
  }

  /// @brief Retrieves a cached dynamic message for dynamically typed messages.
  template <class FOO = SerializableT, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  SerializableTypePtr GetSerializableMessagePointer() {
    return (dynamic_message_cache_ != nullptr) ? dynamic_message_cache_->Get() : nullptr;
  }

  /**
   * @brief Updates statistics for message reception frequency and burst size.
   *
   * Called periodically by the statistics timer to calculate frequency and update discovery.
   *
   * @param now Current timestamp for frequency calculation.
   */
  void UpdateStatistics(const trellis::core::time::TimePoint& now) {
    if (frequency_calculator_.UpdateFrequency(now)) {
      // Collect max burst size from all readers
      unsigned max_burst_size = 0;
      for (const auto& reader : readers_ | std::views::values) {
        if (reader) {
          const auto& metrics = reader->GetMetrics();
          max_burst_size = std::max(max_burst_size, metrics.socket_event.max_burst_size);
        }
      }

      if (discovery_handle_ != discovery::Discovery::kInvalidRegistrationHandle) {
        discovery_->UpdatePubSubStats({.send_receive_count = frequency_calculator_.GetTotalCount(),
                                       .measured_frequency_hz = frequency_calculator_.GetFrequencyHz(),
                                       .max_burst_size = max_burst_size,
                                       .message_drops = dropped_message_count_},
                                      discovery_handle_);
      }
    }
  }

  trellis::core::EventLoop loop_;
  const std::string topic_;
  const trellis::core::Config config_;
  Callback callback_;
  RawCallback raw_callback_;
  Timer watchdog_timer_;
  UpdateSimulatedClockFunction update_sim_fn_;
  const unsigned statistics_update_interval_ms_;  ///< Interval for statistics calculations
  std::shared_ptr<discovery::Discovery> discovery_;
  discovery::Discovery::RegistrationHandle discovery_handle_;
  std::string subscriber_id_;
  discovery::Discovery::CallbackHandle callback_handle_;
  std::unordered_map<std::string, std::shared_ptr<ipc::shm::ShmReader>> readers_;
  /// Held while a route is registered; non-null means registered. Owning it keeps Stop() away from the singleton
  /// accessor, so a subscriber torn down during static destruction still unsubscribes against a live InProcessBus.
  std::shared_ptr<ipc::InProcessBus> inproc_bus_;
  ipc::InProcessBus::Handle inproc_bus_handle_{ipc::InProcessBus::kNoHandle};  ///< Handle for Unsubscribe
  /// Discovery ids of the same-process publishers on this topic. They share the single route above, so it is
  /// registered when the first one appears and dropped when the last one goes away.
  std::unordered_set<std::string> inproc_publisher_ids_;
  std::atomic<bool> inproc_active_{false};  ///< Mirrors inproc_bus_ for IsInProcess(), which any thread may call.
  bool did_receive_{false};
  std::unique_ptr<ipc::proto::DynamicMessageCache> dynamic_message_cache_{nullptr};
  std::atomic<unsigned> rate_throttle_interval_ms_{0};
  trellis::core::time::TimePoint last_callback_time_{};
  std::unordered_map<uint64_t, uint64_t> sequence_numbers_;
  Timer statistics_timer_;                                ///< Timer for periodic statistics updates
  statistics::FrequencyCalculator frequency_calculator_;  ///< Frequency calculation utility
  statistics::LatencyCalculator latency_calculator_;      ///< Latency calculation utility
  unsigned dropped_message_count_{0};                     ///< Total number of dropped messages detected
  ConverterT converter_;                                  ///< Function to convert to serialized message type
  // no_unique_address makes sure in the std::monostate case, it occupies no extra space.
  [[no_unique_address]] ConverterScratch serializable_scratch_{};  ///< Reused parse buffer for the converter path
};

/// @brief Alias for shared pointer to subscriber.
template <typename SerializableT, typename MsgT = SerializableT,
          typename ConverterT = converters::DefaultFromProto<SerializableT, MsgT>>
using Subscriber = std::shared_ptr<SubscriberImpl<SerializableT, MsgT, ConverterT>>;

/// @brief Dynamic message subscriber (protobuf::Message).
using DynamicSubscriberImpl = SubscriberImpl<google::protobuf::Message>;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberImpl>;

/// @brief Alias for raw subscriber (unparsed message handler).
using SubscriberRawImpl = DynamicSubscriberImpl;
using SubscriberRaw = DynamicSubscriber;

}  // namespace trellis::core

#endif  //  TRELLIS_CORE_SUBSCRIBER_V2_HPP_
