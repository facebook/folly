/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>

#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

namespace for_each_detail {

namespace adl {

/* using override */
using std::begin;
/* using override */
using std::end;
/* using override */
using std::get;

/**
 * The adl_ functions below lookup the function name in the namespace of the
 * type of the object being passed into the function.  If no function with
 * that name exists for the passed object then the default std:: versions are
 * going to be called
 */
template <std::size_t Index, typename Type>
auto adl_get(Type&& instance) -> decltype(get<Index>(std::declval<Type>())) {
  return get<Index>(std::forward<Type>(instance));
}
template <typename Type>
auto adl_begin(Type&& instance) -> decltype(begin(instance)) {
  return begin(instance);
}
template <typename Type>
auto adl_end(Type&& instance) -> decltype(end(instance)) {
  return end(instance);
}

} // namespace adl

/**
 * Enable if the range supports fetching via non member get<>()
 */
template <typename T>
using EnableIfNonMemberGetFound =
    void_t<decltype(adl::adl_get<0>(std::declval<T>()))>;
/**
 * Enable if the range supports fetching via a member get<>()
 */
template <typename T>
using EnableIfMemberGetFound =
    void_t<decltype(std::declval<T>().template get<0>())>;

/**
 * A get that tries ADL get<> first and if that is not found tries to execute
 * a member function get<> on the instance, just as proposed by the structured
 * bindings proposal here 11.5.3
 * http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4659.pdf
 */
template <std::size_t Index, typename Type, typename = void>
struct Get {
  template <typename T>
  static auto impl(T&& instance)
      -> decltype(adl::adl_get<Index>(std::declval<T>())) {
    return adl::adl_get<Index>(std::forward<T>(instance));
  }
};
template <std::size_t Index, typename Type>
struct Get<Index, Type, EnableIfMemberGetFound<Type>> {
  template <typename T>
  static auto impl(T&& instance)
      -> decltype(std::declval<T>().template get<Index>()) {
    return std::forward<T>(instance).template get<Index>();
  }
};

/**
 * Concepts-ish
 */
/**
 * Check if the range is a tuple or a range
 */
template <typename Type, typename T = typename std::decay<Type>::type>
using EnableIfTuple = void_t<
    decltype(Get<0, T>::impl(std::declval<T>())),
    decltype(std::tuple_size<T>::value)>;

/**
 * Check if the range is a range
 */
template <typename Type, typename T = typename std::decay<Type>::type>
using EnableIfRange = void_t<
    decltype(adl::adl_begin(std::declval<T>())),
    decltype(adl::adl_end(std::declval<T>()))>;

/**
 * Forwards the return value of the first element of the range, used to
 * determine the type of the first element in the range in SFINAE use cases
 */
template <typename Sequence, typename = void>
struct DeclvalSequence {
  using type = decltype(*(adl::adl_begin(std::declval<Sequence>())));
};

template <typename Sequence>
struct DeclvalSequence<Sequence, EnableIfTuple<Sequence>> {
  using type = decltype(Get<0, Sequence>::impl(std::declval<Sequence>()));
};

/**
 * Check if the functor accepts one or two arguments, one of the first element
 * in the range, assuming that all the other elements can also be passed to the
 * functor, and the second being an instantiation of std::integral_constant,
 * and the third being an instantiation of LoopControl, to provide
 * breakability to the loop
 */
template <typename Sequence, typename Func>
using EnableIfAcceptsOneArgument = void_t<decltype(std::declval<Func>()(
    std::declval<typename DeclvalSequence<Sequence>::type>()))>;
template <typename Sequence, typename Func>
using EnableIfAcceptsTwoArguments = void_t<decltype(std::declval<Func>()(
    std::declval<typename DeclvalSequence<Sequence>::type>(),
    std::integral_constant<std::size_t, 0>{}))>;
template <typename Sequence, typename Func>
using EnableIfAcceptsThreeArguments = void_t<decltype(std::declval<Func>()(
    std::declval<typename DeclvalSequence<Sequence>::type>(),
    std::integral_constant<std::size_t, 0>{},
    adl::adl_begin(std::declval<Sequence>())))>;
template <typename Sequence, typename Func>
using EnableIfBreaksRange = std::enable_if_t<std::is_same<
    typename std::decay<decltype(std::declval<Func>()(
        std::declval<typename DeclvalSequence<Sequence>::type>(),
        std::size_t{0},
        adl::adl_begin(std::declval<Sequence>())))>::type,
    LoopControl>::value>;
template <typename Sequence, typename Func>
using EnableIfBreaksTuple = std::enable_if_t<std::is_same<
    typename std::decay<decltype(std::declval<Func>()(
        std::declval<typename DeclvalSequence<Sequence>::type>(),
        std::integral_constant<std::size_t, 0>{}))>::type,
    LoopControl>::value>;
/**
 * Enables if the sequence has random access iterators
 */
template <typename Sequence>
using EnableIfRandomAccessIterators = std::enable_if_t<std::is_same<
    typename std::iterator_traits<typename std::decay<decltype(
        adl::adl_begin(std::declval<Sequence>()))>::type>::iterator_category,
    std::random_access_iterator_tag>::value>;
template <typename Sequence, typename Index>
using EnableIfHasIndexingOperator =
    void_t<decltype(std::declval<Sequence>()[std::declval<Index>()])>;

/**
 * Implementation for the range iteration, this provides specializations in
 * the case where the function returns a break or continue.
 */
template <typename Seq, typename F, typename = void>
struct ForEachRange {
  template <typename Sequence, typename Func>
  static void impl(Sequence&& range, Func& func) {
    auto first = adl::adl_begin(range);
    auto last = adl::adl_end(range);
    for (auto index = std::size_t{0}; first != last; ++index) {
      auto next = std::next(first);
      func(*first, index, first);
      first = next;
    }
  }
};

template <typename Seq, typename F>
struct ForEachRange<Seq, F, EnableIfBreaksRange<Seq, F>> {
  template <typename Sequence, typename Func>
  static void impl(Sequence&& range, Func& func) {
    auto first = adl::adl_begin(range);
    auto last = adl::adl_end(range);
    for (auto index = std::size_t{0}; first != last; ++index) {
      auto next = std::next(first);
      if (loop_break == func(*first, index, first)) {
        break;
      }
      first = next;
    }
  }
};

/**
 * Implementations for the runtime function
 */
template <
    typename Sequence,
    typename Func,
    EnableIfAcceptsThreeArguments<Sequence, Func>* = nullptr>
void for_each_range_impl(Sequence&& range, Func& func) {
  ForEachRange<Sequence, Func>::impl(std::forward<Sequence>(range), func);
}
template <
    typename Sequence,
    typename Func,
    EnableIfAcceptsTwoArguments<Sequence, Func>* = nullptr>
void for_each_range_impl(Sequence&& range, Func& func) {
  // make a three arg adaptor for the function passed in so that the main
  // implementation function can be used
  auto three_arg_adaptor = [&func](
                               auto&& ele, auto index, auto) -> decltype(auto) {
    return func(std::forward<decltype(ele)>(ele), index);
  };
  for_each_range_impl(std::forward<Sequence>(range), three_arg_adaptor);
}

template <
    typename Sequence,
    typename Func,
    EnableIfAcceptsOneArgument<Sequence, Func>* = nullptr>
void for_each_range_impl(Sequence&& range, Func& func) {
  // make a three argument adaptor for the function passed in that just ignores
  // the second and third argument
  auto three_arg_adaptor = [&func](auto&& ele, auto, auto) -> decltype(auto) {
    return func(std::forward<decltype(ele)>(ele));
  };
  for_each_range_impl(std::forward<Sequence>(range), three_arg_adaptor);
}

/**
 * Handlers for iteration
 */
/**
 * The class provides a way to tell whether the function passed in to the
 * algorithm returns an instance of LoopControl, if it does then the break-able
 * implementation will be used.  If the function provided to the algorithm
 * does not use the break API, then the basic no break, 0 overhead
 * implementation will be used
 */
template <typename Seq, typename F, typename = void>
struct ForEachTupleImpl {
  template <typename Sequence, typename Func, std::size_t... Indices>
  static void
  impl(Sequence&& seq, Func& func, std::index_sequence<Indices...>) {
    // unroll the loop in an initializer list construction parameter expansion
    // pack
    static_cast<void>(std::initializer_list<int>{
        (func(
             Get<Indices, Sequence>::impl(std::forward<Sequence>(seq)),
             std::integral_constant<std::size_t, Indices>{}),
         0)...});
  }
};
template <typename Seq, typename F>
struct ForEachTupleImpl<Seq, F, EnableIfBreaksTuple<Seq, F>> {
  template <typename Sequence, typename Func, std::size_t... Indices>
  static void
  impl(Sequence&& seq, Func& func, std::index_sequence<Indices...>) {
    // unroll the loop in an initializer list construction parameter expansion
    // pack
    LoopControl break_or_not = LoopControl::CONTINUE;

    // cast to void to ignore the result, use the initialzer list constructor
    // to do the loop execution, the ternary conditional will decide whether
    // or not to evaluate the result
    static_cast<void>(std::initializer_list<int>{
        (((break_or_not == loop_continue)
              ? (break_or_not = func(
                     Get<Indices, Sequence>::impl(std::forward<Sequence>(seq)),
                     std::integral_constant<std::size_t, Indices>{}))
              : (loop_continue)),
         0)...});
  }
};

/**
 * The two top level compile time loop iteration functions handle the dispatch
 * based on the number of arguments the passed in function can be passed, if 2
 * arguments can be passed then the implementation dispatches work further to
 * the implementation classes above.  If not then an adaptor is constructed
 * which is passed on to the 2 argument specialization, which then in turn
 * forwards implementation to the implementation classes above
 */
template <
    typename Sequence,
    typename Func,
    EnableIfAcceptsTwoArguments<Sequence, Func>* = nullptr>
void for_each_tuple_impl(Sequence&& seq, Func& func) {
  // pass the length as an index sequence to the implementation as an
  // optimization over manual template "tail recursion" unrolling
  constexpr auto length =
      std::tuple_size<typename std::decay<Sequence>::type>::value;
  ForEachTupleImpl<Sequence, Func>::impl(
      std::forward<Sequence>(seq), func, std::make_index_sequence<length>{});
}
template <
    typename Sequence,
    typename Func,
    EnableIfAcceptsOneArgument<Sequence, Func>* = nullptr>
void for_each_tuple_impl(Sequence&& seq, Func& func) {
  // make an adaptor for the function passed in, in case it can only be passed
  // on argument
  auto two_arg_adaptor = [&func](auto&& ele, auto) -> decltype(auto) {
    return func(std::forward<decltype(ele)>(ele));
  };
  for_each_tuple_impl(std::forward<Sequence>(seq), two_arg_adaptor);
}

/**
 * Top level handlers for the for_each loop, the basic specialization handles
 * ranges and the specialized version handles compile time ranges (tuple like)
 *
 * This implies that if a range is a compile time range, its compile time
 * get<> API (whether through a member function or through a ADL looked up
 * method) will be used in preference over iterators
 */
template <typename R, typename = void>
struct ForEachImpl {
  template <typename Sequence, typename Func>
  static void impl(Sequence&& range, Func& func) {
    for_each_tuple_impl(std::forward<Sequence>(range), func);
  }
};
template <typename R>
struct ForEachImpl<R, EnableIfRange<R>> {
  template <typename Sequence, typename Func>
  static void impl(Sequence&& range, Func& func) {
    for_each_range_impl(std::forward<Sequence>(range), func);
  }
};

template <typename S, typename I, typename = void>
struct FetchIteratorIndexImpl {
  template <typename Sequence, typename Index>
  static decltype(auto) impl(Sequence&& sequence, Index&& index) {
    return std::forward<Sequence>(sequence)[std::forward<Index>(index)];
  }
};
template <typename S, typename I>
struct FetchIteratorIndexImpl<S, I, EnableIfRandomAccessIterators<S>> {
  template <typename Sequence, typename Index>
  static decltype(auto) impl(Sequence&& sequence, Index index) {
    return *(adl::adl_begin(std::forward<Sequence>(sequence)) + index);
  }
};
template <typename S, typename = void>
struct FetchImpl {
  template <typename Sequence, typename Index>
  static decltype(auto) impl(Sequence&& sequence, Index index) {
    return Get<static_cast<std::size_t>(index), Sequence>::impl(
        std::forward<Sequence>(sequence));
  }
};
template <typename S>
struct FetchImpl<S, EnableIfRange<S>> {
  template <typename Sequence, typename Index>
  static decltype(auto) impl(Sequence&& sequence, Index&& index) {
    return FetchIteratorIndexImpl<Sequence, Index>::impl(
        std::forward<Sequence>(sequence), std::forward<Index>(index));
  }
};

} // namespace for_each_detail

template <typename Sequence, typename Func>
FOLLY_CPP14_CONSTEXPR Func for_each(Sequence&& range, Func func) {
  for_each_detail::ForEachImpl<typename std::decay<Sequence>::type>::impl(
      std::forward<Sequence>(range), func);
  return func;
}

template <typename Sequence, typename Index>
FOLLY_CPP14_CONSTEXPR decltype(auto) fetch(Sequence&& sequence, Index&& index) {
  return for_each_detail::FetchImpl<Sequence>::impl(
      std::forward<Sequence>(sequence), std::forward<Index>(index));
}

} // namespace folly
