/*
 * Copyright (C) 2026 Agtonomy
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

#ifndef TRELLIS_CORE_CONVERTERS_HPP_
#define TRELLIS_CORE_CONVERTERS_HPP_

namespace trellis::core::converters {

// Stateless converter that dispatches to an unqualified `FromProto(s)` so ADL finds the user's overload in the
// associated namespace of SerializableT. Intentionally defined in a namespace that contains no FromProto/ToProto of
// its own so ordinary lookup at definition finds nothing and ADL takes over at instantiation.
template <typename SerializableT, typename MsgT>
struct DefaultFromProto {
  MsgT operator()(const SerializableT& s) const { return FromProto(s); }
};

template <typename MsgT, typename SerializableT>
struct DefaultToProto {
  SerializableT operator()(const MsgT& m) const { return ToProto(m); }
};

}  // namespace trellis::core::converters

#endif  // TRELLIS_CORE_CONVERTERS_HPP_
