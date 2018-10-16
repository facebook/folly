#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <bulk.h>
#include <pushmi/o/submit.h>
#include <pushmi/o/just.h>

namespace pushmi {

template<class ExecutionPolicy, class ForwardIt, class T, class BinaryOp>
T reduce(
  ExecutionPolicy&& policy,
  ForwardIt begin, 
  ForwardIt end, 
  T init, 
  BinaryOp binary_op){
    return operators::just(std::move(init)) | 
      operators::bulk(
        [binary_op](auto& acc, auto cursor){ acc = binary_op(acc, *cursor); }, 
        begin,
        end, 
        policy, 
        [](auto&& args){ return args; }, 
        [](auto&& acc){ return acc; }) |
      operators::get<T>;
    }

} // namespace pushmi
