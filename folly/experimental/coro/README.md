# @title Coro

# Introduction

[folly::coro](https://github.com/facebook/folly/blob/master/folly/experimental/coro/) is a developer-friendly asynchronous C++ framework based on [Coroutines TS](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/n4775.pdf). It is available for any fbcode project that is built with Сlang and uses platform007.

## Basic example

```c++
folly::coro::Task<int> task42() {
  co_return 42;
}

folly::coro::Task<int> taskSlow43() {
  co_await folly::futures::sleep(std::chrono::seconds{1});
  co_return co_await task42() + 1;
}

int main() {
  ...
  CHECK_EQ(
      43,
      folly::coro::blockingWait(taskSlow43().scheduleOn(folly::getCPUExecutor().get())));
  ...
}
```
The same logic implemented with folly::SemiFuture:

```c++
folly::SemiFuture<int> task42() {
  return folly::makeSemiFuture().deferValue([](auto) {
    return 42;
  });
}

folly::SemiFuture<int> taskSlow43() {
  return folly::futures::sleep(std::chrono::seconds{1})
      .semi()
      .deferValue([](auto value) { return value + 1; });
}

int main() {
  ...
  CHECK_EQ(
      43,
      taskSlow43().via(folly::getCPUExecutor().get()).get());
  ...
}
```
## Features

* Better performance comparing to `folly::Future`
* Full-compatibility with `folly::Future` and `folly::SemiFuture`
* Asynchronous synchronization primitives (e.g. `coro::Baton`, `coro::Mutex`, `coro::SharedMutex`)
* Compatible with any other library based on Coroutines TS

# Overview

## Writing a coroutine

Any function that returns a `folly::coro::Task` and has at least one use of `co_await` or `co_return` is a coroutine.
NOTE: You have to always use `co_return` instead of `return` in coroutines.

```c++
folly::coro::Task<int> task42() {
  co_return 42;
}

folly::coro::Task<int> task43() {
  auto value = co_await task42();
  co_return value + 1;
}
```

## Starting a coroutine
Calling a `folly::coro::Task`-coroutine function captures the arguments, but doesn't start executing the coroutine immediately. Instead the coroutine is lazily started when you `co_await` the task.

Alternatively, you can start executing a coroutine from a normal function by attaching a `folly::Executor` with `scheduleOn()` and either calling `start()` or using `folly::coro::blockingWait()`.
```c++
folly::coro::Task<void> checkArg(int arg42) {
  CHECK_EQ(42, arg42);
  co_return;
}

void runCoroutine1() {
  int arg42 = 42;
  // coroutine arguments are captured here, not when we start the coroutine
  auto task = checkArg(arg42);
  arg42 = 43;
  folly::coro::blockingWait(std::move(task).scheduleOn(folly::getCPUExecutor().get()));
}

void runCoroutine2() {
  folly::SemiFuture<folly::Unit> f = 
    checkArg(42).scheduleOn(folly::getCPUExecutor().get()).start();
}
```

## Executor-stickiness

Every `folly::coro::Task` will always be running on the `Executor` on which it was launched, even if it `co_await`ed something that completed on a different `Executor`.

You can extract the Executor which the `folly::coro::Task` is running on by using `folly::coro::co_current_executor`.
```c++
folly::coro::Task<int> task42Slow() {
  // This doesn't suspend the coroutine, just extracts the Executor*
  folly::Executor* startExecutor = co_await folly::coro::co_current_executor;
  co_await folly::futures::sleep(std::chrono::seconds{1});
  folly::Executor* resumeExecutor = co_await folly::coro::co_current_executor; 
  CHECK_EQ(startExecutor, resumeExecutor);
}
```

By default, when a `folly::coro::Task` is awaited within the context of another `Task` it inherits the executor from the awaiting coroutine. If you want to run a child coroutine on a different executor then you can call `.scheduleOn()` to explicitly specify an alternative executor.
```c++
folly::coro::Task<void> foo() {
  co_await folly::futures::sleep(std::chrono::seconds{1});
  std::cout << "Current executor is " << (co_await folly::coro::co_current_executor) << std::endl;
}

folly::coro::Task<void> bar(folly::CPUThreadPoolExecutor* otherExecutor) {
  // Executes foo() on whatever execution context bar() was launched on.
  co_await foo();

  // Launches foo() on 'otherExecutor' and when it's done resumes this
  // coroutine on whatever executor bar() was launched on.
  co_await foo().scheduleOn(otherExecutor);
}
```

## Awaitables
You can `co_await` anything that implements the `Awaitable` concept (see Coroutines TS for more details). It can be `folly::coro::Task`, `folly::Future`, `folly::SemiFuture` etc. Keep in mind that an `Awaitable` may result in an exception, so you'll have to use try-catch blocks to handle errors.
```c++
folly::coro::Task<void> throwCoro() {
  throw std::logic_error("Expected");
  co_return;
}

folly::coro::Task<void> coro() {
  auto future42 = folly::makeSemiFuture(42);
  EXPECT_EQ(42, co_await std::move(future42));

  try {
    co_await throwCoro();
    LOG(FATAL) << "Unreachable";
  } catch (const std::logic_error&) {
  } catch (...) {
    LOG(FATAL) << "Unreachable";
  }
}
```

## folly::SemiFuture
Any `folly::coro::Task` can be converted to a `folly::SemiFuture` by calling the `.semi()` method.
NOTE: this allows using any existing `folly::Future` primitives (e.g. `collectAll()`, `collectAny()`) in coroutine code.
```c++
folly::coro::Task<int> task1();
folly::coro::Task<int> task2();

folly::coro::Task<int> sumTask() {
  auto f1 = task1().semi();
  auto f2 = task2().semi();

  folly::Try<int> r1, r2;
  std::tie(r1, r2) = co_await folly::collectAllSemiFuture(std::move(f1), std::move(f2));

  co_return *r1 + *r2;
}

```

## Lambdas
You can implement a lambda coroutine however you need to explicitly specify a return type - the compiler is not yet able to deduce the return type of a coroutine from the body.

IMPORTANT: You need to be very careful about the lifetimes of temporary lambda objects. Invoking a lambda coroutine returns a `folly::coro::Task` that captures a reference to the lambda and so if the returned Task is not immediately `co_await`ed then the task will be left with a dangling reference when the temporary lambda goes out of scope. \
\
Use the `folly::coro::co_invoke()` helper when immediately invoking a lambda coroutine to keep the lambda alive as long as the `Task`.

**BAD:** The following code has undefined behaviour
```c++
folly::coro::Task<Reply> coro_send(const Request&);

folly::SemiFuture<Reply> semifuture_send(const Request& request) {
  auto task = [request]() -> folly::coro::Task<Reply> {
    auto reply = co_await coro_send(request);
    if (reply.isError()) {
      LOG(reply.error().message());
    }
    co_return reply;
  }(); // <-- Whoops, lambda is destroyed at semicolon

  return std::move(task).semi();
}
```

**GOOD:** Use `co_invoke` to invoke the lambda to prevent the lambda from being destroyed.
```c++
folly::SemiFuture<Reply> semifuture_send(const Request& request) {
  auto task = folly::coro::co_invoke([request]() -> folly::coro::Task<Reply> {
    auto reply = co_await coro_send(request);
    if (reply.isError()) {
      LOG(reply.error().message());
    }
    co_return reply;
  });

  return std::move(task).semi();
}
```