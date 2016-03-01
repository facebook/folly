/*
 * Copyright 2016 Facebook, Inc.
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

#include <gtest/gtest.h>

#include <folly/futures/Future.h>
#include <type_traits>

using namespace folly;

class A {
 public:
  virtual ~A() {}
};

class B : public A {
 public:
  explicit B(std::string msg) : msg_(msg) {}

  const std::string& getMsg() { return msg_; }
  std::string msg_;
};

TEST(Convert, subclassConstruct) {
  Future<std::unique_ptr<A>> future =
      makeFuture<std::unique_ptr<B>>(folly::make_unique<B>("hello world"));
  std::unique_ptr<A> a = future.get();
  A* aptr = a.get();
  B* b = dynamic_cast<B*>(aptr);
  EXPECT_NE(nullptr, b);
  EXPECT_EQ("hello world", b->getMsg());
}

TEST(Convert, subclassAssign) {
  Future<std::unique_ptr<B>> f1 =
      makeFuture<std::unique_ptr<B>>(folly::make_unique<B>("hello world"));
  Future<std::unique_ptr<A>> f2 = std::move(f1);
  std::unique_ptr<A> a = f2.get();
  A* aptr = a.get();
  B* b = dynamic_cast<B*>(aptr);
  EXPECT_NE(nullptr, b);
  EXPECT_EQ("hello world", b->getMsg());
}

TEST(Convert, ConversionTests) {
  static_assert(std::is_convertible<Future<std::unique_ptr<B>>,
                                    Future<std::unique_ptr<A>>>::value,
                "Unique ptr not convertible");
  static_assert(!std::is_convertible<Future<std::unique_ptr<A>>,
                                     Future<std::unique_ptr<B>>>::value,
                "Underlying types are not convertible");
  static_assert(!std::is_convertible<Future<B>, Future<A>>::value,
                "Underlying types are not the same size");
  static_assert(sizeof(detail::Core<std::unique_ptr<A>>) ==
                    sizeof(detail::Core<std::unique_ptr<B>>),
                "Sizes of types are not the same");
}
