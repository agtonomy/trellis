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

namespace trellis::utils::mcap {

/**
 * @brief Log writer utility for subscribing to trellis topics and writing messages to an MCAP log file
 *
 * This class does not need to be aware of the message types at compile time. Instead, it will forward the message
 * payload directly to disk and inform MCAP of the message schema at runtime.
 *
 */
class Writer {
 public:
  /**
   * @brief Construct a new writer
   *
   * @param node trellis node by which to create subscribers from
   * @param topics list of topics to subscribe to
   * @param outfile the path of the output mcap file
   * @param options mcap writer options (optional) the default has some compression
   * @param flush_interval_ms interval in milliseconds to periodically flush data to disk (0 means no periodic flush)
   */
  Writer(core::Node& node, const std::vector<std::string>& topics, std::string_view outfile,
         const ::mcap::McapWriterOptions& options = ::mcap::McapWriterOptions("protobuf"),
         std::chrono::milliseconds flush_interval_ms = std::chrono::milliseconds{0});
  ~Writer();

 private:
  std::vector<core::SubscriberRaw> subscribers_;
  core::Timer flush_timer_;
  core::Node& node_;
};

}  // namespace trellis::utils::mcap

#endif  // TRELLIS_UTILS_MCAP_WRITER_HPP_
