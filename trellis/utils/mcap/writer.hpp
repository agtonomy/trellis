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

#ifndef TRELLIS_UTILS_MCAP_WRITER_HPP_
#define TRELLIS_UTILS_MCAP_WRITER_HPP_

#include <mutex>
#include <string>
#include <vector>

#include "mcap/writer.hpp"
#include "trellis/core/node.hpp"
#include "trellis/core/subscriber.hpp"
#include "trellis/core/timer.hpp"
#include "trellis/utils/mcap/writer_subscriber_data.hpp"

namespace trellis::utils::mcap {

class WriterBase {
 public:
  virtual ~WriterBase() = default;
};

/**
 * @brief Log writer utility for subscribing to trellis topics and writing messages to an MCAP log file
 *
 * This class does not need to be aware of the message types at compile time. Instead, it will forward the message
 * payload directly to disk and inform MCAP of the message schema at runtime.
 *
 */
template <typename MessageType, typename OutputMessageType = MessageType, typename Converter = NoopConverter>
class WriterImpl : public WriterBase {
 public:
  /**
   * @brief Construct a new writer using the given node's event loop thread and discovery interface
   *
   * @param node trellis node by which to create subscribers from
   * @param topics list of topics to subscribe to
   * @param outfile the path of the output mcap file
   * @param options mcap writer options (optional) the default has some compression
   * @param flush_interval_ms interval in milliseconds to periodically flush data to disk (0 means no periodic flush)
   */
  WriterImpl(core::Node& node, const std::vector<std::string>& topics, std::string_view outfile,
             const ::mcap::McapWriterOptions& options = ::mcap::McapWriterOptions("protobuf"),
             std::chrono::milliseconds flush_interval_ms = std::chrono::milliseconds{0});

  /**
   * @brief Construct a new writer using a self-managed thread running on the given event loop
   *
   * @param node trellis node by which to create subscribers from
   * @param ev the self-managed event loop (must be separate from node.GetEventLoop())
   * @param discovery the discovery interface to use to connect the underlying subscribers (must be separate from
   * node.GetDiscovery())
   * @param topics list of topics to subscribe to
   * @param outfile the path of the output mcap file
   * @param options mcap writer options (optional) the default has some compression
   * @param flush_interval_ms interval in milliseconds to periodically flush data to disk (0 means no periodic flush)
   */
  WriterImpl(core::Node& node, core::EventLoop ev, core::discovery::DiscoveryPtr discovery,
             const std::vector<std::string>& topics, std::string_view outfile,
             const ::mcap::McapWriterOptions& options = ::mcap::McapWriterOptions("protobuf"),
             std::chrono::milliseconds flush_interval_ms = std::chrono::milliseconds{0});

  ~WriterImpl() override;

  // Forbid copy
  WriterImpl(const WriterImpl&) = delete;
  WriterImpl& operator=(const WriterImpl&) = delete;

  WriterImpl(WriterImpl&&) = default;
  WriterImpl& operator=(WriterImpl&&) = default;

 private:
  void Initialize(core::Node& node, const std::vector<std::string>& topics, const std::string_view outfile,
                  const ::mcap::McapWriterOptions& options, std::chrono::milliseconds flush_interval_ms);
  trellis::core::EventLoop loop_;
  core::discovery::DiscoveryPtr discovery_;
  std::vector<trellis::core::Subscriber<MessageType>> subscribers_;
  core::Timer flush_timer_;
};

template <typename MessageType, typename OutputMessageType, typename Converter>
WriterImpl<MessageType, OutputMessageType, Converter>::WriterImpl(core::Node& node,
                                                                  const std::vector<std::string>& topics,
                                                                  const std::string_view outfile,
                                                                  const ::mcap::McapWriterOptions& options,
                                                                  std::chrono::milliseconds flush_interval_ms)
    : loop_{node.GetEventLoop()}, discovery_{node.GetDiscovery()} {
  Initialize(node, topics, outfile, options, flush_interval_ms);
}

template <typename MessageType, typename OutputMessageType, typename Converter>
WriterImpl<MessageType, OutputMessageType, Converter>::WriterImpl(core::Node& node, core::EventLoop ev,
                                                                  core::discovery::DiscoveryPtr discovery,
                                                                  const std::vector<std::string>& topics,
                                                                  const std::string_view outfile,
                                                                  const ::mcap::McapWriterOptions& options,
                                                                  std::chrono::milliseconds flush_interval_ms)
    : loop_{ev}, discovery_{discovery} {
  Initialize(node, topics, outfile, options, flush_interval_ms);
}

template <typename MessageType, typename OutputMessageType, typename Converter>
WriterImpl<MessageType, OutputMessageType, Converter>::~WriterImpl() {
  if (flush_timer_) {
    flush_timer_->Stop();
    flush_timer_->Fire();
  }

  // Clear subscribers explicitly
  subscribers_.clear();
}

template <typename MessageType, typename OutputMessageType, typename Converter>
void WriterImpl<MessageType, OutputMessageType, Converter>::Initialize(core::Node& node,
                                                                       const std::vector<std::string>& topics,
                                                                       const std::string_view outfile,
                                                                       const ::mcap::McapWriterOptions& options,
                                                                       std::chrono::milliseconds flush_interval_ms) {
  const auto file_writer = FileWriter::MakeFileWriter(outfile, options);
  for (const auto& topic : topics)
    subscribers_.emplace_back(SubscriberData<MessageType, OutputMessageType, Converter>::CreateSubscriber(
        node.GetConfig(), loop_, discovery_, topic, file_writer));

  // Set up periodic flush timer if interval is greater than 0
  if (flush_interval_ms.count() > 0) {
    // Manually create timer so we use the provided event loop instead of the node's event loop
    flush_timer_ = std::make_shared<core::PeriodicTimerImpl>(
        loop_,
        [file_writer](const core::time::TimePoint&) {  // flush the writer
          const auto lock = std::lock_guard{file_writer->mutex};
          file_writer->writer.closeLastChunk();
        },
        static_cast<unsigned>(flush_interval_ms.count()), 0);
  }
}

typedef WriterImpl<google::protobuf::Message> Writer;

}  // namespace trellis::utils::mcap

#endif  // TRELLIS_UTILS_MCAP_WRITER_HPP_
