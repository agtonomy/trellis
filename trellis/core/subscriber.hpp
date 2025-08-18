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

#include <fmt/core.h>

#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/ipc/proto/dynamic_message_cache.hpp"
#include "trellis/core/ipc/shm/shm_reader.hpp"
#include "trellis/core/logging.hpp"

namespace trellis::core {

/**
 * @brief Implementation of a protobuf subscriber for shared memory message passing.
 *
 * This class handles discovery of publishers, connecting to shared memory regions,
 * and deserializing received messages (both statically and dynamically typed).
 *
 * @tparam MSG_T The protobuf message type expected by the subscriber.
 */
template <typename MSG_T>
class SubscriberImpl : public std::enable_shared_from_this<SubscriberImpl<MSG_T>> {
 public:
  /// @brief The type passed to the callback. A unique_ptr to the message.
  using PointerType = std::unique_ptr<MSG_T>;

  /**
   * @brief Callback type for fully parsed messages.
   * @param now The time the message was received.
   * @param msgtime The time the message was sent (embedded in header).
   * @param msg The deserialized message.
   */
  using Callback = std::function<void(const time::TimePoint& now, const time::TimePoint& msgtime, PointerType msg)>;

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
   * @brief Construct a subscriber and register for discovery notifications.
   *
   * @param loop The event loop for posting callbacks.
   * @param topic The name of the topic to subscribe to.
   * @param callback Callback invoked on receiving a parsed message (can be nullptr).
   * @param raw_callback Callback invoked on receiving raw bytes (can be nullptr).
   * @param update_sim_fn Function to update a simulation clock (can be nullptr).
   * @param discovery Pointer to the discovery service.
   */
  SubscriberImpl(trellis::core::EventLoop loop, std::string topic, Callback callback, RawCallback raw_callback,
                 UpdateSimulatedClockFunction update_sim_fn, std::shared_ptr<discovery::Discovery> discovery)
      : loop_{loop},
        topic_{topic},
        callback_{std::move(callback)},
        raw_callback_{std::move(raw_callback)},
        watchdog_timer_{},
        update_sim_fn_{std::move(update_sim_fn)},
        discovery_{discovery},
        discovery_handle_{discovery_->RegisterSubscriber<MSG_T>(topic)},
        subscriber_id_{discovery_->GetSampleId(discovery_handle_)},
        callback_handle_{discovery->AsyncReceivePublishers(
            [this](discovery::Discovery::EventType event, const discovery::Sample& sample) {
              ReceivePublisher(event, sample);
            })} {}

  /// @brief Destructor unregisters from discovery and stops callbacks.
  ~SubscriberImpl() {
    discovery_->StopReceive(callback_handle_);
    discovery_->Unregister(discovery_handle_);
  }

  SubscriberImpl(const SubscriberImpl&) = delete;
  SubscriberImpl& operator=(const SubscriberImpl&) = delete;
  SubscriberImpl(SubscriberImpl&&) = delete;
  SubscriberImpl& operator=(SubscriberImpl&&) = delete;

  /// @return True if a message has ever been received.
  bool DidReceive() const { return did_receive_; }

  /// @brief Sets a watchdog timer that is reset upon each received message.
  void SetWatchdogTimer(Timer timer) { watchdog_timer_ = std::move(timer); }

  /// @return The protobuf descriptor of the dynamic message type, if available.
  const google::protobuf::Descriptor* GetDescriptor() {
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

 private:
  /**
   * @brief Handles discovery events for publishers.
   *
   * Connects to new shared memory regions or disconnects from dropped ones.
   */
  void ReceivePublisher(discovery::Discovery::EventType event, const discovery::Sample& sample) {
    const auto& topic = sample.topic().tname();
    const auto topic_id = sample.id();

    if (topic != topic_) {
      return;
    }

    if (dynamic_message_cache_ == nullptr) {
      // Dynamic publishers may not contain the appropriate metadata, so we must check for existence
      const auto& desc = sample.topic().tdatatype().desc();
      const auto& name = sample.topic().tdatatype().name();
      if (!desc.empty() && !name.empty()) {
        dynamic_message_cache_ = std::make_unique<ipc::proto::DynamicMessageCache>(sample.topic().tdatatype().desc());
        dynamic_message_cache_->Create(sample.topic().tdatatype().name());
      }
    }

    auto it = readers_.find(topic_id);

    if (event == discovery::Discovery::EventType::kNewRegistration) {
      if (it == readers_.end()) {
        for (const auto& layer : sample.topic().tlayer()) {
          if (layer.type() == discovery::tl_shm) {
            const std::vector<std::string> memory_file_list(
                layer.par_layer().layer_par_shm().memory_file_list().begin(),
                layer.par_layer().layer_par_shm().memory_file_list().end());

            if (memory_file_list.empty()) {
              return;
            }

            std::weak_ptr<std::remove_reference_t<decltype(*this)>> weak_self = this->shared_from_this();
            readers_.emplace(topic_id, ipc::shm::ShmReader::Create(loop_, subscriber_id_, memory_file_list,
                                                                   [weak_self](ipc::shm::ShmFile::SMemFileHeader header,
                                                                               const void* data, size_t len) {
                                                                     if (auto self = weak_self.lock()) {
                                                                       self->ReceiveData(header, data, len);
                                                                     }
                                                                   }));
          }
        }
      }
    } else if (event == discovery::Discovery::EventType::kNewUnregistration) {
      if (it != readers_.end()) {
        readers_.erase(topic_id);
      }
    }
  }

  /**
   * @brief Called when a shared memory segment delivers new data.
   *
   * Handles throttling, parsing, and dispatching to user callbacks.
   */
  void ReceiveData(ipc::shm::ShmFile::SMemFileHeader header, const void* data, size_t len) {
    {
      did_receive_ = true;

      auto& last_seq = sequence_numbers_[header.writer_id];
      if (last_seq != 0 && (header.sequence != (last_seq + 1))) {
        // TODO (bsirang) implement a counter to collect metrics on this case
        trellis::core::Log::Warn(
            "Sequence number jump on topic {} from writer_id {}. Current = {} last = {} delta = {}", topic_,
            header.writer_id, header.sequence, last_seq, header.sequence - last_seq);
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

    PointerType msg = GetMessagePointer();

    if (msg == nullptr) {
      // We may hit this case if we're a dynamic subscriber and we don't yet have the message schema
      return;
    }

    if (callback_) {
      if (!msg->ParseFromArray(data, len)) {
        throw std::runtime_error(fmt::format("Failed to parse proto from shared memory from topic {} and writer_id {}",
                                             topic_, header.writer_id));
        return;
      }
    }

    const auto receive_time = trellis::core::time::Now();

    if (raw_callback_) {
      raw_callback_(receive_time, send_time, static_cast<const uint8_t*>(data), len);
    }

    if (callback_) callback_(receive_time, send_time, std::move(msg));
    if (update_sim_fn_) update_sim_fn_(send_time);
  }

  /// @brief Creates a new message instance for statically typed messages.
  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  PointerType GetMessagePointer() {
    return std::make_unique<MSG_T>();
  }

  /// @brief Retrieves a cached dynamic message for dynamically typed messages.
  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  PointerType GetMessagePointer() {
    return (dynamic_message_cache_ != nullptr) ? dynamic_message_cache_->Get() : nullptr;
  }

  trellis::core::EventLoop loop_;
  const std::string topic_;
  Callback callback_;
  RawCallback raw_callback_;
  Timer watchdog_timer_;
  UpdateSimulatedClockFunction update_sim_fn_;
  std::shared_ptr<discovery::Discovery> discovery_;
  discovery::Discovery::RegistrationHandle discovery_handle_;
  std::string subscriber_id_;
  discovery::Discovery::CallbackHandle callback_handle_;
  std::unordered_map<std::string, std::shared_ptr<ipc::shm::ShmReader>> readers_;
  bool did_receive_{false};
  std::unique_ptr<ipc::proto::DynamicMessageCache> dynamic_message_cache_{nullptr};
  std::atomic<unsigned> rate_throttle_interval_ms_{0};
  trellis::core::time::TimePoint last_callback_time_{};
  std::unordered_map<uint64_t, uint64_t> sequence_numbers_;
};

/// @brief Alias for consistency with other versions.
template <typename MSG_T>
using SubscriberImpl = SubscriberImpl<MSG_T>;

/// @brief Alias for shared pointer to subscriber.
template <typename MSG_T>
using Subscriber = std::shared_ptr<SubscriberImpl<MSG_T>>;

/// @brief Dynamic message subscriber (protobuf::Message).
using DynamicSubscriberImpl = SubscriberImpl<google::protobuf::Message>;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberImpl>;

/// @brief Alias for raw subscriber (unparsed message handler).
using SubscriberRawImpl = DynamicSubscriberImpl;
using SubscriberRaw = DynamicSubscriber;

}  // namespace trellis::core

#endif  //  TRELLIS_CORE_SUBSCRIBER_V2_HPP_
