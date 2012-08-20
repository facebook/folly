/*
 * Copyright 2012 Facebook, Inc.
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

#ifndef FOLLY_DETAIL_STATS_H_
#define FOLLY_DETAIL_STATS_H_

#include <cstdint>

namespace folly { namespace detail {

/*
 * Helper functions for how to perform division based on the desired
 * return type.
 */

// For floating point input types, do floating point division
template <typename ReturnType, typename ValueType>
typename std::enable_if<std::is_floating_point<ValueType>::value,
                        ReturnType>::type
avgHelper(ValueType sum, uint64_t count) {
  if (count == 0) { return ReturnType(0); }
  return static_cast<ReturnType>(sum / count);
}

// For floating point return types, do floating point division
template <typename ReturnType, typename ValueType>
typename std::enable_if<std::is_floating_point<ReturnType>::value &&
                        !std::is_floating_point<ValueType>::value,
                        ReturnType>::type
avgHelper(ValueType sum, uint64_t count) {
  if (count == 0) { return ReturnType(0); }
  return static_cast<ReturnType>(sum) / count;
}

// For signed integer input types, do signed division
template <typename ReturnType, typename ValueType>
typename std::enable_if<!std::is_floating_point<ReturnType>::value &&
                        !std::is_floating_point<ValueType>::value &&
                        std::is_signed<ValueType>::value,
                        ReturnType>::type
avgHelper(ValueType sum, uint64_t count) {
  if (count == 0) { return ReturnType(0); }
  return sum / static_cast<int64_t>(count);
}

// For unsigned integer input types, do unsigned division
template <typename ReturnType, typename ValueType>
typename std::enable_if<!std::is_floating_point<ReturnType>::value &&
                        !std::is_floating_point<ValueType>::value &&
                        std::is_unsigned<ValueType>::value,
                        ReturnType>::type
avgHelper(ValueType sum, uint64_t count) {
  if (count == 0) { return ReturnType(0); }
  return sum / count;
}


template<typename T>
struct Bucket {
 public:
  typedef T ValueType;

  Bucket()
    : sum(ValueType()),
      count(0) {}

  void clear() {
    sum = ValueType();
    count = 0;
  }

  void add(const ValueType& s, uint64_t c) {
    // TODO: It would be nice to handle overflow here.
    sum += s;
    count += c;
  }

  Bucket& operator+=(const Bucket& o) {
    add(o.sum, o.count);
    return *this;
  }

  Bucket& operator-=(const Bucket& o) {
    // TODO: It would be nice to handle overflow here.
    sum -= o.sum;
    count -= o.count;
    return *this;
  }

  template <typename ReturnType>
  ReturnType avg() const {
    return avgHelper<ReturnType>(sum, count);
  }

  ValueType sum;
  uint64_t count;
};

}} // folly::detail

#endif // FOLLY_DETAIL_STATS_H_
