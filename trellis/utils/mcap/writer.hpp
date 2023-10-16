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

namespace trellis {
namespace utils {
namespace mcap {

/**
 * @brief Log writer utility for subscribing to trellis topics and writing messages to an MCAP log file
 *
 * This class does not need to be aware of the message types at compile time. Instead, it will forward the message
 * payload directly to disk and inform MCAP of the message schema at runtime.
 */
class Writer {
 public:
  /**
   * @brief Construct a new writer
   *
   * @param node trellis node by which to create subscribers from
   * @param topics list of topics to subscribe to
   * @param outfile the path of the output mcap file
   * @param options mcap writer options (optional)
   */
  Writer(core::Node& node, const std::vector<std::string>& topics, const std::string& outfile,
         ::mcap::McapWriterOptions options = ::mcap::McapWriterOptions(""));

  /**
   * @brief Finalizes the log writer
   */
  ~Writer();

  // Move/copy not allowed
  Writer(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer& operator=(Writer&&) = delete;

 private:
  /**
   * @brief Create subscribers based on our topic list
   */
  void CreateSubscriberList(core::Node& node, const std::vector<std::string>& topics);
  /**
   * @brief Initialize an MCAP channel by specifying metadata such as the message schema and topic
   *
   * @param subscriber_index Relevant index into the subscriber list
   */
  void InitializeMcapChannel(unsigned subscriber_index);
  /**
   * @brief Write a message to the log
   *
   * @param subscriber_index Relevant index into the subscriber list
   * @param now Subscriber receive time (e.g. now)
   * @param msg The message to write to the log
   */
  void WriteMessage(unsigned subscriber_index, const core::time::TimePoint& now, const core::TimestampedMessage& msg);

  /**
   * @brief Per-subscriber metadata
   */
  struct SubscriberInfo {
    bool initialized{false};  /// track initialization to know mcap schema and channel exists for this subscriber
    std::string topic{};      /// topic name
    core::SubscriberRaw subscriber{nullptr};  /// trellis subscriber handle
    ::mcap::ChannelId channel_id{};           /// Identifier to reference the channel for this subscriber
    unsigned sequence{0};                     /// sequence number for each message
  };
  using SubscriberList = std::vector<SubscriberInfo>;
  SubscriberList subscribers_{};
  std::mutex mutex_;
  ::mcap::McapWriter writer_{};
};

}  // namespace mcap
}  // namespace utils
}  // namespace trellis

#endif  //  TRELLIS_UTILS_MCAP_WRITER_HPP_
