/*
 * Copyright (C) 2023 Agtonomy
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

#ifndef TRELLIS_UTILS_MCAP_WRITER_SUBSCRIBER_DATA_HPP_
#define TRELLIS_UTILS_MCAP_WRITER_SUBSCRIBER_DATA_HPP_

#include <mcap/writer.hpp>
#include <memory>
#include <mutex>
#include <string>

#include "trellis/core/subscriber.hpp"
#include "trellis/utils/protobuf/file_descriptor.hpp"

namespace trellis::utils::mcap {

/// A mutex protection wrapper for the mcap writer
struct FileWriter {
  ::mcap::McapWriter writer = {};
  std::mutex mutex = {};

  /// A convenience function for creating FileWriters
  static std::shared_ptr<FileWriter> MakeFileWriter(const std::string_view outfile, ::mcap::McapWriterOptions options) {
    const auto ret = std::make_shared<FileWriter>();
    const auto res = ret->writer.open(outfile, options);
    if (!res.ok()) throw(std::runtime_error{fmt::format("Failed to open {} for writing: {}", outfile, res.message)});
    return ret;
  }
};

/// a class for completeness that provides a no operation. In the common case where conversion is not required, this
/// class is part of the template for SubscriberData and becomes a member but is never called.
struct NoopConverter {
  NoopConverter(const trellis::core::Config&, const std::string_view) {}
  auto operator()(const auto& v) { return std::optional<decltype(v)>{v}; }
};

template <typename MessageType, typename OutputMessageType = MessageType, typename Converter = NoopConverter>
class SubscriberData {
 public:
  /** This provides a suscriber that has a SubscriberData member bound to its callbacks. The lifetime of the callback
   * determines the lifetime of the SubscriberData. All member access for the SubscriberData happens within the
   * callbacks so nothing else needs to be exposed through the class interface.
   *
   * @param config the configuration
   * @param ev the event loop
   * @param discovery the discovery service
   * @param topic the subscription topic
   * @param file_writer the file writer that will be used. The file_writer ptr wil be copied into the SubscriberData
   * which will life as long as the callback within the Subscriber. This guarantees that the file_writer will exist for
   * at least as long as the Subscriber.
   * @return a shared_ptr to a subscriber for the message type on the topic. The received data will be written do file.
   */
  static trellis::core::Subscriber<MessageType> CreateSubscriber(const core::Config& config, core::EventLoop ev,
                                                                 core::discovery::DiscoveryPtr discovery,
                                                                 const std::string_view topic,
                                                                 std::shared_ptr<FileWriter> file_writer);

 private:
  /// hidden constructor so that the only way to use this class is via the CreateSubscriber factory
  SubscriberData(const trellis::core::Config& config, std::string_view topic, std::shared_ptr<FileWriter> file_writer)
      : initialized{false}, topic{topic}, file_writer{std::move(file_writer)}, sequence{0}, converter(config, topic) {}

  /// mutex should be locked before calling this function
  void TryInitializeMcapChannel();

  /// the writing function
  void WriteRawMessage(const core::time::TimePoint& stamp, const std::byte* data, size_t len);

  /// a convenience function for writing data that is provided by a raw subscriber callback
  void Write(const core::time::TimePoint& stamp, const uint8_t* data, size_t len);
  /// a convenience function for writing data that is provided by a subscriber callback
  void Write(const core::time::TimePoint& stamp, const MessageType& msg);

  bool initialized;   /// track initialization to know mcap schema and channel exists for this subscriber
  std::string topic;  /// topic name
  std::shared_ptr<FileWriter> file_writer;  /// the mutex protected file writer
  ::mcap::ChannelId channel_id;             /// Identifier to reference the channel for this subscriber
  unsigned sequence;                        /// sequence number for each message
  Converter converter;  /// converter that can be used if the input and output message types are different
  std::weak_ptr<core::SubscriberImpl<MessageType>>
      subscriber;  /// the subscriber object, weak ptr to avoid circular ref
};

template <typename MessageType, typename OutputMessageType, typename Converter>
trellis::core::Subscriber<MessageType> SubscriberData<MessageType, OutputMessageType, Converter>::CreateSubscriber(
    const core::Config& config, core::EventLoop ev, core::discovery::DiscoveryPtr discovery,
    const std::string_view topic, std::shared_ptr<FileWriter> file_writer) {
  // A bit of a chicken-and-egg problem, we need the callback to be able to access the subscriber to fill in the schema.
  // This introduces a small race condition that the subscriber may be nullptr when the first message arrives.
  // Hence, we use a shared ptr to update the data after creating the subscriber, and we guard in the
  // InitalizeMcapChannel function against data with nullptr subscriber.
  const auto subscriber_data = std::shared_ptr<SubscriberData>(new SubscriberData(config, topic, file_writer));

  typename core::SubscriberImpl<MessageType>::Callback callback;
  typename core::SubscriberImpl<MessageType>::RawCallback callback_raw;

  if constexpr (std::is_same_v<MessageType, OutputMessageType>) {
    // since the input and output are the same type, nothing needs to be known about the message type and a raw callback
    // can be used.
    callback_raw = [subscriber_data](const core::time::TimePoint&, const core::time::TimePoint& stamp,
                                     const uint8_t* data, size_t len) { subscriber_data->Write(stamp, data, len); };
  } else {
    // since the input and output types are not the same, the Write method is used that takes type information so that a
    // pre-write conversion can be done.
    callback = [subscriber_data](const core::time::TimePoint&, const core::time::TimePoint& stamp,
                                 core::SubscriberImpl<MessageType>::MsgTypePtr msg) {
      subscriber_data->Write(stamp, *msg);
    };
  }

  const trellis::core::Subscriber<MessageType> subscriber =
      std::make_shared<trellis::core::SubscriberImpl<MessageType>>(
          ev, std::string{topic}, callback, callback_raw, [](const core::time::TimePoint&) {}, discovery, config);

  subscriber_data->subscriber = subscriber;

  return subscriber;
}

template <typename MessageType, typename OutputMessageType, typename Converter>
void SubscriberData<MessageType, OutputMessageType, Converter>::Write(const core::time::TimePoint& stamp,
                                                                      const uint8_t* data, size_t len) {
  const auto lock = std::lock_guard{file_writer->mutex};
  if (!initialized) TryInitializeMcapChannel();
  if (initialized) WriteRawMessage(stamp, reinterpret_cast<const std::byte*>(data), len);
}

template <typename MessageType, typename OutputMessageType, typename Converter>
void SubscriberData<MessageType, OutputMessageType, Converter>::Write(const core::time::TimePoint& stamp,
                                                                      const MessageType& msg) {
  const std::lock_guard lock{file_writer->mutex};
  if (!initialized) TryInitializeMcapChannel();

  if (initialized) {
    // the lock is held for the duration of the conversion since the converter is not guaranteed to be thread safe
    std::optional<OutputMessageType> output_msg = converter(msg);
    if (output_msg) {
      const std::string serialized_msg{output_msg->SerializeAsString()};
      WriteRawMessage(stamp, reinterpret_cast<const std::byte*>(serialized_msg.data()), serialized_msg.length());
    }
  }
}

template <typename MessageType, typename OutputMessageType, typename Converter>
void SubscriberData<MessageType, OutputMessageType, Converter>::TryInitializeMcapChannel() {
  const google::protobuf::Descriptor* descriptor{nullptr};

  if constexpr (std::is_same_v<OutputMessageType, google::protobuf::Message>) {
    // This is not writing a compile time type of output message so it is necessary to wait
    // for the subscriber's cache to be populated to obtain a valid descriptor.

    // The subscriber may still be being constructed (unlikely), so we guard against it being nullptr.
    const std::shared_ptr<core::SubscriberImpl<MessageType>> sub = subscriber.lock();
    if (sub == nullptr) {
      core::Log::Error("Subscriber is nullptr, cannot initialize MCAP channel");
      return;
    }

    descriptor = sub->GetDescriptor();
  } else {
    // we can directly get the output descriptor since this is a known type
    descriptor = OutputMessageType::descriptor();
  }

  if (descriptor == nullptr) {
    core::Log::Error("Descriptor is nullptr, cannot initialize MCAP channel");
    return;
  }

  const auto& message_name = descriptor->full_name();

  // Add both the schema and channel to the writer, and then record the channel ID for the future
  // Not const to receive the schema id
  auto schema = ::mcap::Schema{
      message_name, "protobuf",
      trellis::utils::protobuf::GenerateFileDescriptorSetFromTopLevelDescriptor(descriptor).SerializeAsString()};
  file_writer->writer.addSchema(schema);
  // Not const to receive the channel id
  auto channel = ::mcap::Channel{topic, "protobuf", schema.id};
  file_writer->writer.addChannel(channel);
  channel_id = channel.id;
  initialized = true;
  core::Log::Info("Initialized MCAP recorder channel for {} on {} with id {}", message_name, topic, channel_id);
}

template <typename MessageType, typename OutputMessageType, typename Converter>
void SubscriberData<MessageType, OutputMessageType, Converter>::WriteRawMessage(const core::time::TimePoint& stamp,
                                                                                const std::byte* data, size_t len) {
  // MCAP files are indexed by log time, so we use send time as log time.
  const auto time = core::time::TimePointToNanoseconds(stamp);
  const auto mcap_msg = ::mcap::Message{.channelId = channel_id,
                                        .sequence = sequence,
                                        .logTime = time,
                                        .publishTime = time,
                                        .dataSize = len,
                                        .data = data};

  const auto res = file_writer->writer.write(mcap_msg);
  if (!res.ok()) {
    file_writer->writer.close();
    throw(std::runtime_error{fmt::format("MCAP write failed: {}", res.message)});
  }
  ++sequence;
}

}  // namespace trellis::utils::mcap

#endif  // TRELLIS_UTILS_MCAP_WRITER_SUBSCRIBER_DATA_HPP_
