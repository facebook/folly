/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/futures/test/ThenCompileTest.h>

using namespace folly;

TEST(Basic, thenVariants) {
  SomeClass anObject;

  {
    Future<B> f = someFuture<A>().then(&aFunction<Future<B>, Try<A>&&>);
  }
  {
    Future<B> f =
        someFuture<A>().then(&SomeClass::aStaticMethod<Future<B>, Try<A>&&>);
  }
  {
    Future<B> f = someFuture<A>().then(aStdFunction<Future<B>, Try<A>&&>());
  }
  {
    Future<B> f = someFuture<A>().then([&](Try<A>&&) {
      return someFuture<B>();
    });
  }
  {
    Future<B> f = someFuture<A>().then(&aFunction<Future<B>, Try<A> const&>);
  }
  {
    Future<B> f = someFuture<A>().then(
        &SomeClass::aStaticMethod<Future<B>, Try<A> const&>);
  }
  {
    Future<B> f =
        someFuture<A>().then(aStdFunction<Future<B>, Try<A> const&>());
  }
  {
    Future<B> f = someFuture<A>().then([&](Try<A> const&) {
      return someFuture<B>();
    });
  }
  {
    Future<B> f = someFuture<A>().then(&aFunction<Future<B>, Try<A>>);
  }
  {
    Future<B> f =
        someFuture<A>().then(&SomeClass::aStaticMethod<Future<B>, Try<A>>);
  }
  {
    Future<B> f = someFuture<A>().then(aStdFunction<Future<B>, Try<A>>());
  }
  {
    Future<B> f = someFuture<A>().then([&](Try<A>) { return someFuture<B>(); });
  }
  {
    Future<B> f = someFuture<A>().then(&aFunction<B, Try<A>&&>);
  }
  {
    Future<B> f = someFuture<A>().then(&SomeClass::aStaticMethod<B, Try<A>&&>);
  }
  {
    Future<B> f = someFuture<A>().then(aStdFunction<B, Try<A>&&>());
  }
  {
    Future<B> f = someFuture<A>().then([&](Try<A>&&) { return B(); });
  }
  {
    Future<B> f = someFuture<A>().then(&aFunction<B, Try<A> const&>);
  }
  {
    Future<B> f =
        someFuture<A>().then(&SomeClass::aStaticMethod<B, Try<A> const&>);
  }
  {
    Future<B> f = someFuture<A>().then(aStdFunction<B, Try<A> const&>());
  }
  {
    Future<B> f = someFuture<A>().then([&](Try<A> const&) { return B(); });
  }
  {
    Future<B> f = someFuture<A>().then(&aFunction<B, Try<A>>);
  }
  {
    Future<B> f = someFuture<A>().then(&SomeClass::aStaticMethod<B, Try<A>>);
  }
  {
    Future<B> f = someFuture<A>().then(aStdFunction<B, Try<A>>());
  }
  {
    Future<B> f = someFuture<A>().then([&](Try<A>) { return B(); });
  }
  {
    Future<B> f = someFuture<A>().thenValue(&aFunction<Future<B>, A&&>);
  }
  {
    Future<B> f =
        someFuture<A>().thenValue(&SomeClass::aStaticMethod<Future<B>, A&&>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(aStdFunction<Future<B>, A&&>());
  }
  {
    Future<B> f = someFuture<A>().thenValue([&](A&&) {
      return someFuture<B>();
    });
  }
  {
    Future<B> f = someFuture<A>().thenValue(&aFunction<Future<B>, A const&>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(
        &SomeClass::aStaticMethod<Future<B>, A const&>);
  }
  {
    Future<B> f =
        someFuture<A>().thenValue(aStdFunction<Future<B>, A const&>());
  }
  {
    Future<B> f = someFuture<A>().thenValue([&](A const&) {
      return someFuture<B>();
    });
  }
  {
    Future<B> f = someFuture<A>().thenValue(&aFunction<Future<B>, A>);
  }
  {
    Future<B> f =
        someFuture<A>().thenValue(&SomeClass::aStaticMethod<Future<B>, A>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(aStdFunction<Future<B>, A>());
  }
  {
    Future<B> f = someFuture<A>().thenValue([&](A) { return someFuture<B>(); });
  }
  {
    Future<B> f = someFuture<A>().thenValue(&aFunction<B, A&&>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(&SomeClass::aStaticMethod<B, A&&>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(aStdFunction<B, A&&>());
  }
  {
    Future<B> f = someFuture<A>().thenValue([&](A&&) { return B(); });
  }
  {
    Future<B> f = someFuture<A>().thenValue(&aFunction<B, A const&>);
  }
  {
    Future<B> f =
        someFuture<A>().thenValue(&SomeClass::aStaticMethod<B, A const&>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(aStdFunction<B, A const&>());
  }
  {
    Future<B> f = someFuture<A>().thenValue([&](A const&) { return B(); });
  }
  {
    Future<B> f = someFuture<A>().thenValue(&aFunction<B, A>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(&SomeClass::aStaticMethod<B, A>);
  }
  {
    Future<B> f = someFuture<A>().thenValue(aStdFunction<B, A>());
  }
  {
    Future<B> f = someFuture<A>().thenValue([&](A) { return B(); });
  }
  {
    Future<B> f = someFuture<A>().then(
        &SomeClass::aMethod<Future<B>, Try<A>&&>, &anObject);
  }
  {
    Future<B> f = someFuture<A>().then(
        &SomeClass::aMethod<Future<B>, Try<A> const&>, &anObject);
  }
  {
    Future<B> f =
        someFuture<A>().then(&SomeClass::aMethod<Future<B>, Try<A>>, &anObject);
  }
  {
    Future<B> f =
        someFuture<A>().then(&SomeClass::aMethod<B, Try<A>&&>, &anObject);
  }
  {
    Future<B> f =
        someFuture<A>().then(&SomeClass::aMethod<B, Try<A> const&>, &anObject);
  }
  {
    Future<B> f =
        someFuture<A>().then(&SomeClass::aMethod<B, Try<A>>, &anObject);
  }
}
