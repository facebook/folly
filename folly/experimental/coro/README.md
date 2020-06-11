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
      .deferValue([](auto) { return task42(); })
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
You can `co_await` anything that implements the `Awaitable` concept (see Coroutines TS for more details).
It can be `folly::coro::Task`, `folly::Future`, `folly::SemiFuture` etc.

Keep in mind that an `Awaitable` may result in an exception, so you'll have to use try-catch blocks to handle errors.
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

## Concurrently awaiting multiple Tasks

Normally, when you call another `folly::coro::Task`-returning coroutine it doesn't
start executing until you `co_await` the returned task and doing so will suspend
the awaiting coroutine until the operation completes.

This means that you cannot perform two operations concurrently by simply calling
the two coroutines and later awaiting them both.

**SLOWER: The following will execute the two operations sequentially**
```c++
folly::coro::Task<int> task1();
folly::coro::Task<int> task2();

folly::coro::Task<int> example() {
  auto t1 = task1();
  auto t2 = task2();
  int result1 = co_await std::move(t1);
  int result2 = co_await std::move(t2);
  co_return result1 + result2;
}
```

If, instead, you want to perform these operations concurrently and wait until
both of the operations complete you can use `folly::coro::collectAll()`.

**FASTER: The following _may_ execute the two operations concurrently**
```c++
folly::coro::Task<int> task1();
folly::coro::Task<int> task2();

folly::coro::Task<int> example() {
  auto [result1, result2] =
      co_await folly::coro::collectAll(task1(), task2());
  co_return result1 + result2;
}
```

Note that in the above example, when the `co_await` expression is evaluated
it first launches the `task1()` coroutine and it will execute in the current
thread until it reaches its first suspend-point, at which point it will then
launch `task2()`. Once both sub-tasks are complete then the `example()`
coroutine is resumed with a tuple of the individual results.

Note that if both `task1()` and `task2()` complete synchronously then they
will still be executed sequentially.

## Handling partial failure

When executing multiple sub-tasks concurrently it's possible that some of those
tasks will fail with an exception and some will succeed.

If you use the `folly::coro::collectAll()` function to concurrently wait for
multiple tasks to complete then any partial results are discarded if any of
the tasks complete with an exception. If multiple sub-tasks complete with an
exception then one of the exceptions is rethrown as the result and the others
are discarded.

If you need to be able determine which sub-operation failed or if you need
to be able to retrieve partial results then you can use `folly::coro::collectAllTry()`
instead. Instead of producing a tuple of the results it produces a tuple of
`folly::Try<T>` values, one for each input task.

```c++
folly::coro::Task<int> task1();
folly::coro::Task<int> task2();

folly::coro::Task<int> example() {
  auto [try1, try2] = co_await folly::coro::collectAllTry(task1(), task2());
  int result = 0;

  if (try1.hasValue()) {
    result += try1.value();
  } else {
    LOG(ERROR) << "Error in task1(): " << try1.exception().what();
  }

  if (try2.hasValue()) {
    result += try2.value();
  } else {
    LOG(ERROR) << "Error in task2(): " << try2.exception().what();
  }

  co_return result;
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

## Writing loops with coroutines

### Sequential retry loop

```c++
folly::coro::Task<void> pingServer();

folly::coro::Task<void> pingServerWithRetry(int retryCount) {
  for (int retry = 0; retry <= retryCount; ++retry) {
    try {
      co_await pingServer();
      co_return;
    } catch (...) {
      LOG(WARNING) << "Ping attempt " << retry << " failed";
      if (retry == retryCount) throw;
    }
    // Wait before trying again.
    co_await folly::futures::sleep(10ms);
  }
}
```

### Concurrently execute many operations

Operations with side-effects:
```c++
folly::coro::Task<void> doWork(int i);

folly::coro::Task<void> example(int count) {
  std::vector<folly::coro::SemiFuture<Unit>> tasks;
  for (int i = 0; i < count; ++i) {
    tasks.push_back(doWork(i).semi());
  }
  co_await folly::collectAllSemiFuture(tasks.begin(), tasks.end());
}
```

Operations that return values:
```c++
folly::coro::Task<std::string> getString(int i);

folly::coro::Task<void> example(int count) {
  std::vector<folly::coro::SemiFuture<Unit>> tasks;
  for (int i = 0; i < count; ++i) {
    tasks.push_back(getString(i).semi());
  }

  // Concurrently wait for all of these tasks.
  std::vector<std::string> strings =
      co_await folly::collectAllSemiFuture(tasks.begin(), tasks.end());

  // ... use 'strings'
}
```
