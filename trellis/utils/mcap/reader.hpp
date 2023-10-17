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

#ifndef TRELLIS_UTILS_MCAP_READER_HPP_
#define TRELLIS_UTILS_MCAP_READER_HPP_

#include <string>
#include <string_view>
#include <unordered_set>

#include "mcap/reader.hpp"
#include "trellis/core/time.hpp"

namespace trellis {
namespace utils {
namespace mcap {

class Reader {
 public:
  /**
   * @brief A class representing a "view" into a particular mcap channel (topic)
   *
   * This class acts as a wrapper for an ::mcap::LinearMessageView for a given protobuf message type, so that the
   * deserialization is taken care of.
   *
   * @tparam MSG_T the protobuf message type of interest
   */
  template <typename MSG_T>
  class LinearMessageView {
   public:
    /**
     * @brief Data structure representing a linear forward iterator into the message view
     *
     * This class acts as a wrapper for an ::mcap::LinearMessageView::Iterator
     */
    struct LinearIterator {
     public:
      /**
       * @brief A timestamp and message pair
       */
      struct StampedMessage {
        core::time::TimePoint timestamp;
        MSG_T msg;
      };
      using iterator_category = std::input_iterator_tag;
      using difference_type = int64_t;
      using value_type = StampedMessage;
      using pointer = const StampedMessage*;
      using reference = const StampedMessage&;

      /**
       * @brief Dereference operator, which will deserialize the underlying message
       *
       * @return reference to the underlying value_type
       */
      reference operator*() {
        if (!cur_msg_.has_value()) {
          cur_msg_ = {core::time::NanosecondsToTimePoint(it_->message.publishTime), Reader::As<MSG_T>(it_)};
        }
        return *cur_msg_;
      }

      /**
       * @brief Access operator
       *
       * @return pointer to the underlying value_type
       */
      pointer operator->() const { return &(this->operator*()); }

      // Delete the post-increment operator, we'll support pre-increment only.
      LinearIterator& operator++(int x) = delete;

      /**
       * @brief Pre-increment operator
       *
       * Advances the iterator forward by one, and then returns a self-reference
       *
       * @return LinearIterator& reference to self after increment
       */
      LinearIterator& operator++() {
        cur_msg_ = {};
        it_.operator++();
        return *this;
      }

      /**
       * @brief Iterator equality comparison operator
       */
      friend bool operator==(const LinearIterator& a, const LinearIterator& b) { return a.it_ == b.it_; }

      /**
       * @brief Iterator not-equal comparison operator
       */
      friend bool operator!=(const LinearIterator& a, const LinearIterator& b) { return a.it_ != b.it_; }

     private:
      // restrict construction to `LinearMessageView` only
      friend LinearMessageView<MSG_T>;
      LinearIterator(::mcap::LinearMessageView::Iterator it) : it_{std::move(it)} {}

      ::mcap::LinearMessageView::Iterator it_;
      std::optional<StampedMessage> cur_msg_;
    };

    LinearMessageView(::mcap::McapReader& reader, const ::mcap::ReadMessageOptions& options)
        : view_{reader.readMessages(
              [](const ::mcap::Status& status) {
                throw std::runtime_error("Error reading message from mcap file: " + status.message);
              },
              options)} {}

    LinearIterator begin() { return LinearIterator{view_.begin()}; }
    LinearIterator end() { return LinearIterator{view_.end()}; }

   private:
    ::mcap::LinearMessageView view_;
  };

  using TopicSet = std::unordered_set<std::string>;

  /**
   * @brief Construct a new mcap reader
   *
   * @param file A file path to the mcap file to read
   * @throws runtime error if we failed to open the file or read the summary section
   */
  Reader(std::string_view file) : reader_{}, topics_{} {
    auto res = reader_.open(file);
    if (!res.ok()) {
      throw std::runtime_error("Failed to open " + std::string(file) + ": " + res.message);
    }

    (void)reader_.readSummary(::mcap::ReadSummaryMethod::AllowFallbackScan, [](const ::mcap::Status& status) {
      throw std::runtime_error("Error reading summary from mcap file: " + status.message);
    });

    topics_ = GenerateTopicSet(reader_);
  }

  /**
   * @brief Get the set of topics contained in the mcap file
   *
   * @return const TopicSet& the topics
   */
  const TopicSet& GetTopics() const { return topics_; }

  /**
   * @brief Check if a particular topic exists in the mcap file
   *
   * @param topic the topic to check
   * @return true if it exists
   */
  bool HasTopic(std::string_view topic) const { return topics_.contains(std::string(topic)); }

  ::mcap::LinearMessageView ReadMessages(const ::mcap::ReadMessageOptions& options) {
    return reader_.readMessages(
        [](const ::mcap::Status& status) {
          throw std::runtime_error("Error reading message from mcap file: " + status.message);
        },
        options);
  }

  /**
   * @brief Return a linear message view based on a set of topics to read
   *
   * @param topics The set of topics to read
   * @param start The optional start time to read from (defaults to beginning of log)
   * @param end The optional end time to read until (defaults to end of log)
   * @param order The optional read order (defaults to file order). Other options are LogTimeOrder and
   * ReverseLogTimeOrder
   * @return ::mcap::LinearMessageView a message view, which generates iterators to the messages
   */
  ::mcap::LinearMessageView ReadMessagesFromTopicSet(
      TopicSet topics, ::mcap::Timestamp start = 0, ::mcap::Timestamp end = ::mcap::MaxTime,
      ::mcap::ReadMessageOptions::ReadOrder order = ::mcap::ReadMessageOptions::ReadOrder::FileOrder) {
    ::mcap::ReadMessageOptions options;
    options.startTime = start;
    options.endTime = end;
    options.topicFilter = [topics = std::move(topics)](std::string_view topic) -> bool {
      return topics.contains(std::string(topic));
    };
    options.readOrder = order;
    return ReadMessages(options);
  }

  /**
   * @brief Return a linear message view based on a single topic and message type
   *
   * @tparam MSG_T the protobuf message type of the topic
   * @param topic the topic to read
   * @param start The optional start time to read from (defaults to beginning of log)
   * @param end The optional end time to read until (defaults to end of log)
   * @param order The optional read order (defaults to file order). Other options are LogTimeOrder and
   * ReverseLogTimeOrder
   * @return LinearMessageView<MSG_T> a message view for the given topic and message type
   */
  template <typename MSG_T>
  LinearMessageView<MSG_T> ReadMessagesFromTopic(
      std::string_view topic, ::mcap::Timestamp start = 0, ::mcap::Timestamp end = ::mcap::MaxTime,
      ::mcap::ReadMessageOptions::ReadOrder order = ::mcap::ReadMessageOptions::ReadOrder::FileOrder) {
    if (!HasTopic(topic)) {
      throw std::runtime_error("Attempt to read topic " + std::string(topic) + ", which doesn't exist");
    }
    const std::string topic_str(topic);
    ::mcap::ReadMessageOptions options;
    options.startTime = start;
    options.endTime = end;
    options.topicFilter = [topic_str = std::move(topic_str)](std::string_view topic) -> bool {
      return topic_str == std::string(topic);
    };
    options.readOrder = order;
    return LinearMessageView<MSG_T>(reader_, options);
  }

  /**
   * @brief Return the mcap file statistics (if it exists)
   *
   * @return const std::optional<::mcap::Statistics>& optional statistics
   */
  const std::optional<::mcap::Statistics>& Statistics() const { return reader_.statistics(); }

  /**
   * @brief Helper to return a new MSG_T deserialized from the iterator contents
   *
   * @tparam MSG_T the message type to return
   * @param it the iterator to deserialize from
   * @return MSG_T the message
   */
  template <typename MSG_T>
  static MSG_T As(const ::mcap::LinearMessageView::Iterator& it) {
    MSG_T msg;
    msg.ParseFromArray(static_cast<const void*>(it->message.data), it->message.dataSize);
    return msg;
  }

 private:
  /**
   * @brief Helper to generate the topic set (for caching)
   *
   * @param reader the mcap reader object to pull the topics from
   * @return TopicSet the set of topics
   */
  static TopicSet GenerateTopicSet(const ::mcap::McapReader& reader) {
    TopicSet topics;
    const auto channels = reader.channels();
    for (const auto& channel : channels) {
      topics.emplace(channel.second->topic);
    }
    return topics;
  }
  ::mcap::McapReader reader_;
  TopicSet topics_;
};

}  // namespace mcap
}  // namespace utils
}  // namespace trellis

#endif  // TRELLIS_UTILS_MCAP_READER_HPP_
