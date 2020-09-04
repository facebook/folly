/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/io/async/Request.h>

namespace folly {

class TestData : public RequestData {
 public:
  explicit TestData(int data) : data_(data) {}
  ~TestData() override {}

  bool hasCallback() override {
    return true;
  }

  void onSet() override {
    set_++;
    onSetRctx = RequestContext::get();
  }

  void onUnset() override {
    unset_++;
    onUnSetRctx = RequestContext::get();
  }

  int set_ = 0, unset_ = 0;
  int data_;
  RequestContext* onSetRctx = nullptr;
  RequestContext* onUnSetRctx = nullptr;
};

} // namespace folly
