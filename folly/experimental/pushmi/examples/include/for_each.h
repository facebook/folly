#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <bulk.h>
#include <pushmi/o/submit.h>
#include <pushmi/o/just.h>

namespace pushmi {

template<class ExecutionPolicy, class RandomAccessIterator, class Function>
void for_each(
  ExecutionPolicy&& policy, 
  RandomAccessIterator begin, 
  RandomAccessIterator end, 
  Function f)
{
  operators::just(0) | 
    operators::bulk(
      [f](auto& acc, auto cursor){ f(*cursor); }, 
      begin,
      end, 
      policy, 
      [](auto&& args){ return args; }, 
      [](auto&& acc){ return 0; }) |
    operators::blocking_submit();
}

} // namespace pushmi
