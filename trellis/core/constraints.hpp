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

#ifndef TRELLIS_CORE_CONSTRAINTS_HPP_
#define TRELLIS_CORE_CONSTRAINTS_HPP_

#include <google/protobuf/message.h>

#include <concepts>
#include <cstddef>

namespace trellis::core::constraints {

// A type that reports its serialized size and writes exactly that many bytes. Protobuf messages qualify, and so do
// types that serialize a large payload straight into the output buffer.
template <typename T>
concept _IsSerializer = requires(const T t, void* write_data, int size) {
  { t.ByteSizeLong() } -> std::same_as<size_t>;
  { t.SerializeToArray(write_data, size) } -> std::same_as<bool>;
};

template <typename T>
concept _IsSerializable = _IsSerializer<T> && requires(T t, const void* read_data, int size) {
  { t.ParseFromArray(read_data, size) } -> std::same_as<bool>;
};

template <typename FuncT, typename SourceT, typename DestT>
concept _IsConverter = std::convertible_to<std::decay_t<std::invoke_result_t<FuncT, SourceT>>, DestT>;

// True for publishers/subscribers whose serializable and native message types are both the abstract
// `google::protobuf::Message`. Used to gate the runtime-dynamic discovery and dispatch paths. The dynamic flow
// (CreateDynamicPublisher / CreateDynamicSubscriber) always uses MsgT = SerializableT = Message; requiring both here
// preserves that invariant defensively. ConverterT is intentionally not constrained because the default converter type
// differs by direction (FromProto vs. ToProto), and the converter is never invoked on the dynamic path anyway.
template <typename SerializableT, typename MsgT>
concept _IsDynamic = std::same_as<SerializableT, google::protobuf::Message> && std::same_as<MsgT, SerializableT>;

}  // namespace trellis::core::constraints

#endif  // TRELLIS_CORE_CONSTRAINTS_HPP_
