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

#include "writer.hpp"

namespace trellis {
namespace utils {
namespace mcap {
Writer::Writer(core::Node& node, const std::vector<std::string>& topics, const std::string& outfile,
               ::mcap::McapWriterOptions options)
    : subscribers_{} {
  CreateSubscriberList(node, topics);
  const auto res = writer_.open(outfile, options);
  if (!res.ok()) {
    throw std::runtime_error("Failed to open " + outfile + " for writing: " + res.message);
  }
}

Writer::~Writer() { writer_.close(); }

void Writer::CreateSubscriberList(core::Node& node, const std::vector<std::string>& topics) {
  unsigned subscriber_index{0};
  for (const auto& topic : topics) {
    subscribers_.emplace_back(SubscriberInfo{
        false, topic,
        node.CreateRawSubscriber(
            topic, [this, subscriber_index](const core::time::TimePoint& now, const core::TimestampedMessage& msg) {
              std::lock_guard<std::mutex> guard(mutex_);
              if (subscribers_.at(subscriber_index).initialized == false) {
                InitializeMcapChannel(subscriber_index);
              }
              WriteMessage(subscriber_index, now, msg);
            })});
    ++subscriber_index;
  }
}

void Writer::InitializeMcapChannel(unsigned subscriber_index) {
  const auto& subscriber = subscribers_.at(subscriber_index).subscriber;
  const auto& topic = subscribers_.at(subscriber_index).topic;
  const auto& message_name = subscriber->GetDescriptor()->full_name();
  auto& channel_id = subscribers_.at(subscriber_index).channel_id;
  auto& initialized = subscribers_.at(subscriber_index).initialized;

  // Add both the schema and channel to the writer, and then record the channel ID for the future
  ::mcap::Schema schema(message_name, "protobuf", subscriber->GenerateFileDescriptorSet().SerializeAsString());
  writer_.addSchema(schema);
  ::mcap::Channel channel(topic, "protobuf", schema.id);
  writer_.addChannel(channel);
  channel_id = channel.id;
  initialized = true;

  core::Log::Info("Initialized MCAP recorder channel for {} on {} with id {}", message_name, topic, channel_id);
}

void Writer::WriteMessage(unsigned subscriber_index, const core::time::TimePoint& now,
                          const core::TimestampedMessage& msg) {
  auto& subscriber = subscribers_.at(subscriber_index);
  const auto& channel_id = subscriber.channel_id;
  auto& sequence = subscriber.sequence;
  ::mcap::Message mcap_msg;
  mcap_msg.channelId = channel_id;
  mcap_msg.sequence = sequence;
  mcap_msg.publishTime = core::time::TimePointToNanoseconds(core::time::TimePointFromTimestamp(msg.timestamp()));
  mcap_msg.logTime = core::time::TimePointToNanoseconds(now);
  mcap_msg.data = reinterpret_cast<const std::byte*>(msg.payload().data());
  mcap_msg.dataSize = msg.payload().size();

  const auto res = writer_.write(mcap_msg);
  if (!res.ok()) {
    writer_.close();
    throw std::runtime_error("MCAP write failed: " + res.message);
  }
  ++sequence;
}

}  // namespace mcap
}  // namespace utils
}  // namespace trellis
