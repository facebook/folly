#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <folly/experimental/pushmi/examples/bulk.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/just.h>

namespace pushmi {

PUSHMI_INLINE_VAR constexpr struct reduce_fn {
private:
  template <class BinaryOp>
  struct fn {
    BinaryOp binary_op_;
    template <class Acc, class Cursor>
    void operator()(Acc& acc, Cursor cursor) const {
      acc = binary_op_(acc, *cursor);
    }
  };
  struct identity {
    template <class T>
    auto operator()(T&& t) const {
      return (T&&) t;
    }
  };
public:
  template<class ExecutionPolicy, class ForwardIt, class T, class BinaryOp>
  T operator()(
    ExecutionPolicy&& policy,
    ForwardIt begin,
    ForwardIt end,
    T init,
    BinaryOp binary_op) const {
      return operators::just(std::move(init)) |
        operators::bulk(
          fn<BinaryOp>{binary_op},
          begin,
          end,
          policy,
          identity{},
          identity{}
        ) |
        operators::get<T>;
    }
} reduce {};

} // namespace pushmi
