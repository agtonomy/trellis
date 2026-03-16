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

#ifndef TRELLIS_CORE_MESSAGE_TYPE_TUPLE_HPP_
#define TRELLIS_CORE_MESSAGE_TYPE_TUPLE_HPP_

#include "trellis/core/constraints.hpp"

namespace trellis::core {

/**
 * @brief A struct used to bundle message types to support automatic conversion
 *
 * @tparam SerializableT The serializable message type (typically a protobuf message).
 * @tparam MsgT The message type (typically a native struct).
 * @tparam ConverterT The converter type (a free function or functor).
 */
template <typename SerializableT, typename MsgT = SerializableT, typename ConverterT = std::identity>
  requires constraints::_IsSerializable<SerializableT>
struct TypeTuple {
  using SerializableType = SerializableT;
  using MsgType = MsgT;
  using ConverterType = ConverterT;
};

/**
 * @brief Concept that can be used to make sure that a type is a TypeTuple
 */
template <typename T>
concept _IsTypeTuple = requires {
  typename T::SerializableType;
  typename T::MsgType;
  typename T::ConverterType;
};

/**
 * @brief Concept that can be used to make sure that a type is a TypeTuple where the conversion is from the serializable
 * type to the message type
 */
template <typename T>
concept _IsReceiverTypeTuple =
    _IsTypeTuple<T> &&
    (constraints::_IsDynamic<typename T::SerializableType, typename T::MsgType, typename T::ConverterType> ||
     constraints::_IsConverter<typename T::ConverterType, typename T::SerializableType, typename T::MsgType>);

/**
 * @brief Concept that can be used to make sure that a type is a TypeTuple where the conversion is from the message type
 * to the serializable type
 */
template <typename T>
concept _IsSenderTypeTuple =
    _IsTypeTuple<T> &&
    constraints::_IsConverter<typename T::ConverterType, typename T::MsgType, typename T::SerializableType>;

}  // namespace trellis::core

#endif  // TRELLIS_CORE_MESSAGE_TYPE_TUPLE_HPP_
