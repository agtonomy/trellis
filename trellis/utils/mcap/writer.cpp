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

#include "trellis/utils/mcap/writer.hpp"

#include "trellis/core/timer.hpp"
#include "trellis/utils/protobuf/file_descriptor.hpp"

namespace trellis::utils::mcap {

namespace {

struct FileWriter {
  ::mcap::McapWriter writer = {};
  std::mutex mutex = {};
};

std::shared_ptr<FileWriter> MakeFileWriter(const std::string_view outfile, ::mcap::McapWriterOptions options) {
  const auto ret = std::make_shared<FileWriter>();
  const auto res = ret->writer.open(outfile, options);
  if (!res.ok()) throw(std::runtime_error{fmt::format("Failed to open {} for writing: {}", outfile, res.message)});
  return ret;
}

// Data for each subscriber
struct SubscriberData {
  bool initialized = {};  /// track initialization to know mcap schema and channel exists for this subscriber
  std::string topic{};    /// topic name
  std::shared_ptr<FileWriter> file_writer = {};            /// the mutex protected file writer
  std::weak_ptr<core::SubscriberRawImpl> subscriber = {};  /// the subscriber object, weak ptr to avoid circular ref
  ::mcap::ChannelId channel_id = {};                       /// Identifier to reference the channel for this subscriber
  unsigned sequence = {};                                  /// sequence number for each message
};

// mutex should be locked before calling this function
void TryInitializeMcapChannel(SubscriberData& data) {
  // The subscriber may still be being constructed (unlikely), so we guard against it being nullptr.
  const auto subscriber = data.subscriber.lock();
  if (subscriber == nullptr) {
    core::Log::Error("Subscriber is nullptr, cannot initialize MCAP channel");
    return;
  }

  const auto descriptor = subscriber->GetDescriptor();
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
  data.file_writer->writer.addSchema(schema);
  // Not const to receive the channel id
  auto channel = ::mcap::Channel{data.topic, "protobuf", schema.id};
  data.file_writer->writer.addChannel(channel);
  data.channel_id = channel.id;
  data.initialized = true;
  core::Log::Info("Initialized MCAP recorder channel for {} on {} with id {}", message_name, data.topic,
                  data.channel_id);
}

void WriteMessage(const core::time::TimePoint& stamp, const uint8_t* data, size_t len,
                  SubscriberData& subscriber_data) {
  // MCAP files are indexed by log time, so we use send time as log time.
  const auto time = core::time::TimePointToNanoseconds(stamp);
  const auto mcap_msg = ::mcap::Message{.channelId = subscriber_data.channel_id,
                                        .sequence = subscriber_data.sequence,
                                        .logTime = time,
                                        .publishTime = time,
                                        .dataSize = len,
                                        .data = reinterpret_cast<const std::byte*>(data)};

  const auto res = subscriber_data.file_writer->writer.write(mcap_msg);
  if (!res.ok()) {
    subscriber_data.file_writer->writer.close();
    throw(std::runtime_error{fmt::format("MCAP write failed: {}", res.message)});
  }
  ++subscriber_data.sequence;
}

void FlushWriter(FileWriter& file_writer) {
  const auto lock = std::lock_guard{file_writer.mutex};
  file_writer.writer.closeLastChunk();
}

core::SubscriberRaw CreateSubscriber(const core::Config& config, core::EventLoop ev,
                                     core::discovery::DiscoveryPtr discovery, const std::string_view topic,
                                     std::shared_ptr<FileWriter> file_writer) {
  // A bit of a chicken-and-egg problem, we need the callback to be able to access the subscriber to fill in the schema.
  // This introduces a small race condition that the subscriber may be nullptr when the first message arrives.
  // Hence, we use a shared ptr to update the data after creating the subscriber, and we guard in the
  // InitalizeMcapChannel function against data with nullptr subscriber.
  const auto subscriber_data = std::make_shared<SubscriberData>(
      SubscriberData{.topic = std::string{topic}, .file_writer = std::move(file_writer)});
  const auto subscriber = std::make_shared<trellis::core::SubscriberImpl<google::protobuf::Message>>(
      ev, std::string{topic}, core::SubscriberRawImpl::Callback{},
      [subscriber_data](const core::time::TimePoint&, const core::time::TimePoint& stamp, const uint8_t* data,
                        size_t len) {
        const auto lock = std::lock_guard{subscriber_data->file_writer->mutex};
        if (!subscriber_data->initialized) TryInitializeMcapChannel(*subscriber_data);
        if (subscriber_data->initialized) WriteMessage(stamp, data, len, *subscriber_data);
      },
      [](const core::time::TimePoint&) {}, discovery, config);

  subscriber_data->subscriber = subscriber;

  return subscriber;
}

}  // namespace

void Writer::Initialize(core::Node& node, const std::vector<std::string>& topics, const std::string_view outfile,
                        const ::mcap::McapWriterOptions& options, std::chrono::milliseconds flush_interval_ms) {
  const auto file_writer = MakeFileWriter(outfile, options);
  for (const auto& topic : topics)
    subscribers_.emplace_back(CreateSubscriber(node.GetConfig(), loop_, discovery_, topic, file_writer));

  // Set up periodic flush timer if interval is greater than 0
  if (flush_interval_ms.count() > 0) {
    // Manually create timer so we use the provided event loop instead of the node's event loop
    flush_timer_ = std::make_shared<core::PeriodicTimerImpl>(
        loop_, [file_writer](const core::time::TimePoint&) { FlushWriter(*file_writer); },
        static_cast<unsigned>(flush_interval_ms.count()), 0);
  }
}

Writer::Writer(core::Node& node, const std::vector<std::string>& topics, const std::string_view outfile,
               const ::mcap::McapWriterOptions& options, std::chrono::milliseconds flush_interval_ms)
    : loop_{node.GetEventLoop()}, discovery_{node.GetDiscovery()} {
  Initialize(node, topics, outfile, options, flush_interval_ms);
}

Writer::Writer(core::Node& node, core::EventLoop ev, core::discovery::DiscoveryPtr discovery,
               const std::vector<std::string>& topics, const std::string_view outfile,
               const ::mcap::McapWriterOptions& options, std::chrono::milliseconds flush_interval_ms)
    : loop_{ev}, discovery_{discovery} {
  Initialize(node, topics, outfile, options, flush_interval_ms);
}

Writer::~Writer() {
  if (flush_timer_) {
    flush_timer_->Stop();
    flush_timer_->Fire();
  }

  // Clear subscribers explicitly
  subscribers_.clear();
}

}  // namespace trellis::utils::mcap
