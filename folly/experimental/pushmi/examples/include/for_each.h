#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <bulk.h>
#include <pushmi/o/submit.h>
#include <pushmi/o/just.h>

namespace pushmi {

PUSHMI_INLINE_VAR constexpr struct for_each_fn {
private:
  template <class Function>
  struct fn {
    Function f_;
    template <class Cursor>
    void operator()(detail::any, Cursor cursor) const {
      f_(*cursor);
    }
  };
  struct identity {
    template <class T>
    auto operator()(T&& t) const { return (T&&) t; }
  };
  struct zero {
    int operator()(detail::any) const noexcept { return 0; }
  };
public:
  template<class ExecutionPolicy, class RandomAccessIterator, class Function>
  void operator()(
      ExecutionPolicy&& policy,
      RandomAccessIterator begin,
      RandomAccessIterator end,
      Function f) const {
    operators::just(0) |
      operators::bulk(
        fn<Function>{f},
        begin,
        end,
        policy,
        identity{},
        zero{}
      ) |
      operators::blocking_submit();
  }
} for_each {};

} // namespace pushmi
