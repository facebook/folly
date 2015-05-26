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

#pragma once

#include <folly/wangle/channel/Handler.h>
#include <gmock/gmock.h>

namespace folly { namespace wangle {

template <class Rin, class Rout = Rin, class Win = Rout, class Wout = Rin>
class MockHandler : public Handler<Rin, Rout, Win, Wout> {
 public:
  typedef typename Handler<Rin, Rout, Win, Wout>::Context Context;

  MockHandler() = default;
  MockHandler(MockHandler&&) = default;

#ifdef __clang__
# pragma clang diagnostic push
# if __clang_major__ > 3 || __clang_minor__ >= 6
#  pragma clang diagnostic ignored "-Winconsistent-missing-override"
# endif
#endif

  MOCK_METHOD2_T(read_, void(Context*, Rin&));
  MOCK_METHOD1_T(readEOF, void(Context*));
  MOCK_METHOD2_T(readException, void(Context*, exception_wrapper));

  MOCK_METHOD2_T(write_, void(Context*, Win&));
  MOCK_METHOD1_T(close_, void(Context*));

  MOCK_METHOD1_T(attachPipeline, void(Context*));
  MOCK_METHOD1_T(attachTransport, void(Context*));
  MOCK_METHOD1_T(detachPipeline, void(Context*));
  MOCK_METHOD1_T(detachTransport, void(Context*));

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  void read(Context* ctx, Rin msg) override {
    read_(ctx, msg);
  }

  Future<void> write(Context* ctx, Win msg) override {
    return makeFutureWith([&](){
      write_(ctx, msg);
    });
  }

  Future<void> close(Context* ctx) override {
    return makeFutureWith([&](){
      close_(ctx);
    });
  }
};

template <class R, class W = R>
using MockHandlerAdapter = MockHandler<R, R, W, W>;

}}
