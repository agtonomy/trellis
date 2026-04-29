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

#ifndef TRELLIS_CORE_BIND_HPP
#define TRELLIS_CORE_BIND_HPP

#include <functional>
#include <type_traits>
#include <utility>

#define bind0(fn, ...) std::bind(fn, this, ##__VA_ARGS__)
#define bind1(fn, ...) std::bind(fn, this, std::placeholders::_1, ##__VA_ARGS__)
#define bind2(fn, ...) std::bind(fn, this, std::placeholders::_1, std::placeholders::_2, ##__VA_ARGS__)
#define bind3(fn, ...) \
  std::bind(fn, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, ##__VA_ARGS__)

namespace trellis::core {

// Namespace holds some utilities that will make compiler errors more parseable.
namespace _bind {

// Extracts the owning class from any pointer-to-member type. The partial specialization
// `M C::*` deduces `C` (the class) and `M` (everything else — the member's type, including
// signature, cv-qualifiers, ref-qualifiers, and noexcept). Because `M` is a catch-all, one
// specialization covers every member-pointer shape the language can produce, instead of
// enumerating all the combinations of const/volatile/&/&&/noexcept etc.
//
// Instantiating the template in `BindToThis` means that a compiler error will point to bind
// site with a reason the binding failed instead of the call site with a million line SFINAE
// "constraints not satisfied" message. (Ask me how I know...)
template <typename T>
struct MemberClass;

template <typename M, typename C>
struct MemberClass<M C::*> {
  using type = C;
};

// Checks whether `(self->*method)(args...)` is well-formed
template <typename Method, typename Self, typename... Args>
concept InvocableOnSelf = std::is_invocable_v<Method, Self*, Args...>;

}  // namespace _bind

template <typename Method, typename Self>
auto BindThis(Method method, Self* self) {
  static_assert(std::is_member_function_pointer_v<Method>, "BindToThis requires a pointer to member function");
  using ClassOf = typename _bind::MemberClass<Method>::type;
  static_assert(std::is_base_of_v<ClassOf, std::remove_cv_t<Self>>,
                "BindToThis: 'self' must point to the class that owns 'method' (or a derived class)");
  return [self, method]<typename... Args>(Args&&... args) noexcept(
             std::is_nothrow_invocable_v<Method, Self*, Args...>) -> decltype(auto)
           requires _bind::InvocableOnSelf<Method, Self, Args...>
  { return (self->*method)(std::forward<Args>(args)...); };
}

}  // namespace trellis::core

#endif  // TRELLIS_CORE_BIND_HPP
