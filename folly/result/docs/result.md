# `folly::result<T>`: error/value container & short-circuiting coroutine

This doc describes when, and how, to use the `result` class template for
error-handling in C++ programs.

This doc assumes familiarity with C++ exceptions.  It is helpful -- but
completely optional -- to have used other similar types, like `folly::Try` or
`Expected`, Rust's [`Result`](https://doc.rust-lang.org/std/result/), Haskell's
[MonadError](https://hackage.haskell.org/package/mtl-2.2.2/docs/Control-Monad-Except.html),
or Niall Douglas's C++ [`boost::outcome`
](https://www.boost.org/doc/libs/1_87_0/libs/outcome/doc/html/index.html).

Folly veterans can think of `result` as an improved `Try`, with a smooth enough
user experience to use as your main error-handling pattern in synchronous code.
For async `folly::coro` code, `co_await_result(fn())` / `co_ready(result)` /
`co_result(result)` supersede `co_awaitTry(fn())` and `co_result(Try)`.

The intent of `result` is most similar to `boost::outcome`, and indeed its
[introduction](https://www.boost.org/doc/libs/1_87_0/libs/outcome/doc/html/index.html
) describes many use-cases where `result` would also be appropriate. Unlike the
C++11 `boost::outcome`, `folly::result` is a C++20 design, making it simpler &
more usable:

  - `result` coroutines support easy error propagation via `co_await`, and
    provide automatic exception boundaries. Credit: The original [C++
    short-circuiting coroutine](
    https://github.com/toby-allsopp/coroutine_monad), later reimplemented in
    [`folly::Expected`](
    https://github.com/facebook/folly/commit/0e8c7e1c97e27d96fddbdf552bc99faa22066d00).

  - `result` uses `std::exception_ptr` (with some folly-specific enhancements)
    to efficiently transport all exception types, which avoids the user-facing
    complexity of distinguishing `outcome::result` (`error_code` only) and
    `outcome::outcome` (code OR exception).

  - `result` needs no macros, and has a much easier-to-learn API.

Although `boost::outcome` enumerates a litany of use-cases, we will focus on two
particular scenarios that are common in Meta's code.

## A tale of two use-cases

### Use-case 1: Explicit error propagation in synchronous code

Exceptions in C++ are common, unavoidable, useful, and a frequent source of bugs
(`DesignNotes.md` has more in "Exceptions are imperfect & inevitable"). However,
some circumstances -- such as low-level systems programs, high-performance, or
high-reliability applications -- can benefit from policies along these lines:
  - never throw exceptions,
  - never let thrown exceptions escape a function,
  - handle thrown exceptions as locally as possible, etc.

`result` can reduce the effort of following such policies, and improve adherence.

An extreme of this approach is exemplified by the Linux kernel, a C-language
mega-project focused on reliability and performance.  Linux handles errors
primarily by one convention -- functions return an integer that encodes success
or error.  This approach is vulnerable to "forgetting to check the return
value", but in practice this **local** requirement is easily enforced (lint all
functions to be "nodiscard").  On the other hand, exception propagation is
inherently non-local, making it much easier to forget to draw appropriate error
boundaries.

Using `result` gives you the best of both worlds.  It does not eliminate
exceptions from C++ -- that ship has long sailed.  Instead, critical portions
of the code can be made into `result` coroutines.  These are safer & faster
than thrown exceptions, and more flexible than error codes:
  - `result` in an error state transports an `exception_ptr`, giving perfect
    interoperability with thrown exceptions and `folly::Try`.
  - You cannot forget to handle returned errors -- `result<T>` is nodiscard,
    and does not implicitly convert to `T`.  You **have** to unpack it.
  - The standard pattern for unpacking the value is `co_await myResult`, which
    also **visibly** propagates unhandled exceptions to the caller.
  - Handling specific exceptions via `if (auto* ex = get_exception<Ex>(res))` is
    as clear as `try-catch`, and lacks the many gotchas of the
    exception-unwinding context.
  - You only need one error path -- `result` coroutines capture all internally
    thrown exceptions and pack them into the return value, so the caller can
    assume that the function won't throw (besides `std::bad_alloc` or move/copy
    constructors of its arguments).
  - Propagating errors via `result` costs O(1nsec), vs ~1µsec for `throw`.
  - `result` also propagates `folly::coro` cancellation -- and exposes it as a
    separate "stopped" state. The API is carefully designed not to add
    complexity in writing code that is transparent to cancellation. It is also
    forward-compatible with C++26 / P1677 / P2300.

This `result` coroutine showcases some common error-handling patterns:

```cpp
result<size_t> countGrapefruitSeeds() {
  // If `getFruitBox()` returns an "error" or a "stopped" state, then
  // `co_await` immediately propagates that result to the caller!
  auto box = co_await getFruitBox();
  auto boxRes = box.findFruit(FruitTypes::GRAPEFRUIT);
  if (auto* ex = get_exception<RottenFruit>(boxRes)) {
    logDiscardedGrapefruit(ex);
    return 0;
  }
  const auto& grapefruit = co_await std::cref(boxRes);
  size_t numSeeds = 0;
  // `fetchSegments()` does **not** return `result<>` as a demo -- any
  // exceptions it throws are caught and captured in our return value.
  auto segments = grapefruit.fetchSegments();
  for (auto& segment : segments) {
    numSeeds += (co_await segment.seeds()).size();
  }
  co_return numSeeds;
}
```

`result` coroutines also interoperate with async `folly::coro` coroutines:

```cpp
auto val = co_await coro::co_ready(someResult());
```

Before diving in, you might review the `result` contract, best practices, and
"how-to"s below.

### Use-case 2: Exceptions without `throw` overhead in `folly::coro`

`result` brings some minor improvements over the `Try` solution to the same
problem (pre-2025). Most importantly, it helps us migrate to using
`has_stopped()` tests for cancellation, instead of the legacy pattern of
"cancellation is an exception".

Continue to use `co_nothrow` to pass **all** child exceptions to the parent
without re-throwing:

```cpp
co_await coro::co_nothrow(childTask())
```

Where handling exceptions is required, `result` can replace `Try`, with
slightly better ergonomics:

```cpp
result<int> intRes = co_await coro::co_await_result(taskReturningInt());
if (auto* ex = get_exception<MyError>(intRes)) {
  /* handle ex */
} else {
  // Efficiently propagates unhandled errors
  sum += co_await coro::co_ready(std::move(intRes));
}
```

If you wanted to only handle specific errors, but not the value (like in a
generic retry functor), then `co_yield co_result(std::move(intRes))` will
propagate any value *or* unhandled error to the parent of the current coro.

## A summary of `result`'s contract

`result` is a bit opinionated, aiming to popularize several reliability &
efficiency best practices. Reading this contract will help you understand the
design principles.  If you want to know **why** `result` was designed this way,
check out `DesignNotes.md`.  For example, they offer comparisons with `folly`
prior art, like `Expected`, `Try`, and `coro::Task`.

In bullets, `result<T>`:

  - Contains one of:
    * `T` -- which can be a value or reference, or
    * `non_value_result` -- which either `has_stopped()`, or stores an error as
      `std::exception_ptr`, with folly-specific optimizations.  Access the
      latter via `folly::get_exception<Ex>(res)`.

    *NB*: Right now, `result` is "almost never empty, like `folly::Expected`",
    but when C++23 is widely available, it will be truly never empty.

  - Provides constructors optimized for usability. For example, unlike
    `folly::Try`, `result` is implicitly constructible from values & errors.

    *NB*: Storing an empty `std::exception_ptr` is a contract violation.  It is
    fatal in debug builds, but "works" in opt unless you hit a later assertion,
    like the `std::terminate` in `exception_wrapper::throw_exception`.

  - Is move-only, unlike `Expected` and `Try`. The benefit is that the `result`
    plumbing tax stays low, avoiding both user data copies, and
    `std::exception_ptr` atomic ops.
      * For usability, copy conversion from cheap types (like `int`) is allowed.
      * Explicit `result::copy()` is rarely needed, since `return` and
        `co_return` are "implicit move contexts".
      * With named `result<T> r`, you will need to explicitly `co_await` one of
        `std::move(r)` / `std::cref(r)` / `std::ref(r)` / `folly::rref(r)`.

  - Is `[[nodiscard]]`, meaning that in common usage you cannot forget to handle
    an error.  A line with just `resultFoo()` will not compile, you need
    `co_await resultFoo()`.

  - Integrates with `coro::Task` (or safer `NowTask` / `ValueTask`) via
      * `v = co_await co_ready(res)` -- get `res`'s value, propagating errors.
      * `r = co_await co_await_result(someCoro())` -- await without throwing,
        though if you won't handle *any* errors, keep using `co_nothrow`.
      * `co_yield co_result(res)` propagate a value or error to your awaiter.

  - Supports migration from `Try` via `try_to_result` and `result_to_try`.

  - Uses automatic storage duration for `T`.  Allocation caveats:
      * `std::exception_ptr` stores the exception on the heap.
      * While `result` coroutines are set up to be HALO-friendly, a compiler is
        not *obligated* to allocate `result` coroutines on the stack. Profile
        first, then read "how to avoid coro frame allocations" below.

What to know about exceptions & `result`:

  - `result` coroutines (but **not** functions) are exception boundaries.
    Any uncaught exception is captured in `res.non_value()` & returned.

  - The `result` API avoids throwing, aside from:
      * `value_or_throw()`, which you should avoid in favor of `co_await`,
      * `std::bad_alloc`, which is unavoidable due to `std::exception_ptr`
        and coroutines.

  - If you need truly non-throwing code, wrap it with `result_catch_all`.

  - `result` coroutines (functions containing `co_await`, `co_return`, or
    `co_yield` ) should **NOT** be declared `noexcept` -- that will not do [what
    you expect](
    https://devblogs.microsoft.com/oldnewthing/20210426-00/?p=105153).
      * In particular, if you mark a `result` coro `noexcept`, calling it may
        `std::terminate` if an argument's copy/move constructors throws, or if
        HALO fails (it shouldn't, but compilers make no guarantees) and you hit
        `std::bad_alloc`. Those are not useful behaviors!
      * You may, of course, write a non-coroutine function that `return`s a
        `result` and is `noexcept`, but this should not be typical usage.

## `result` best practices

### Avoid `Task<result<T>>`

`coro::Task` coroutines (or safer `NowTask`, `ValueTask`, etc) should **not**
wrap `result`, because tasks are already capable of non-throwing error
propagation. Instead, read "how to interoperate with `Task`" below.

### Prefer `result` coroutines over non-coroutine `result` functions

This recommendation is *primarily* about consistency of expectations -- `result`
coroutines are more likely to follow a policy like "not letting exceptions fly",
since each one is an exception boundary. Secondarily, propagating errors via
`co_await subResultFn()` is just easier & cleaner. Compare:

```cpp
// coroutine
result<int> addFive1() { co_return 5 + co_await childFn(); }

// non-coroutine
result<int> addFive2() {
  auto res = childFn();
  if (!res.has_value()) {
    return res.non_value(); // propagate "error" or "stopped"
  }
  return 5 + res.value_or_throw();
}
```

### `value_or_throw()` is *only* for edge-cases

The recommended pattern for `result`-returning code is to **explicitly** return
errors, so the "throw" part is discouraged. It is also bad for performance and
readability:
  - In `result` coroutines, `res.value_or_throw()` is an inefficient & ugly way
    of writing `co_await res`.
  - In `folly::coro` async coroutines, `auto v = co_await co_ready(resultFn())`
    and `co_yield co_error(ew)` are much cheaper than throwing.

`value_or_throw()` **can** be useful when you're writing non-coroutine `result`
functions, which might help hot-path code (profile first!). In those cases, make
the "throw" branch impossible via an explicit `has_value()` check:

```cpp
if (res.has_value()) { auto& val = res.value_or_throw(); }
```

### Avoid `co_return std::move(...)` or `return std::move(...)`

Both are implicitly movable contexts, so the `std::move` is just visual
noise, and can actually prevent NVRO for `return` (there's a linter against it).

You can directly return any of these types: `result<V>`, `V`,
convertible-to-`V`, convertible-to-`result<V>`, `non_value_result`, or
`stopped_result`.  None will incur unnecessary copies.

## How to...

### Handle specific errors

If you want to check for a single error, `folly::get_exception` is all you need:

```cpp
result<> handlesErrors() {
  auto r = propagatesErrors();
  if (auto* ex = get_exception<MyErr>(r)) {
    // handle `ex`
  } else {
    auto v = co_await std::move(r); // propagate unhandled error & stopped
  }
}
```

Chaining these via `} else if (...) {` should be fine for the vast majority of
usage. If not, "Future improvements" mentions the possibility of more efficient
multi-type dispatch.

### Store values in a `result`

The semantics of `result<Value>` are straightforward:
  - You can implicitly move values in & out -- `result r{fn()}`, then
    `co_await std::move(r)`.
  - You can explicitly copy `result` via `r.copy()`.
  - To copy `Value v` into a `result`, use `result r{folly::copy(v)}`. Small
    trivially copyable `Value`s (like `int`) can be copied in implicitly.
  - Some `result<U>`s are implicitly convertible to `result<V>`. See the two
    cases in `result.h` -- "simple" and "fallible".
  - [CTAD](https://en.cppreference.com/w/cpp/language/class_template_argument_deduction
    ) works: `result r{42}` declares a `result<int>`.

### Store references in a `result`

`result` of a reference type is just syntax sugar for a ref wrapper:
  - `result<V&>` stores `std::reference_wrapper<V>`
  - `result<V&&>` stores `folly::rvalue_reference_wrapper<V>`.

    **WATCH OUT**: This opinionated wrapper enforces "use-once" behavior --
    use-after-move literally becomes a segfault.

Reference support enables "fallible getters": `result<Value&> at(const Key&)`.
  - For this to make sense, `co_await m.at(k)` must return `Value&`, even though
    the `result<Value&>` being accessed is an rvalue.
  - `const result<V&>` does not make the reference `const`. This is consistent
    with `result<V*>` and `std::reference_wrapper<V>`.

For explicitness, you must use the corresponding wrapper type to construct
reference `result`s.

```cpp
int n = 5;
result<int&> lv = std::ref(n);
result<const int&> clv = std::cref(n);
result<int&&> rv = folly::rref(std::move(n));
```

### `co_await` by-value and by-reference

Most of the time, you will await a prvalue result, i.e. `co_await resultFunc()`.
This moves the underlying value, or exposes the returned reference.

However, if you have `result<V> r`, then `co_await r` will not compile -- that
would have to copy `V` or `std::exception_ptr`, and the `result` API tries to
make copies explicit.

Instead, you have to choose from:
  - By-value: `co_await std::move(m)`: Returns a moved-out `V`. Error
    propagation is fast.
  - By-reference: `co_await std::cref(m)` / `std::ref(m)`: Returns `const V&` /
    `V&`.  Propagates `std::exception_ptr` by copying (~25ns).

For some `V`, `co_await`ing by-reference can speed up value access, at the
expense of the error path.

On the hot path, choose by profiling.  Off the hot path, those finer points of
performance are negligible, so choose readability:
  - Prefer `co_await std::cref(r)` for read-only access.
  - `co_await std::move(r)` if you need an r-value right away.
  - `co_await std::ref(r)` if you need to mutate the value, or plan to move it
    after using it.

### Interoperate with `coro::Task` (or safer `NowTask` / `ValueTask`)

  - To pass all errors to the parent, use `co_await co_nothrow(childTask())`.
  - Get a task's `result` via `res = co_await co_await_result(childTask())`.
  - Get the value from a `result` via `v = co_await co_ready(std::move(res))` --
    error & stopped states are propagated to the parent. Future: add
    by-reference support via `std::ref` and `std::cref`.
  - In any `Task-like<T>`, `co_yield co_result(res)` cheaply forwards the
    value/error/stopped state of `result<T>` to the caller.

### Interoperate with `folly::Try`

New code should prefer `result`, since it is more flexible (e.g. reference
types), and more ergonomic (short-circuiting coroutines, usable conversions).

To interact with existing `Try` code, use the shims `result_to_try()` and
`try_to_result()`. The latter defaults to encoding empty `Try`s as
`UsingUninitializedTry` errors, though you can customize this by passing
`empty_try_with(fn)` as the second argument.

### Eliminate "no coro frame overhead"

Before you read further, profile to ascertain that you **have** a coro frame
allocation problem. `result` coroutines are designed to be HALO-friendly, so
they should be able to run on-stack. If you're seeing heap allocations, it
likely means we missed a clang "elidable" attribute somewhere.

If you have a compelling reason to avoid coroutines (an old compiler?), you can
write plain functions that return `result<T>`, and the caller won't know the
difference, **as long as you follow the "mostly non-throwing" contract** of
`result` coros. This demonstrates non-coroutine error-handling patterns:

```cpp
result<int> plantSeeds(int n) {
  return result_catch_all([&]() -> result<int> {
    if (n < 0) {
      return non_value_result{std::logic_error{"cannot plant < 0 seeds"}};
    }
    int seedsLeft = n;
    for (int i = 0; i < n; ++i) {}
      auto rh = digHole(i);
      if (auto* ex = get_exception<HitBigRock>(rh)) {
        continue; // skip planting this seed
      } else if (!rh.has_value()) {
        return rh.non_value(); // unhandled error or stopped
      }
      rh.value_or_throw().plantSeed(i);
      --seedsLeft;
    }
    return seedsLeft;
  });
}
```
