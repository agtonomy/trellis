#ifndef TRELLIS_TRELLIS_CORE_INBOX_HPP_
#define TRELLIS_TRELLIS_CORE_INBOX_HPP_

#include "trellis/core/message_consumer.hpp"

namespace trellis::core {

/**
 * @brief A simple wrapper around MessageConsumer that grabs the lastest unexpired message of each type.
 *
 * TODO(matt): Extend to support multiple topics per type.
 * TODO(matt): Extend to support N latest messages instead of only the latest.
 *
 * @tparam Types the message types to receive
 */
template <typename... Types>
class Inbox {
 public:
  using MessageConsumer_t = MessageConsumer<1U, Types...>;
  using MessageTimeouts = std::array<time::TimePoint::duration, sizeof...(Types)>;

  /**
   * @brief Construct a new Inbox object.
   *
   * @param node the node to get the subscribers from
   * @param topics the topic for each type to subscribe to
   * @param timeouts the timeout for each type
   */
  Inbox(Node& node, MessageConsumer_t::SingleTopicArray topics, MessageTimeouts timeouts)
      : message_consumer_{node, std::move(topics)}, timeouts_{std::move(timeouts)} {}

  // TODO(matt): Consider moving `StampedMessage` outside of the templated `MessageConsumer` class as it does not depend
  // on the template types of the `MessageConsumer`.
  using LatestMessages = std::tuple<std::optional<typename MessageConsumer_t::StampedMessage<Types>>...>;

  /**
   * @brief Gets the latest message of each type that is not expired (past the corresponding timeout).
   *
   * @param time the current time at which to check the inbox
   * @return Latest message of each type (if it exists)
   */
  LatestMessages GetLatestMessages(const time::TimePoint& time) { return {GetLatestMessage<Types>(time)...}; }

 private:
  template <typename Type>
  std::optional<typename MessageConsumer_t::StampedMessage<Type>> GetLatestMessage(const time::TimePoint& time) {
    if (message_consumer_.template Size<Type>() == 0) return std::nullopt;

    constexpr auto index = detail::Index<Type, std::tuple<Types...>>::value;

    // Not const to allow move.
    auto stamped_message =
        message_consumer_.template Newest<Type>();  // Lightweight, only reference to message not value.

    if (stamped_message.timestamp < time - timeouts_[index]) return std::nullopt;

    return {std::move(stamped_message)};
  }

  MessageConsumer_t message_consumer_;
  MessageTimeouts timeouts_;
};

}  // namespace trellis::core

#endif  // TRELLIS_TRELLIS_CORE_INBOX_HPP_
