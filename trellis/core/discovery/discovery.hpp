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

#ifndef TRELLIS_CORE_DISCOVERY_DISCOVERY_HPP_
#define TRELLIS_CORE_DISCOVERY_DISCOVERY_HPP_

#include <optional>
#include <queue>

#include "trellis/core/config.hpp"
#include "trellis/core/discovery/types.hpp"
#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/proto/rpc/types.hpp"
#include "trellis/core/timer.hpp"
#include "trellis/network/udp.hpp"

namespace trellis::core::discovery {

/**
 * @brief Service discovery class for Trellis shared memory IPC
 *
 * This class is responsible for broadcasting and receiving discovery messages over UDP to allow processes to find each
 * other and establish IPC connections. Each process periodically announces which topics it publishes and subscribes to,
 * along with associated metadata such as memory file names.
 *
 * This discovery mechanism enables automatic connection of publishers and
 * subscribers using shared memory without prior configuration.
 */
class Discovery {
 public:
  /**
   * @brief Struct that associates a discovery sample with a timestamp
   */
  struct TimestampedSample {
    trellis::core::time::TimePoint stamp;  ///< Time the sample was received or updated
    Sample sample;                         ///< The discovery sample
  };

  /**
   * @brief Structure to store statistics about a pub/sub connection
   */
  struct PubSubStats {
    unsigned send_receive_count;   ///< Number of messages sent or received
    double measured_frequency_hz;  ///< Observed message frequency in Hz
  };

  using SamplesMap = std::unordered_map<std::string, TimestampedSample>;
  using RegistrationHandle = int;
  using CallbackHandle = int;

  static constexpr RegistrationHandle kInvalidRegistrationHandle = -1;
  static constexpr CallbackHandle kInvalidCallbackHandle = -1;

  /**
   * @brief Enumeration for types of discovery events
   */
  enum EventType {
    kNewRegistration = 0,   ///< A new sample was registered
    kNewUnregistration = 1  ///< A sample was removed due to timeout or unregistration
  };

  /**
   * @brief Callback invoked on new or removed discovery samples
   *
   * @param event Type of event (registration or unregistration)
   * @param sample The discovery sample affected
   */
  using SampleCallback = std::function<void(EventType, const Sample&)>;
  using SampleCallbackMap = std::unordered_map<CallbackHandle, SampleCallback>;

  /**
   * @brief Construct a Discovery instance
   *
   * @param node_name Logical name of this node (typically an app name)
   * @param loop The Trellis event loop used for async operations
   * @param config The configuration tree to optionally pull values from
   */
  Discovery(std::string node_name, trellis::core::EventLoop loop, const trellis::core::Config& config);

  Discovery(const Discovery&) = delete;
  Discovery& operator=(const Discovery&) = delete;
  Discovery(Discovery&&) = delete;
  Discovery& operator=(Discovery&&) = delete;

  /**
   * @brief Register a sample for broadcasting
   *
   * @param sample The discovery sample to register
   * @return A handle that can be used to later unregister
   */
  RegistrationHandle Register(Sample sample);

  /**
   * @brief Unregister a previously registered sample
   *
   * @param handle The handle returned by Register()
   */
  void Unregister(RegistrationHandle handle);

  /**
   * @brief Subscribe to discovery updates about publishers
   *
   * @param callback The callback invoked on discovery events
   * @return A handle to later stop receiving updates
   */
  CallbackHandle AsyncReceivePublishers(SampleCallback callback);

  /**
   * @brief Subscribe to discovery updates about subscribers
   *
   * @param callback The callback invoked on discovery events
   * @return A handle to later stop receiving updates
   */
  CallbackHandle AsyncReceiveSubscribers(SampleCallback callback);

  /**
   * @brief Subscribe to discvoery update about services (RPC)
   */
  CallbackHandle AsyncReceiveServices(SampleCallback callback);

  /**
   * @brief Stop receiving discovery updates
   *
   * @param handle The callback handle returned by AsyncReceivePublishers/Subscribers
   */
  void StopReceive(CallbackHandle handle);

  /**
   * @brief Register a subscriber for a specific topic, pulling message descriptors from MSG_T
   *
   * @tparam MSG_T Protobuf message type
   * @param topic The name of the topic being subscribed to
   * @return Registration handle
   */
  template <class MSG_T, std::enable_if_t<!std::is_same<MSG_T, google::protobuf::Message>::value>* = nullptr>
  RegistrationHandle RegisterSubscriber(const std::string& topic) {
    MSG_T msg;
    const std::string tdesc = discovery::utils::GetProtoMessageDescription(msg);
    return Register(utils::CreateProtoPubSubSample(topic, tdesc, msg.GetDescriptor()->full_name(), false,
                                                   std::vector<std::string>{}));
  }

  /**
   * @brief Register a subscriber for a specific topic with message type unknown at compile time
   *
   * @tparam MSG_T Must be google::protobuf::Message
   * @param topic The name of the topic being subscribed to
   * @return Registration handle
   */
  template <class MSG_T, std::enable_if_t<std::is_same<MSG_T, google::protobuf::Message>::value>* = nullptr>
  RegistrationHandle RegisterSubscriber(const std::string& topic) {
    return Register(utils::CreateProtoPubSubSample(topic, "", "", false, std::vector<std::string>{}));
  }

  /**
   * @brief Register a publisher for a specific topic, pulling message descriptors from MSG_T
   *
   * @tparam MSG_T Protobuf message type
   * @param topic The topic being published
   * @param memory_file_list The list of shared memory file names backing the publisher
   * @return Registration handle
   */
  template <class MSG_T, std::enable_if_t<!std::is_same<MSG_T, google::protobuf::Message>::value>* = nullptr>
  RegistrationHandle RegisterPublisher(const std::string& topic, std::vector<std::string> memory_file_list) {
    MSG_T msg;
    const std::string tdesc = discovery::utils::GetProtoMessageDescription(msg);
    return Register(
        utils::CreateProtoPubSubSample(topic, tdesc, msg.GetDescriptor()->full_name(), true, memory_file_list));
  }

  RegistrationHandle RegisterDynamicPublisher(const std::string& topic, std::vector<std::string> memory_file_list,
                                              const std::string& tdesc, const std::string& full_name) {
    return Register(utils::CreateProtoPubSubSample(topic, tdesc, full_name, true, memory_file_list));
  }

  /**
   * @brief Register a service for a specific RPC service, pulling descriptors from SERVICE_T
   *
   * @param service_name the fully qualified name of the servce
   * @param port the TCP port that the service server is listening on
   * @param methods the mapping of available methods to the associated metadata
   * @return Registration handle
   */
  RegistrationHandle RegisterServiceServer(std::string service_name, uint16_t port,
                                           const ipc::proto::rpc::MethodsMap& methods) {
    return Register(utils::CreateServiceServerSample(port, service_name, methods));
  }

  /**
   * @brief Register a publisher for a specific topic with message type unknown at compile time
   *
   * @tparam MSG_T Must be google::protobuf::Message
   * @param topic The topic being published
   * @param memory_file_list The list of shared memory file names backing the topic
   * @return Registration handle
   */
  template <class MSG_T, std::enable_if_t<std::is_same<MSG_T, google::protobuf::Message>::value>* = nullptr>
  RegistrationHandle RegisterPublisher(const std::string& topic, std::vector<std::string> memory_file_list) {
    return Register(utils::CreateProtoPubSubSample(topic, "", "", true, memory_file_list));
  }

  /**
   * @brief Get the current set of known pub/sub discovery samples
   *
   * This method is helpful for introspection tools
   *
   * @return A vector of known discovery samples from remote peers
   */
  std::vector<Sample> GetPubSubSamples() const;

  /**
   * @brief Get the current set of known service server discovery samples
   *
   * This method is helpful for introspection tools
   *
   * @return A vector of known discovery samples from remote peers
   */
  std::vector<Sample> GetServiceSamples() const;

  /**
   * @brief Get the current set of known process metadata samples
   *
   * This method is helpful for introspection tools
   *
   * @return A vector of known discovery samples from remote peers
   */
  std::vector<Sample> GetProcessSamples() const;

  /**
   * @brief Update runtime statistics for a specific publisher/subscriber
   *
   * @param stats The new statistics to report
   * @param handle The registration handle corresponding to the topic
   */
  void UpdatePubSubStats(PubSubStats stats, RegistrationHandle handle);

  /**
   * @brief Get the unique identification string for the registration
   *
   * @param handle registration handle
   * @return unique identifier for the registration
   */
  std::string GetSampleId(RegistrationHandle handle);

 private:
  void ReceiveData(trellis::core::time::TimePoint now, const void* data, size_t len);
  void ProcessSubscriberSample(trellis::core::time::TimePoint now, EventType event, Sample sample);
  void ProcessPublisherSample(trellis::core::time::TimePoint now, EventType event, Sample sample);
  void ProcessProcessSample(trellis::core::time::TimePoint now, EventType event, Sample sample);
  void ProcessServiceSample(trellis::core::time::TimePoint now, EventType event, Sample sample);
  void Evaluate(const trellis::core::time::TimePoint& now);
  void BroadcastSamples();
  void BroadcastSample(const Sample& sample);
  void PurgeStaleSamples(const trellis::core::time::TimePoint& now, SamplesMap& map,
                         const SampleCallbackMap& callback_map);
  void PurgeStalePartialBuffers(const trellis::core::time::TimePoint& now);

  static constexpr size_t kUdpPayloadSizeMax = 65507;  // practical UDP limit of 2^16 minus protocol overhead
  using UdpReceiver = trellis::network::UDPReceiver<kUdpPayloadSizeMax>;
  using OptUdpReceiver = std::optional<UdpReceiver>;
  using OptUdpSender = std::optional<trellis::network::UDP>;

  const std::string node_name_;                        ///< This process's logical node name
  const std::string send_addr_;                        ///< The address to send discovery updates to
  const unsigned discovery_port_;                      ///< The port to send and receive discovery updates on
  const unsigned management_interval_;                 ///< The interval in milliseconds for the management timer
  const std::chrono::milliseconds sample_timeout_ms_;  ///< How long to wait before considering samples stale
  const bool loopback_enabled_;                        ///< Whether or not to loopback broadcasts bypassing UDP
  OptUdpReceiver udp_receiver_;                        ///< Receives discovery broadcasts
  OptUdpSender udp_sender_;                            ///< Sends discovery broadcasts
  trellis::core::Timer management_timer_;              ///< Periodic timer for housekeeping
  std::unordered_map<RegistrationHandle, Sample> registered_samples_{};  ///< Locally registered samples
  std::mutex registered_samples_mutex_{};               ///< syncrhonize access to registered samples map
  RegistrationHandle next_handle_{0};                   ///< Monotonically increasing handle generator
  CallbackHandle next_callback_handle_{0};              ///< Monotonically increasing callback ID
  std::array<uint8_t, kUdpPayloadSizeMax> send_buf_{};  ///< Reusable send buffer for UDP

  std::mutex callback_mutex_{};                      ///< Protects access to callback maps
  SampleCallbackMap process_sample_callbacks_{};     ///< Callbacks for process-level samples
  SampleCallbackMap publisher_sample_callbacks_{};   ///< Callbacks for publisher samples
  SampleCallbackMap subscriber_sample_callbacks_{};  ///< Callbacks for subscriber samples
  SampleCallbackMap service_sample_callbacks_{};     ///< Callbacks for service samples

  SamplesMap process_samples_{};     ///< Remote process samples
  SamplesMap publisher_samples_{};   ///< Remote publisher samples by topic
  SamplesMap subscriber_samples_{};  ///< Remote subscriber samples by topic
  SamplesMap service_samples_{};     ///< Remote service server samples

  // Multi-packet reassembly data structure
  struct PartialSample {
    uint32_t total_packets;                      ///< Expected total number of packets
    uint32_t next_packet_index;                  ///< Next expected packet index (0, 1, 2...)
    std::string payload_buffer;                  ///< Buffer to accumulate payload chunks
    trellis::core::time::TimePoint last_update;  ///< Time of most recent packet for cleanup
  };

  std::unordered_map<std::string, PartialSample> partial_samples_;  ///< Track partial samples by ID
};

/// @brief Shared pointer alias for Discovery
using DiscoveryPtr = std::shared_ptr<Discovery>;

}  // namespace trellis::core::discovery

#endif  // TRELLIS_CORE_DISCOVERY_DISCOVERY_HPP_
