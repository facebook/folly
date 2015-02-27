/*
 * Copyright 2015 Facebook, Inc.
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

#ifndef FOLLY_DETAIL_DISCRIMINATEDPTRDETAIL_H_
#define FOLLY_DETAIL_DISCRIMINATEDPTRDETAIL_H_

#include <type_traits>

namespace folly {
namespace dptr_detail {

/**
 * Given a target type and a list of types, return the 1-based index of the
 * type in the list of types.  Fail to compile if the target type doesn't
 * appear in the list.
 *
 * GetIndex<int, void, char, int>::value == 3
 * GetIndex<int, void, char>::value -> fails to compile
 */
template <typename... Types> struct GetTypeIndex;

// When recursing, we never reach the 0- or 1- template argument base case
// unless the target type is not in the list.  If the target type is in the
// list, we stop recursing when it is at the head of the remaining type
// list via the GetTypeIndex<T, T, Types...> partial specialization.
template <typename T, typename... Types>
struct GetTypeIndex<T, T, Types...> {
  static const size_t value = 1;
};

template <typename T, typename U, typename... Types>
struct GetTypeIndex<T, U, Types...> {
  static const size_t value = 1 + GetTypeIndex<T, Types...>::value;
};

// Generalize std::is_same for variable number of type arguments
template <typename... Types>
struct IsSameType;

template <>
struct IsSameType<> {
  static const bool value = true;
};

template <typename T>
struct IsSameType<T> {
  static const bool value = true;
};

template <typename T, typename U, typename... Types>
struct IsSameType<T, U, Types...> {
  static const bool value =
    std::is_same<T,U>::value && IsSameType<U, Types...>::value;
};

// Define type as the type of all T in (non-empty) Types..., asserting that
// all types in Types... are the same.
template <typename... Types>
struct SameType;

template <typename T, typename... Types>
struct SameType<T, Types...> {
  typedef T type;
  static_assert(IsSameType<T, Types...>::value,
                "Not all types in pack are the same");
};

// Determine the result type of applying a visitor of type V on a pointer
// to type T.
template <typename V, typename T>
struct VisitorResult1 {
  typedef typename std::result_of<V (T*)>::type type;
};

// Determine the result type of applying a visitor of type V on a const pointer
// to type T.
template <typename V, typename T>
struct ConstVisitorResult1 {
  typedef typename std::result_of<V (const T*)>::type type;
};

// Determine the result type of applying a visitor of type V on pointers of
// all types in Types..., asserting that the type is the same for all types
// in Types...
template <typename V, typename... Types>
struct VisitorResult {
  typedef typename SameType<
    typename VisitorResult1<V,Types>::type...>::type type;
};

// Determine the result type of applying a visitor of type V on const pointers
// of all types in Types..., asserting that the type is the same for all types
// in Types...
template <typename V, typename... Types>
struct ConstVisitorResult {
  typedef typename SameType<
    typename ConstVisitorResult1<V,Types>::type...>::type type;
};

template <typename V, typename R, typename... Types> struct ApplyVisitor1;

template <typename V, typename R>
struct ApplyVisitor1<V, R> {
  R operator()(size_t index, V&& visitor, void* ptr) const {
    CHECK(false);  // NOTREACHED
  }
};

template <typename V, typename R, typename T, typename... Types>
struct ApplyVisitor1<V, R, T, Types...> {
  R operator()(size_t index, V&& visitor, void* ptr) const {
    return (index == 1 ? visitor(static_cast<T*>(ptr)) :
            ApplyVisitor1<V, R, Types...>()(
              index - 1, std::forward<V>(visitor), ptr));
  }
};

template <typename V, typename R, typename... Types> struct ApplyConstVisitor1;

template <typename V, typename R>
struct ApplyConstVisitor1<V, R> {
  R operator()(size_t index, V&& visitor, void* ptr) const {
    CHECK(false);  // NOTREACHED
  }
};

template <typename V, typename R, typename T, typename... Types>
struct ApplyConstVisitor1<V, R, T, Types...> {
  R operator()(size_t index, V&& visitor, void* ptr) const {
    return (index == 1 ? visitor(static_cast<const T*>(ptr)) :
            ApplyConstVisitor1<V, R, Types...>()(
              index - 1, std::forward<V>(visitor), ptr));
  }
};

template <typename V, typename... Types>
struct ApplyVisitor
  : ApplyVisitor1<
      V, typename VisitorResult<V, Types...>::type, Types...> {
};

template <typename V, typename... Types>
struct ApplyConstVisitor
  : ApplyConstVisitor1<
      V, typename ConstVisitorResult<V, Types...>::type, Types...> {
};

}  // namespace dptr_detail
}  // namespace folly

#endif /* FOLLY_DETAIL_DISCRIMINATEDPTRDETAIL_H_ */
