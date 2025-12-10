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

#include "trellis/core/discovery/discovery.hpp"

#include <fmt/core.h>

#include <ranges>

#include "trellis/core/logging.hpp"

namespace trellis::core::discovery {

namespace {

/**
 * Creates and returns a socket file descriptor with SO_REUSEADDR and SO_REUSEPORT
 */
int CreateNativeUDPSocket(uint16_t port, int rcvbuf_size, int sndbuf_size) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    throw std::runtime_error("Failed to create UDP socket");
  }

  // Enable SO_REUSEADDR and SO_REUSEPORT so we can bind even if another process is already bound
  const int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    ::close(fd);
    throw std::runtime_error("Failed to set SO_REUSEADDR");
  }

  // Enable SO_REUSEPORT (optional, allows multiple sockets to bind to the same port)
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
    ::close(fd);
    throw std::runtime_error("Failed to set SO_REUSEPORT");
  }

  const int yes = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0) {
    ::close(fd);
    throw std::runtime_error("Failed to set SO_BROADCAST");
  }

  // Set SO_RCVBUF and SO_SNDBUF to handle bursts of incoming/outgoing data
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
    ::close(fd);
    throw std::runtime_error("Failed to set SO_RCVBUF");
  }
  if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size)) < 0) {
    ::close(fd);
    throw std::runtime_error("Failed to set SO_SNDBUF");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    throw std::runtime_error("Failed to bind UDP socket");
  }
  return fd;
}

struct SampleHeader {
  char head[4];  //-V112
  int32_t version;
  int32_t len;                 // header: complete size of message, data: current size of that part
  uint32_t packet_index;       // 0-based index of this packet in the sequence
  uint32_t total_packets;      // total number of packets for this sample
  uint32_t total_sample_size;  // total size of the complete sample payload
};

std::string GeneratePreamble(const std::string& id) {
  SampleHeader header;
  header.head[0] = 'T';
  header.head[1] = 'R';
  header.head[2] = 'L';
  header.head[3] = 'S';
  header.version = 1;

  const uint16_t sample_id_string_length = id.size() + 1;
  const size_t preamble_length = sizeof(SampleHeader) + sizeof(sample_id_string_length) + sample_id_string_length;

  std::string preamble_bytes(preamble_length, '\0');
  char* const data = preamble_bytes.data();

  // Here we write the fixed-size header, the 2-byte length field for the variable length ID, and then the ID itself
  ::memcpy(data, &header, sizeof(SampleHeader));
  ::memcpy(data + sizeof(SampleHeader), &sample_id_string_length, sizeof(sample_id_string_length));
  ::memcpy(data + sizeof(SampleHeader) + sizeof(sample_id_string_length), id.data(), sample_id_string_length);

  return preamble_bytes;
}

constexpr std::string_view kDefaultSendAddress = "127.255.255.255";
constexpr unsigned kDefaultDiscoveryPort = 1400u;
constexpr unsigned kDefaultDiscoveryInterval = 1000u;
constexpr unsigned kDefaultSampleTimeout = 2000u;
constexpr bool kDefaultLoopbackEnabled = false;
constexpr unsigned kDefaultRcvBufSize = 8 * 1024 * 1024;  // 8MB
constexpr unsigned kDefaultSndBufSize = 2 * 1024 * 1024;  // 2MB

}  // namespace

Discovery::ConfigData::ConfigData(const trellis::core::Config& config)
    : send_addr{config.AsIfExists<std::string>("trellis.discovery.send_address", std::string(kDefaultSendAddress))},
      discovery_port{config.AsIfExists<unsigned>("trellis.discovery.port", kDefaultDiscoveryPort)},
      management_interval{config.AsIfExists<unsigned>("trellis.discovery.interval", kDefaultDiscoveryInterval)},
      sample_timeout_ms{config.AsIfExists<unsigned>("trellis.discovery.sample_timeout", kDefaultSampleTimeout)},
      loopback_enabled{config.AsIfExists<bool>("trellis.discovery.loopback_enabled", kDefaultLoopbackEnabled)},
      rcvbuf_size{static_cast<int>(config.AsIfExists<unsigned>("trellis.discovery.rcvbuf_size", kDefaultRcvBufSize))},
      sndbuf_size{static_cast<int>(config.AsIfExists<unsigned>("trellis.discovery.sndbuf_size", kDefaultSndBufSize))} {}

Discovery::Discovery(std::string node_name, trellis::core::EventLoop loop, const trellis::core::Config& config)
    : node_name_{std::move(node_name)},
      config_{config},
      loop_{loop},
      udp_receiver_(config_.loopback_enabled
                        ? std::nullopt
                        : std::make_optional<UdpReceiver>(
                              loop,
                              static_cast<asio::ip::udp::socket::native_handle_type>(CreateNativeUDPSocket(
                                  config_.discovery_port, config_.rcvbuf_size, config_.sndbuf_size)),
                              [this](const void* data, size_t len, const asio::ip::udp::endpoint&) {
                                ReceiveData(trellis::core::time::Now(), data, len);
                              })),
      udp_sender_(config_.loopback_enabled
                      ? std::nullopt
                      : std::make_optional<trellis::network::UDP>(
                            loop, CreateNativeUDPSocket(0, config_.rcvbuf_size, config_.sndbuf_size))),
      management_timer_{std::make_shared<PeriodicTimerImpl>(
          loop, [this](const time::TimePoint& now) { Evaluate(now); }, config_.management_interval,
          config_.management_interval)} {
  Register(utils::GetNodeProcessSample(node_name_));
}

void Discovery::Evaluate(const trellis::core::time::TimePoint& now) {
  PurgeStaleSamples(now, process_samples_, process_sample_callbacks_);
  PurgeStaleSamples(now, publisher_samples_, publisher_sample_callbacks_);
  PurgeStaleSamples(now, subscriber_samples_, subscriber_sample_callbacks_);
  PurgeStaleSamples(now, service_samples_, service_sample_callbacks_);
  PurgeStalePartialBuffers(now);
  BroadcastSamples();
}

void Discovery::PurgeStaleSamples(const trellis::core::time::TimePoint& now, SamplesMap& map,
                                  const SampleCallbackMap& callback_map) {
  std::lock_guard guard(callback_mutex_);
  for (auto it = map.begin(); it != map.end();) {
    if (now - it->second.stamp > config_.sample_timeout_ms) {
      for (const auto& callback : callback_map | std::views::values) {
        if (callback) callback(EventType::kNewUnregistration, it->second.sample);
      }
      it = map.erase(it);
    } else {
      ++it;
    }
  }
}

void Discovery::PurgeStalePartialBuffers(const trellis::core::time::TimePoint& now) {
  for (auto it = partial_samples_.begin(); it != partial_samples_.end();) {
    if (now - it->second.last_update > config_.sample_timeout_ms) {
      it = partial_samples_.erase(it);
    } else {
      ++it;
    }
  }
}

void Discovery::ReceiveData(trellis::core::time::TimePoint now, const void* data, size_t len) {
  static constexpr size_t kHeaderSize = sizeof(SampleHeader);
  const char* buf = static_cast<const char*>(data);

  // Do we have enough data to cover the header?
  if (len < kHeaderSize) {
    return;
  }

  const SampleHeader* header = static_cast<const SampleHeader*>(data);
  // Do we have enough data to cover the sample name length field?
  if (len < kHeaderSize + sizeof(uint16_t)) {
    return;
  }

  // Do we have enough data to cover the sample name string?
  const uint16_t sample_id_string_length = (buf[kHeaderSize + 1] << 8) + buf[kHeaderSize];
  if (len < kHeaderSize + sizeof(uint16_t) + sample_id_string_length) {
    return;
  }

  const size_t payload_size = header->len - sizeof(sample_id_string_length) - sample_id_string_length;
  const unsigned sample_start_offset = kHeaderSize + sizeof(sample_id_string_length) + sample_id_string_length;

  // Assume our buffer contains the entire sample payload. This is true in the case of header->total_packets == 1.
  // If this is a multi packet sample, we'll buffer the data below and update the pointer and size
  const char* sample_buffer = buf + sample_start_offset;
  size_t sample_buffer_size = payload_size;
  const std::string sample_id(buf + kHeaderSize + sizeof(sample_id_string_length), sample_id_string_length);

  if (header->total_packets > 1) {
    // For multi-packet samples, we'll buffer the data until we received all the packets
    auto it = partial_samples_.find(sample_id);
    if (it == partial_samples_.end()) {
      if (header->packet_index != 0) {
        return;  // Drop non-zero packets if we haven't seen packet 0
      }
      // Initialize partial sample for packet 0
      auto [new_it, inserted] = partial_samples_.try_emplace(sample_id);
      it = new_it;  // Update iterator
      auto& partial_sample = it->second;
      partial_sample.total_packets = header->total_packets;
      partial_sample.next_packet_index = 0;
      partial_sample.payload_buffer.clear();
      partial_sample.payload_buffer.reserve(header->total_sample_size);
    }

    auto& partial_sample = it->second;

    // Verify this is the expected packet in sequence
    if (header->packet_index != partial_sample.next_packet_index) {
      // Out of order packet - reset and wait for sequence to restart
      partial_samples_.erase(sample_id);
      return;
    }
    partial_sample.payload_buffer.append(buf + sample_start_offset, payload_size);
    partial_sample.next_packet_index++;
    partial_sample.last_update = now;

    if (partial_sample.next_packet_index < partial_sample.total_packets) {
      return;  // We're done for now since we're waiting for more packets
    }

    sample_buffer = partial_sample.payload_buffer.data();
    sample_buffer_size = partial_sample.payload_buffer.size();
  }

  // All packets received - parse the complete sample
  Sample sample;
  const bool parse_success = sample.ParseFromArray(sample_buffer, sample_buffer_size);
  partial_samples_.erase(sample_id);

  if (!parse_success) {
    return;
  }

  // Process the complete sample
  switch (sample.type()) {
    case discovery::unknown:
      break;
    case discovery::process_registration:
      ProcessProcessSample(now, EventType::kNewRegistration, std::move(sample));
      break;
    case discovery::process_unregistration:
      ProcessProcessSample(now, EventType::kNewUnregistration, std::move(sample));
      break;
    case discovery::service_registration:
      ProcessServiceSample(now, EventType::kNewRegistration, std::move(sample));
      break;
    case discovery::service_unregistration:
      ProcessServiceSample(now, EventType::kNewUnregistration, std::move(sample));
      break;
    case discovery::client_registration:
      break;
    case discovery::client_unregistration:
      break;
    case discovery::subscriber_registration:
      ProcessSubscriberSample(now, EventType::kNewRegistration, std::move(sample));
      break;
    case discovery::subscriber_unregistration:
      ProcessSubscriberSample(now, EventType::kNewUnregistration, std::move(sample));
      break;
    case discovery::publisher_registration:
      ProcessPublisherSample(now, EventType::kNewRegistration, std::move(sample));
      break;
    case discovery::publisher_unregistration:
      ProcessPublisherSample(now, EventType::kNewUnregistration, std::move(sample));
      break;
    default:
      break;
  }
}

namespace {

void ProcessSamplesMap(Discovery::SamplesMap& map, trellis::core::time::TimePoint now, Discovery::EventType event,
                       Sample sample) {
  if (event == Discovery::EventType::kNewRegistration) {
    const auto topic_id = sample.id();  // copy map key before moving sample
    map[topic_id] = Discovery::TimestampedSample{.stamp = now, .sample = std::move(sample)};
  } else if (event == Discovery::EventType::kNewUnregistration) {
    auto it = map.find(sample.id());
    if (it != map.end()) {
      map.erase(it);
    }
  }
}

}  // namespace

void Discovery::ProcessProcessSample(trellis::core::time::TimePoint now, EventType event, Sample sample) {
  ProcessSamplesMap(process_samples_, now, event, sample);
}

void Discovery::ProcessSubscriberSample(trellis::core::time::TimePoint now, EventType event, Sample sample) {
  ProcessSamplesMap(subscriber_samples_, now, event, sample);
  std::lock_guard guard(callback_mutex_);
  for (const auto& callback : subscriber_sample_callbacks_ | std::views::values) {
    if (callback) callback(event, sample);
  }
}

void Discovery::ProcessPublisherSample(trellis::core::time::TimePoint now, EventType event, Sample sample) {
  ProcessSamplesMap(publisher_samples_, now, event, sample);
  std::lock_guard guard(callback_mutex_);
  for (const auto& callback : publisher_sample_callbacks_ | std::views::values) {
    if (callback) callback(event, sample);
  }
}

void Discovery::ProcessServiceSample(trellis::core::time::TimePoint now, EventType event, Sample sample) {
  ProcessSamplesMap(service_samples_, now, event, sample);
  std::lock_guard guard(callback_mutex_);
  for (const auto& callback : service_sample_callbacks_ | std::views::values) {
    if (callback) callback(event, sample);
  }
}

Discovery::RegistrationHandle Discovery::Register(Sample sample) {
  std::lock_guard lock(registered_samples_mutex_);
  registered_samples_.emplace(std::make_pair(next_handle_, sample));
  const auto handle = next_handle_++;
  return handle;
}

void Discovery::Unregister(RegistrationHandle handle) {
  if (handle == kInvalidRegistrationHandle) {
    return;
  }
  std::lock_guard lock(registered_samples_mutex_);
  const auto it = registered_samples_.find(handle);
  if (it != registered_samples_.end()) {
    registered_samples_.erase(it);
  }
}

void Discovery::BroadcastSamples() {
  std::lock_guard lock(registered_samples_mutex_);
  for (const auto& sample : registered_samples_ | std::views::values) {
    BroadcastSample(sample);
  }
}

void Discovery::BroadcastSample(const Sample& sample) {
  const uint16_t sample_id_string_length = sample.id().size() + 1;
  const size_t preamble_length = sizeof(SampleHeader) + sizeof(sample_id_string_length) + sample_id_string_length;

  // Capture the preamble and the payload in separate buffers because in the case of multiple packet transmissions, the
  // preamble is included in each packet.
  const std::string preamble_bytes = GeneratePreamble(sample.id());
  const std::string payload_bytes = sample.SerializeAsString();

  const size_t max_payload_per_packet = kUdpPayloadSizeMax - preamble_length;
  const size_t total_payload_size = payload_bytes.size();
  const uint32_t total_packets =
      static_cast<uint32_t>((total_payload_size + max_payload_per_packet - 1) / max_payload_per_packet);

  size_t payload_offset = 0;
  uint32_t packet_index = 0;
  while (payload_offset < total_payload_size) {
    const size_t chunk_size = std::min(max_payload_per_packet, total_payload_size - payload_offset);
    const size_t packet_size = preamble_length + chunk_size;

    // Copy both preamble and payload to the send buffer
    ::memcpy(send_buf_.data(), preamble_bytes.data(), preamble_length);
    ::memcpy(send_buf_.data() + preamble_length, payload_bytes.data() + payload_offset, chunk_size);

    // Update header with packet sequencing information
    SampleHeader* const header = reinterpret_cast<SampleHeader*>(send_buf_.data());
    header->len = sizeof(sample_id_string_length) + sample_id_string_length + chunk_size;
    header->packet_index = packet_index;
    header->total_packets = total_packets;
    header->total_sample_size = static_cast<uint32_t>(total_payload_size);

    if (!config_.loopback_enabled) {
      size_t bytes_sent = 0;
      auto error =
          udp_sender_->SendTo(config_.send_addr, config_.discovery_port, send_buf_.data(), packet_size, bytes_sent);
      if (error) {
        trellis::core::Log::Warn("Failed to send discovery packet: {}", error.message());
        break;
      }
    } else {
      // IMPORTANT: We must defer ReceiveData via asio::post to prevent deadlocks in loopback scenarios in cases where
      // the ReceiveData callback results in a call to register a new sample on the same callback. BroadcastSamples()
      // holds registered_samples_mutex_, and ReceiveData triggers callbacks that may call Register() which also needs
      // registered_samples_mutex_. By deferring to the next event loop iteration, we break this synchronous chain. We
      // copy the buffer data since send_buf_ is reused across packets.

      // Loopback mode is only for testing or replay, so the extra copy is acceptable.
      auto buffer_copy = std::make_shared<std::vector<uint8_t>>(send_buf_.begin(), send_buf_.begin() + packet_size);
      asio::post(*loop_, [this, buffer_copy]() {
        ReceiveData(trellis::core::time::Now(), buffer_copy->data(), buffer_copy->size());
      });
    }

    payload_offset += chunk_size;
    ++packet_index;
  }
}

Discovery::CallbackHandle Discovery::AsyncReceivePublishers(SampleCallback callback) {
  std::lock_guard guard(callback_mutex_);
  const unsigned handle = next_callback_handle_++;
  publisher_sample_callbacks_[handle] = std::move(callback);
  return handle;
}

Discovery::CallbackHandle Discovery::AsyncReceiveSubscribers(SampleCallback callback) {
  std::lock_guard guard(callback_mutex_);
  const unsigned handle = next_callback_handle_++;
  subscriber_sample_callbacks_[handle] = std::move(callback);
  return handle;
}

Discovery::CallbackHandle Discovery::AsyncReceiveServices(SampleCallback callback) {
  std::lock_guard guard(callback_mutex_);
  const unsigned handle = next_callback_handle_++;
  service_sample_callbacks_[handle] = std::move(callback);
  return handle;
}

void Discovery::StopReceive(Discovery::CallbackHandle handle) {
  if (handle == kInvalidCallbackHandle) {
    return;
  }
  std::lock_guard guard(callback_mutex_);
  if (publisher_sample_callbacks_.contains(handle)) {
    publisher_sample_callbacks_.erase(handle);
  }
  if (subscriber_sample_callbacks_.contains(handle)) {
    subscriber_sample_callbacks_.erase(handle);
  }
  if (service_sample_callbacks_.contains(handle)) {
    service_sample_callbacks_.erase(handle);
  }
}

std::vector<Sample> Discovery::GetPubSamples() const {
  std::vector<Sample> samples;
  for (const auto& [timestamp, sample] : publisher_samples_ | std::views::values) {
    samples.push_back(sample);
  }
  return samples;
}

std::vector<Sample> Discovery::GetSubSamples() const {
  std::vector<Sample> samples;
  for (const auto& [timestamp, sample] : subscriber_samples_ | std::views::values) {
    samples.push_back(sample);
  }
  return samples;
}

std::vector<Sample> Discovery::GetPubSubSamples() const {
  std::vector<Sample> samples;
  for (const auto& [timestamp, sample] : publisher_samples_ | std::views::values) {
    samples.push_back(sample);
  }
  for (const auto& [timestamp, sample] : subscriber_samples_ | std::views::values) {
    samples.push_back(sample);
  }
  return samples;
}

std::vector<Sample> Discovery::GetServiceSamples() const {
  std::vector<Sample> samples;
  for (const auto& [timestamp, sample] : service_samples_ | std::views::values) {
    samples.push_back(sample);
  }
  return samples;
}

std::vector<Sample> Discovery::GetProcessSamples() const {
  std::vector<Sample> samples;
  for (const auto& [timestamp, sample] : process_samples_ | std::views::values) {
    samples.push_back(sample);
  }
  return samples;
}

void Discovery::UpdatePubSubStats(PubSubStats stats, RegistrationHandle handle) {
  std::lock_guard lock(registered_samples_mutex_);
  auto it = registered_samples_.find(handle);
  if (it == registered_samples_.end()) {
    Log::Error("Attempt to retrieve registered sample that doesn't exist. Handle = {}", static_cast<unsigned>(handle));
    return;
  }
  auto& sample = it->second;
  sample.mutable_topic()->set_data_count(stats.send_receive_count);
  sample.mutable_topic()->set_data_frequency(static_cast<int32_t>(stats.measured_frequency_hz * 1000));
  sample.mutable_topic()->set_max_burst_count(stats.max_burst_size);
  sample.mutable_topic()->set_message_drops(static_cast<int32_t>(stats.message_drops));  // narrowing conversion
}

std::string Discovery::GetSampleId(RegistrationHandle handle) {
  std::lock_guard lock(registered_samples_mutex_);
  auto it = registered_samples_.find(handle);
  if (it == registered_samples_.end()) {
    throw std::logic_error(fmt::format("Attempt to retrieve registered sample that doesn't exist. Handle = {}",
                                       static_cast<unsigned>(handle)));
  }
  auto& sample = it->second;
  return sample.id();
}

const Discovery::ConfigData& Discovery::GetConfig() const { return config_; }

}  // namespace trellis::core::discovery
