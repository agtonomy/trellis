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

#ifndef TRELLIS_CONTAINERS_MULTI_FIFO_HPP_
#define TRELLIS_CONTAINERS_MULTI_FIFO_HPP_

#include "fifo.hpp"

namespace trellis {
namespace containers {

namespace {
template <class T1, class T2>
struct SameType {
  static const bool value = false;
};

template <class T>
struct SameType<T, T> {
  static const bool value = true;
};
}  // namespace

template <size_t MAX_SIZE, class... Types>
class MultiFifo {
 public:
  template <typename T>
  T Next() {
    return access<T>().Next();
  }

  template <typename T>
  T Newest() {
    return access<T>().Newest();
  }

  template <typename T>
  void Push(T x) {
    access<T>().Push(std::forward<T>(x));
  }

  template <typename T>
  size_t Size() {
    return access<T>().Size();
  }

 private:
  // The strategy here is to use a tuple to encapsulate all of the Fifos for the
  // types given by the variadic template arguments. The template voodo below
  // gives us a templated `access()` method, which retrieves the FIFO for the
  // given type from the tuple.
  // This solution was heavily inspired by
  // https://stackoverflow.com/questions/27941661/generating-one-class-member-per-variadic-template-argument
  using fifotupletype = std::tuple<Fifo<Types, std::mutex, MAX_SIZE>...>;
  template <int N, typename T>
  struct FifoOfType : SameType<T, typename std::tuple_element<N, fifotupletype>::type::value_type> {};

  template <int N, class T, class Tuple,
            bool Match = false>  // this =false is only for clarity
  struct MatchingField {
    static Fifo<T, std::mutex, MAX_SIZE>& get(Tuple& tp) {
      // The "non-matching" version
      return MatchingField<N + 1, T, Tuple, FifoOfType<N + 1, T>::value>::get(tp);
    }
  };

  template <int N, class T, class Tuple>
  struct MatchingField<N, T, Tuple, true> {
    static Fifo<T, std::mutex, MAX_SIZE>& get(Tuple& tp) { return std::get<N>(tp); }
  };

  template <typename T>
  Fifo<T, std::mutex, MAX_SIZE>& access() {
    return MatchingField<0, T, fifotupletype, FifoOfType<0, T>::value>::get(fifos_);
  }
  fifotupletype fifos_;
};

}  // namespace containers
}  // namespace trellis

#endif  // TRELLIS_CONTAINERS_MULTI_FIFO_HPP_
