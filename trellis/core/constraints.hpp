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

namespace trellis::core::constraints {

template <typename FuncT, typename SourceT, typename DestT>
concept _IsConverter = std::convertible_to<std::decay_t<std::invoke_result_t<FuncT, SourceT>>, DestT>;

template <typename SerializableT, typename MsgT, typename ConverterT>
concept _IsDynamic = std::same_as<SerializableT, google::protobuf::Message> && std::same_as<SerializableT, MsgT> &&
                     std::same_as<ConverterT, std::identity>;

}  // namespace trellis::core::constraints

#endif  // TRELLIS_CORE_CONSTRAINTS_HPP_
