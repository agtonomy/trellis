/*
 * Copyright (C) 2021 Agtonomy
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

#ifndef TRELLIS_CORE_PROTO_UTILS_HPP
#define TRELLIS_CORE_PROTO_UTILS_HPP

#include <string>

namespace trellis {
namespace core {
namespace proto_utils {

namespace {

const std::string raw_topic_prefix = "/trellis/raw:";

}

/**
 * IsRawTopic return true if the given topic name is a "raw" topic as described above
 */
inline bool IsRawTopic(const std::string& topic) {
  return topic.size() > raw_topic_prefix.size() && topic.substr(0, raw_topic_prefix.size()) == raw_topic_prefix;
}

/**
 * GetRawTopicString get the raw topic name for a given topic
 *
 * Each Trellis topic publishes a "TimestampedMessage" and has an associated "raw" topic that advertises the actual
 * message type. This function returns the associated raw topic name.
 */
inline std::string GetRawTopicString(const std::string& topic) {
  if (IsRawTopic(topic)) {
    return topic;
  }
  return raw_topic_prefix + topic;
}

/**
 * GetTopicFromRawTopic get the topic name given a raw topic
 *
 * This is the inverse of GetRawTopicString
 */
inline std::string GetTopicFromRawTopic(const std::string& raw_topic) {
  if (!IsRawTopic(raw_topic)) {
    return raw_topic;
  }
  return raw_topic.substr(raw_topic_prefix.size(), raw_topic.size() - raw_topic_prefix.size());
}

}  // namespace proto_utils
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_PROTO_UTILS_HPP
