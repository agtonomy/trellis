
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

#ifndef TRELLIS_CORE_STAMPED_MESSAGE_HPP_
#define TRELLIS_CORE_STAMPED_MESSAGE_HPP_

#include "trellis/core/subscriber.hpp"

namespace trellis::core {

/**
 * @brief Structure to hold a timestamp and a message pointer
 *
 * Lightweight structure to hold a timestamp and a pointer to a message. This structure is used with the FIFOs of
 * MessageConsumer.
 *
 * @tparam MSG_T Message type to hold
 */
template <typename MSG_T>
struct StampedMessagePtr {
  time::TimePoint timestamp;
  std::unique_ptr<MSG_T> message;
};

/**
 * @brief Structure to hold a timestamp and a const ref message pointer
 *
 * Lightweight structure to hold a timestamp and a const reference to a message. This structure is passed to users.
 *
 * @tparam MSG_T Message type to hold
 * @see MessageConsumer::Newest<MSG_T>()
 */
template <typename MSG_T>
struct StampedMessage {
  /**
   * @brief Construct a stamped message from a stamped message pointer.
   *
   * The pointer retains ownership, the constructed object is a non-owning view.
   *
   * @param ptr the pointer to created a non-owning view to
   */
  explicit StampedMessage(const StampedMessagePtr<MSG_T>& ptr) : StampedMessage{ptr.timestamp, *(ptr.message)} {}

  /**
   * @brief Construct a new Stamped Message object.
   *
   * @param timestamp the timestamp
   * @param message a const reference to the message, this struct is non-owning.
   */
  StampedMessage(time::TimePoint timestamp, const MSG_T& message) : timestamp{timestamp}, message{message} {}

  time::TimePoint timestamp;
  const MSG_T& message;
};

/**
 * @brief Structure to hold a message that is owned by the structure, i.e. not in the message pool.
 *
 * @tparam MSG_T Message type to hold
 */
template <typename MSG_T>
struct OwningStampedMessage {
  time::TimePoint timestamp = {};
  MSG_T message = {};
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_MESSAGE_CONSUMER_HPP_
