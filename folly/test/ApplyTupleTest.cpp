/*
 * Copyright 2014 Facebook, Inc.
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

#include <iostream>

#include "folly/ApplyTuple.h"
#include <gtest/gtest.h>

#include <memory>

namespace {

void func(int a, int b, double c) {
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 2);
  EXPECT_EQ(c, 3.0);
}

struct Wat {
  void func(int a, int b, double c) {
    ::func(a, b, c);
  }

  double retVal(int a, double b) {
    return a + b;
  }

  Wat() {}
  Wat(Wat const&) = delete;

  int foo;
};

struct Overloaded {
  int func(int) { return 0; }
  bool func(bool) { return true; }
};

struct Func {
  int operator()() const {
    return 1;
  }
};

struct CopyCount {
  CopyCount() {}
  CopyCount(CopyCount const&) {
    std::cout << "copy count copy ctor\n";
  }
};

void anotherFunc(CopyCount const&) {}

std::function<void (int, int, double)> makeFunc() {
  return &func;
}

struct GuardObjBase {
  GuardObjBase(GuardObjBase&&) {}
  GuardObjBase() {}
  GuardObjBase(GuardObjBase const&) = delete;
  GuardObjBase& operator=(GuardObjBase const&) = delete;
};
typedef GuardObjBase const& Guard;

template<class F, class Tuple>
struct GuardObj : GuardObjBase {
  explicit GuardObj(F&& f, Tuple&& args)
    : f_(std::move(f))
    , args_(std::move(args))
  {}
  GuardObj(GuardObj&& g)
    : GuardObjBase(std::move(g))
    , f_(std::move(g.f_))
    , args_(std::move(g.args_))
  {}

  ~GuardObj() {
    folly::applyTuple(f_, args_);
  }

  GuardObj(const GuardObj&) = delete;
  GuardObj& operator=(const GuardObj&) = delete;

private:
  F f_;
  Tuple args_;
};

template<class F, class ...Args>
GuardObj<typename std::decay<F>::type,std::tuple<Args...>>
guard(F&& f, Args&&... args) {
  return GuardObj<typename std::decay<F>::type,std::tuple<Args...>>(
    std::forward<F>(f),
    std::tuple<Args...>(std::forward<Args>(args)...)
  );
}

struct Mover {
  Mover() {}
  Mover(Mover&&) {}
  Mover(const Mover&) = delete;
  Mover& operator=(const Mover&) = delete;
};

void move_only_func(Mover&&) {}

}

TEST(ApplyTuple, Test) {
  auto argsTuple = std::make_tuple(1, 2, 3.0);
  auto func2 = func;
  folly::applyTuple(func2, argsTuple);
  folly::applyTuple(func, argsTuple);
  folly::applyTuple(func, std::make_tuple(1, 2, 3.0));
  folly::applyTuple(makeFunc(), std::make_tuple(1, 2, 3.0));
  folly::applyTuple(makeFunc(), argsTuple);

  std::unique_ptr<Wat> wat(new Wat);
  folly::applyTuple(&Wat::func, std::make_tuple(wat.get(), 1, 2, 3.0));
  auto argsTuple2 = std::make_tuple(wat.get(), 1, 2, 3.0);
  folly::applyTuple(&Wat::func, argsTuple2);

  EXPECT_EQ(10.0,
            folly::applyTuple(&Wat::retVal,
                              std::make_tuple(wat.get(), 1, 9.0)));

  auto test = guard(func, 1, 2, 3.0);
  CopyCount cpy;
  auto test2 = guard(anotherFunc, cpy);
  auto test3 = guard(anotherFunc, std::cref(cpy));

  Overloaded ovl;
  EXPECT_EQ(0,
            folly::applyTuple(
              static_cast<int (Overloaded::*)(int)>(&Overloaded::func),
              std::make_tuple(&ovl, 12)));
  EXPECT_EQ(true,
            folly::applyTuple(
              static_cast<bool (Overloaded::*)(bool)>(&Overloaded::func),
              std::make_tuple(&ovl, false)));

  int x = folly::applyTuple(std::plus<int>(), std::make_tuple(12, 12));
  EXPECT_EQ(24, x);

  Mover m;
  folly::applyTuple(move_only_func,
                    std::forward_as_tuple(std::forward<Mover>(Mover())));
}
