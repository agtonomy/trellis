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

#include <fmt/core.h>
#include <unistd.h>

#include <memory>

#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/ipc/shm/shm_writer.hpp"
#include "trellis/core/logging.hpp"

namespace trellis::core {

namespace {
template <class MSG_T, std::enable_if_t<!std::is_same<MSG_T, google::protobuf::Message>::value>* = nullptr>
constexpr bool IsDynamicPublisher() {
  return false;
}

template <class MSG_T, std::enable_if_t<std::is_same<MSG_T, google::protobuf::Message>::value>* = nullptr>
constexpr bool IsDynamicPublisher() {
  return true;
}

std::string SanitizeTopicString(const std::string& topic) {
  std::string result = topic;
  std::replace(result.begin(), result.end(), '/', '-');
  return result;
}

template <typename T>
T GetConfigAttributeForTopic(const trellis::core::Config& config, const std::string& topic,
                             const std::string& attribute, T default_val) {
  const std::string topic_specific_attribute =
      fmt::format("trellis.publisher.topic_specific_attributes.{}.{}", SanitizeTopicString(topic), attribute);
  const std::string general_attribute = fmt::format("trellis.publisher.attributes.{}", attribute);

  const T topic_specific_config = config.AsIfExists<T>(topic_specific_attribute, default_val);
  const T general_config = config.AsIfExists<T>(general_attribute, default_val);

  if (topic_specific_config != default_val) {
    Log::Info("Overriding topic-specific attribute {} with {} for topic {}", attribute, topic_specific_config, topic);
    return topic_specific_config;
  }
  return general_config;
}

}  // namespace

/**
 * @brief Publisher implementation that uses shared memory and service discovery
 *        to broadcast messages to subscribers in other processes.
 *
 * This class handles the publication of protobuf messages over shared memory. It integrates
 * with the discovery system to broadcast presence and discover subscribers dynamically.
 *
 * @tparam MSG_T The message type (typically a protobuf message).
 */
template <typename MSG_T>
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
   */
  PublisherImpl(trellis::core::EventLoop loop, const std::string& topic,
                std::shared_ptr<discovery::Discovery> discovery, const trellis::core::Config& config)
      : topic_{topic},
        num_write_buffers_{GetConfigAttributeForTopic<size_t>(config, topic, "num_buffers", kDefaultNumWriterBuffers)},
        initial_buffer_size_{
            GetConfigAttributeForTopic<size_t>(config, topic, "initial_buffer_size", kDefaultInitialBufferSize)},
        max_buffer_size_{GetConfigAttributeForTopic<size_t>(config, topic, "max_buffer_size", kDefaultMaxBufferSize)},
        statistics_update_interval_ms_{GetConfigAttributeForTopic<unsigned>(
            config, topic, "statistics_update_interval_ms", kDefaultStatisticsUpdateIntervalMs)},
        writer_(loop, ::getpid(), num_write_buffers_, 0),
        discovery_{discovery},
        discovery_handle_{IsDynamicPublisher<MSG_T>()
                              ? discovery::Discovery::kInvalidRegistrationHandle
                              : discovery_->RegisterPublisher<MSG_T>(topic, writer_.GetMemoryFileList())},
        callback_handle_{discovery->AsyncReceiveSubscribers(
            [this](discovery::Discovery::EventType event, const discovery::Sample& sample) {
              ReceiveSubscriber(event, sample);
            })} {}

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
   * @param msg The message to send.
   * @return The timestamp used.
   */
  trellis::core::time::TimePoint Send(const MSG_T& msg) { return Send(msg, trellis::core::time::Now()); }

  /**
   * @brief Send a message at a specific timestamp.
   *
   * Serializes the message to a shared memory buffer and publishes it. If the underlying shared memory buffer needs to
   * be resized to accommodate the message, it will be done automatically.
   *
   * @param msg The message to send.
   * @param now The timestamp to associate with the message.
   * @return The timestamp used.
   */
  trellis::core::time::TimePoint Send(const MSG_T& msg, const trellis::core::time::TimePoint& now) {
    bool success{false};
    {  // Scope the mutex region to end before interacting with the discovery layer to avoid a potential deadlock
       // condition
      std::lock_guard guard(mutex_);
      ipc::shm::ShmFile::WriteInfo write_info = writer_.GetWriteAccess(buffer_size_);
      if (write_info.data == nullptr) {
        throw std::runtime_error("PublisherImpl::Send Failed to obtain write access!");
      }

      // Try to serialize; double the buffer size if necessary
      while (!(success = msg.SerializeToArray(write_info.data, write_info.size))) {
        if (buffer_size_ == max_buffer_size_) {
          throw std::runtime_error(fmt::format(
              "PublisherImpl::Send Failed to serialize to the max specified buffer size {}", max_buffer_size_));
        }
        buffer_size_ = std::min((buffer_size_ * 2), max_buffer_size_);
        writer_.ReleaseWriteAccess(now, /* bytes_written = */ 0, /* success = */ false);
        write_info = writer_.GetWriteAccess(buffer_size_);
        if (write_info.size != buffer_size_) {
          throw std::logic_error(
              fmt::format("PublisherImpl::Send Failed to increase buffer size. Requested = {} actual = {}",
                          buffer_size_, write_info.size));
        }
      }

      // Release the shared memory write lock after writing
      size_t bytes_written = static_cast<size_t>(msg.ByteSizeLong());
      writer_.ReleaseWriteAccess(now, bytes_written, success);
    }

    // Track message send statistics and update discovery
    if (success) {
      ++send_count_;
      if (!last_frequency_measurement_time_.has_value()) {
        last_frequency_measurement_time_ = now;
        last_frequency_measurement_send_count_ = send_count_;
      } else if (auto time_delta = now - last_frequency_measurement_time_.value();
                 time_delta >= std::chrono::milliseconds(statistics_update_interval_ms_)) {
        last_frequency_measurement_time_ = now;
        unsigned count_delta = send_count_ - last_frequency_measurement_send_count_;
        const auto duration_s = std::chrono::duration_cast<std::chrono::duration<double>>(time_delta).count();
        measured_frequency_hz = static_cast<double>(count_delta) / duration_s;
        last_frequency_measurement_send_count_ = send_count_;
        discovery_->UpdatePubSubStats(
            {.send_receive_count = send_count_, .measured_frequency_hz = measured_frequency_hz}, discovery_handle_);
      }
    }

    return now;
  }

 private:
  /**
   * @brief Handle notifications from discovery about new or removed subscribers.
   *
   * Adds or removes readers from the shared memory writer based on the PID of the subscriber.
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
      // so we delay registration until we receive this data
      if (discovery_handle_ == discovery::Discovery::kInvalidRegistrationHandle) {
        discovery_handle_ = discovery_->RegisterDynamicPublisher(
            topic_, writer_.GetMemoryFileList(), sample.topic().tdatatype().desc(), sample.topic().tdatatype().name());
      }
      writer_.AddReader(sample.id());
    } else if (event == discovery::Discovery::EventType::kNewUnregistration) {
      writer_.RemoveReader(sample.id());
    }
  }
  const std::string topic_;                                    ///< Topic name
  const size_t num_write_buffers_;                             ///< Number of buffers for the writer to use
  const size_t initial_buffer_size_;                           ///< Initial buffer size before any serialization attempt
  const size_t max_buffer_size_;                               ///< Maximum buffer size to attempt to use
  const unsigned statistics_update_interval_ms_;               ///< Interval for statistics calculations
  ipc::shm::ShmWriter writer_;                                 ///< Shared memory writer
  std::shared_ptr<discovery::Discovery> discovery_;            ///< Discovery service
  discovery::Discovery::RegistrationHandle discovery_handle_;  ///< Publisher registration handle
  discovery::Discovery::CallbackHandle callback_handle_;       ///< Subscriber callback handle
  size_t buffer_size_{initial_buffer_size_};                   ///< Buffer size for message serialization
  std::mutex mutex_;                                           ///< Mutex for thread safety

  // Statistics
  unsigned send_count_{0};          ///< Total messages sent
  double measured_frequency_hz{0};  ///< Frequency of messages sent
  std::optional<trellis::core::time::TimePoint>
      last_frequency_measurement_time_{};              ///< Last time frequency was measured
  unsigned last_frequency_measurement_send_count_{0};  ///< Send count at last frequency measurement
};

// Type aliases for shared ownership and dynamic use
template <typename T>
using Publisher = std::shared_ptr<PublisherImpl<T>>;

using DynamicPublisher = std::shared_ptr<PublisherImpl<google::protobuf::Message>>;

}  // namespace trellis::core

#endif  // TRELLIS_CORE_PUBLISHER_HPP_
