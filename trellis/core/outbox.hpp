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

#ifndef TRELLIS_CORE_OUTBOX_HPP_
#define TRELLIS_CORE_OUTBOX_HPP_

#include <optional>

#include "trellis/core/constraints.hpp"
#include "trellis/core/node.hpp"
#include "trellis/core/publisher.hpp"
#include "trellis/core/type_tuple.hpp"

namespace trellis::core {

/**
 * @brief Concept that must be satisfied by sender types used with @ref Outbox.
 *
 * A conforming type must expose:
 * - `SerializableType` — the wire-format (protobuf) type passed to the publisher.
 * - `MsgType` — the native C++ message type accepted by `UpdateMsg`. May equal `SerializableType`
 *   when no conversion is needed.
 * - `ConverterType` — a callable that converts `MsgType` → `SerializableType`.
 * - `Args` — an aggregate struct whose fields are the sender-specific constructor arguments
 *   (excluding the `Node&` that every sender receives). Defining `Args` as an aggregate allows
 *   callers to use brace-initialization at the `Outbox` construction site without naming the type.
 * - A constructor `T(Node&, Args&&)`.
 * - A method `UpdateMsg(std::optional<MsgType>&&)`.
 */
template <class T>
concept _Sender = requires {
  typename T::SerializableType;
  typename T::MsgType;
  typename T::ConverterType;
  typename T::Args;
  requires constraints::_IsConverter<typename T::ConverterType, typename T::MsgType, typename T::SerializableType>;
  requires requires(T t, Node& node) {
    { t.UpdateMsg(std::optional<typename T::MsgType>{}) };
    { T(node, typename T::Args{}) };
  };
};

/**
 * @brief A sender that buffers the most recent message and publishes it on a fixed timer.
 *
 * Each call to `UpdateMsg` overwrites the stored message. The timer callback publishes the stored
 * message (if any) and then clears it, so only one message is sent per period even if `UpdateMsg`
 * is called multiple times between ticks.
 *
 * Use this sender when the messages on the topic must be delivered at a fixed frequency that may differs from frequency
 * at which messages are computed (i.e., when the outbound messages need to be rate limited). The consumer will only
 * receive the latest value and must be able to tolerate up to one full timer period of latency.
 *
 * @tparam SerializableT Wire-format (protobuf) type passed to the publisher.
 * @tparam MsgT          Native C++ message type. Defaults to `SerializableT` (no conversion).
 * @tparam ConverterT    Callable `MsgT → SerializableT`. Defaults to `std::identity`.
 */
template <typename SerializableT, typename MsgT = SerializableT, typename ConverterT = std::identity>
  requires constraints::_IsSerializable<SerializableT> && _IsSenderTypeTuple<TypeTuple<SerializableT, MsgT, ConverterT>>
class AsyncSender {
 public:
  using SerializableType = SerializableT;
  using MsgType = MsgT;
  using ConverterType = ConverterT;

  /// @brief Constructor arguments for use with @ref Outbox (excludes the `Node&` parameter).
  struct Args {
    std::string topic;
    unsigned rate_ms;
    ConverterType converter = {};
  };

  AsyncSender(AsyncSender&) = delete;
  AsyncSender(AsyncSender&&) = delete;
  AsyncSender operator=(AsyncSender&) = delete;
  AsyncSender operator=(AsyncSender&&) = delete;

  AsyncSender(Node& node, Args&& args)
      : pub_{node.CreatePublisher<SerializableType, MsgType, ConverterType>(std::move(args.topic),
                                                                            std::move(args.converter))},
        timer_{node.CreateTimer(std::move(args.rate_ms), [this](const time::TimePoint&) { PublishMsg(); })} {}

  /**
   * @brief Store the message to be published on the next timer tick.
   *
   * Passing `std::nullopt` suppresses the next publication.
   */
  void UpdateMsg(std::optional<MsgType>&& msg) { opt_msg_ = std::move(msg); }

 private:
  std::optional<MsgType> opt_msg_;
  Publisher<SerializableType, MsgType, ConverterType> pub_;
  Timer timer_;

  /// Publishes the stored message (if present) and clears it.
  void PublishMsg() {
    if (opt_msg_.has_value()) {
      pub_->Send(opt_msg_.value());
      opt_msg_.reset();
    }
  }
};

/**
 * @brief A sender that publishes a optional message immediately upon every call to `UpdateMsg` if it has a value.
 *
 * Use this sender when the publish rate is controlled externally (e.g., by a timer or event).
 *
 * @tparam SerializableT Wire-format (protobuf) type passed to the publisher.
 * @tparam MsgT          Native C++ message type. Defaults to `SerializableT` (no conversion).
 * @tparam ConverterT    Callable `MsgT → SerializableT`. Defaults to `std::identity`.
 */
template <typename SerializableT, typename MsgT = SerializableT, typename ConverterT = std::identity>
  requires constraints::_IsSerializable<SerializableT> && _IsSenderTypeTuple<TypeTuple<SerializableT, MsgT, ConverterT>>
class ImmediateSender {
 public:
  using SerializableType = SerializableT;
  using MsgType = MsgT;
  using ConverterType = ConverterT;

  /// @brief Constructor arguments for use with @ref Outbox (excludes the `Node&` parameter).
  struct Args {
    std::string topic;
    ConverterType converter = {};
  };

  ImmediateSender(Node& node, Args&& args)
      : pub_{node.CreatePublisher<SerializableType, MsgType, ConverterType>(std::move(args.topic),
                                                                            std::move(args.converter))} {}

  /// @brief Publish @p msg immediately if it has value; otherwise, do nothing.
  void UpdateMsg(std::optional<MsgType>&& msg) {
    if (msg.has_value()) pub_->Send(std::move(msg.value()));
  }

 private:
  Publisher<SerializableType, MsgType, ConverterType> pub_;
};

/**
 * @brief Publishes a fixed set of topics atomically in a single `UpdateMsgs` call.
 *
 * `Outbox` owns one sender per topic. Each sender is constructed from a `Node` reference plus a
 * sender-specific `Args` struct. The `Args` structs are aggregates, so the call site can use
 * brace-initialization without naming the type:
 *
 * @code
 * Outbox<ImmediateSender<Foo>, AsyncSender<Bar>> outbox{
 *     node,
 *     {"topic_foo"},           // ImmediateSender::Args
 *     {"topic_bar", 50},       // AsyncSender::Args  (rate_ms = 50)
 * };
 * @endcode
 *
 * Messages are dispatched to each sender via `UpdateMsgs`, which accepts a tuple of
 * `std::optional<MsgType>` — one per sender in the same order as the template parameters.
 *
 * Supports opt-in automatic conversion from a native C++ type to the wire-format protobuf type.
 * To enable conversion for a slot, instantiate the corresponding sender with three template
 * arguments: `SerializableT`, `MsgT`, and `ConverterT`.
 *
 * @tparam SenderType... One or more types satisfying the @ref _Sender concept.
 */
template <_Sender... SenderType>
class Outbox {
 public:
  using Senders = std::tuple<std::unique_ptr<SenderType>...>;
  using Messages = std::tuple<std::optional<typename SenderType::MsgType>...>;

  /**
   * @brief Construct all senders.
   *
   * Each `SenderType::Args` argument is forwarded to the corresponding sender's constructor
   * alongside the shared @p node. Because `Args` is an aggregate on every conforming sender,
   * callers can omit the type name and use brace-initialization directly (see class-level docs).
   *
   * @param node Trellis node used to create publishers and timers.
   * @param args One `SenderType::Args` per sender, in the same order as the template parameters.
   */
  Outbox(Node& node, typename SenderType::Args&&... args)
      // Pack-expand both SenderType... and args... in lock-step to construct each unique_ptr.
      : senders_{std::make_unique<SenderType>(node, std::move(args))...} {}

  /**
   * @brief Dispatch one message to every sender.
   *
   * @param msgs A tuple of `std::optional<MsgType>` values, one per sender in template-parameter
   *             order. A `std::nullopt` entry skips publication for that sender.
   */
  void UpdateMsgs(Messages&& msgs) { Update(std::move(msgs), std::index_sequence_for<SenderType...>{}); }

 private:
  Senders senders_;

  /// Unpacks the index sequence so each sender can be addressed by position.
  template <size_t... Is>
  void Update(Messages&& msgs, std::index_sequence<Is...>) {
    // Fold expression: calls UpdateMsg on every sender with its corresponding message.
    (std::get<Is>(senders_)->UpdateMsg(std::move(std::get<Is>(msgs))), ...);
  }
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_OUTBOX_HPP_
