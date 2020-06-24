# folly/fibers/async: Async annotation framework for folly/fibers
--------
This library provides syntactic annotations to improve developer experience while using `folly/fibers` by make fiber-concurrency explicit.

This library is designed for projects *already* using `folly/fibers`. If you are looking for a new asynchronous framework, try `folly::coro` instead.

## Why use async annotations?
Fibers are designed to behave like threads, but are more lightweight using user-mode switching. `folly/fibers` is an implementation of fibers that integrates with folly's executors and futures. Fibers are switched out when they encounter blocking operations.

However these blocking operations are unobvious and implicit in code, and therefore it can be difficult to determine if a given function can block without carefully examining all its children. Note that where a blocking thread simply waits, inefficient, potentially deadlock prone, but predictable, a blocking fiber switches to another fiber - this introduces invisible concurrency in the system.

This makes development error prone and produces bugs:
- Codepaths can silently switch to the main thread stack, making code expected to behave as multiple threads to end up behaving as one when it blocks.
- Blocking functions run iteratively in a for loop (performing I/Os sequentially vs fanning out)

These async annotations for fibers library were designed for two main use cases:

### Improve developer experience for performance sensitive applications that must remain on fiber
The annotations:
- Provide the familiar `Async/await` syntax.
- Explicitly mark functions that can block.
- Enforce (in debug mode) against running blocking functions on the main thread stack (the "main context") potentially leading to unexpected, implicit thread-blocking I/O.
- Help developers (even unfamiliar with the codebase) avoid accidentally scheduling concurrent I/O operations in apparently serial code (for example: await in for-loop).
- Enforce clear separation of I/O and non-I/O code paths, avoiding painful tech-debts.

### Helping migrate to folly::coro
Annotations are a safe and 0 cost intermediate step in a migration to `folly::coro`:
- Async codepaths are already identified and explicit
- Required separation of I/O and non-I/O code paths is already done
- Enforce (in debug mode) against calling (Async annotated) fibers aware code on coro context (implicitly block the thread)
- Conversion from `Async/await` to `folly::coro` can be automated, at least partially (beware eager-return, references...)

### Limitations
- All blocking operations need to be identified manually. No protection is provided for unannotated functions
- Annotations do not protect against fiber stack overflows. However, they can make it easier to decide whether it is safe to run code on main context.

## Overview
This library provides two main syntactic primitives: `Async` and `await`. To declare that a function can block (and therefore must be run on fiber), its return type *must* be annotated with `Async<>`. `Async` is a simple zero cost wrapper, that must be unpacked through a call to `await`. `await` *must* be called only from fiber context (runtime enforced only in debug builds). This ensures that all annotated functions are executed on fiber, and are not executed on main-context

The library also provides APIs to convert most blocking operations encountered in fiber into `Async<>` returning function calls:
- `promiseWait` to block on generic async callbacks. (See `Promise.h`)
- `baton_wait` etc to block on `fibers::Baton`. (see `Baton.h`)
- `futureWait` to wait for a `Future/SemiFuture` to become ready and execute deferred work. (see `Future.h`)
- `taskWait` to execute block on a `coro::Task`. (see `Task.h`)

Next, the library provides APIs to fan-out (see `Collect.h`) and to schedule annotated functions onto the FiberManager (see `FiberManager.h`).

Finally, the library provides a handy utility to execute annotated functions on a FiberManager without the boilerplate (see `WaitUtils.h`)

## Basic example
Vanilla fibers code:
```cpp
using namespace folly;

...
constexpr auto kWaitTime = std::chrono::seconds{1};

auto blockingOperation1 = [&] {
  fibers::Baton b;
  b.try_wait_for(kWaitTime);
};

auto blockingOperation2 = [&] { futures::sleep(kWaitTime).get(); };

auto blockingOperation3 = [&] {
  coro::blockingWait(coro::co_invoke(
      [&]() -> coro::Task<void> { co_await coro::sleep(kWaitTime); }));
};

EventBase evb;
fibers::getFiberManager(evb)
    .addTaskFuture([&] {
      std::vector<Function<void()>> tasks;
      tasks.emplace_back(blockingOperation1);
      tasks.emplace_back(blockingOperation2);
      tasks.emplace_back(blockingOperation3);
      fibers::collectAll(tasks.begin(), tasks.end());
    })
    .getVia(&evb);
```

Annotated fibers code:
```cpp
using namespace folly;

constexpr auto kWaitTime = std::chrono::seconds{1};

auto blockingOperation1 = [&]() -> fibers::async::Async<bool> {
  fibers::Baton b;
  return fibers::async::baton_try_wait_for(b, kWaitTime);
};

auto blockingOperation2 = [&]() -> fibers::async::Async<Unit> {
  return fibers::async::futureWait(futures::sleep(kWaitTime));
};

auto blockingOperation3 = [&]() -> fibers::async::Async<Unit> {
  return fibers::async::taskWait(coro::co_invoke([&]() -> coro::Task<Unit> {
    co_await coro::sleep(kWaitTime);
    co_return{};
  }));
};

fibers::async::executeOnFiberAndWait([&]() -> fibers::async::Async<void> {
  fibers::async::await(fibers::async::collectAll(
      blockingOperation1, blockingOperation2, blockingOperation3));
  return {};
});
```

## Annotating an entire codebase
To ensure that all functions that can block are annotated, the following workflow is recommended:
- Identify blocking operations in your code (`future.get()` etc) and use the APIs provided by the library to translate them into annotated function calls. Use `await` to unwrap the result of the function call, and annotate the current function to return `Async<>`.
- Implement static analysis / linters that will force you annotate any function calling `await` to itself return `Async<>`.
- It can be difficult to migrate the whole codebase in a single change, so use `init_await` instead of `await` to unpack the result of the functions annotated in the current code-change. Static analysis can be used to ensure that a function calling `init_await` should not be annotated.
- Use the provided APIs to fan-out (`collectFibers` etc) or schedule more work onto the `FiberManager` (`addFiber`). These APIS require annotated functors as input.
- Replace any `init_await`s with `await`s and continue annotating higher up the stack. At the end of your migration there should be no `init_await`s left in your code.
- Finally, use `executeOnFiberAndWait` as an entry point into fiber code to ensure that even the top of the stack is annotated.
