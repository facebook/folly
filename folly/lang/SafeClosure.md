# `safe_closure`

`safe_closure` creates a callable that stores arguments by value and can be
invoked multiple times. It's like Python's `functools.partial` but with
compile-time safety guarantees for use with `safe_alias`-aware APIs.

## Basic Usage

```cpp
#include <folly/lang/SafeClosure.h>

namespace bind = folly::bind;
using folly::safe_closure;

int add(int a, int b, int c) { return a + b + c; }

// Bind the first two arguments of `add`
auto fn = safe_closure(bind::args{10, 20}, add);
// Sum the already-captured 10 & 20 with a newly-provided 5.
// Return 35 = add(10, 20, 5)
int result = fn(5);
```

## Argument Storage

Arguments are always stored by value in the closure. You control how they're
stored using standard C++ semantics:

```cpp
std::string s = "hello";
auto fn = safe_closure(
    bind::args{
        s,                     // copy s into closure
        std::move(s),          // move s into closure
        std::string{"world"}}, // construct and move prvalue
    // These all deduce to `const std::string&` -- see the next section
    [](auto& a, auto& b, auto& c) { return a + " " + b + " " + c; });
```

For truly in-place construction, use `bind::in_place`:

```cpp
auto fn = safe_closure(
    bind::args{bind::in_place<std::string>, "hello"},
    [](auto& s) { return s.size(); });
```

## Argument Binding

`safe_closure` defaults to binding by `const&`. The example lambdas' arguments
use `auto&` for brevity, but they get deduced as `const T&` anyway. It is fine
to write `const auto&` if you prefer. But, declaring those arguments as
`std::string&` would not compile without `bind::mut`.

Use `bind::` verbs to control how stored arguments are passed to your function:

```cpp
std::string s = "test";
auto fn = safe_closure(
    bind::args{
        s,                         // pass by const reference (default)
        bind::mut{s},              // pass by mutable reference
        bind::copy{s},             // pass by value (decay-copy)
        bind::move{std::move(s)}}, // pass by rvalue reference
    [](/*const*/ auto& a, auto& b, auto c, auto&& d) {
      // a: const std::string&
      // b: std::string&
      // c: std::string
      // d: std::string&&
    });
```

## Invocation Qualifiers

The closure can be called with different qualifiers:

```cpp
auto fn = safe_closure(bind::args{42}, [](int x) { return x * 2; });

fn(0);                // & qualifier - can call multiple times
std::as_const(fn)(0); // const& qualifier - read-only access
std::move(fn)(0);     // && qualifier - single-use, destructive
```

**Important:** Using `bind::move` & `bind::copy` restricts available qualifiers:
- `bind::copy` disables `&&` invocation
- `bind::move` disables `&` and `const&` invocation (only `&&` works)

## vs `async_closure`

| `safe_closure`           | `async_closure` |
|--------------------------|-----------------|
| Returns a callable       | Returns an awaitable |
| Can be called repeatedly | Single-use only |
| Stores regular types     | Offers `bind::capture` for async RAII |
| Runs synchronously       | An asynchronous coroutine |

## Safety & `coro::capture<>`s

`safe_closure` only accepts `SafeAlias.h`-safe callables and arguments. Unsafe
inputs cause compile errors. The returned closure's safety level is determined
by the minimum safety of its function and stored arguments.

```cpp
// ✓ Safe: function pointer + safe arguments
auto safe_fn = safe_closure(bind::args{42}, &some_function);

// ✗ Compile error: lambda with captures is unsafe
int y = 5;
auto unsafe_fn = safe_closure(bind::args{37}, [&](int x){ return x + y; });
```

### Real-life usage in async code

You can pass `coro::capture<>`s to `safe_closure` to take *references* to data
stored by `async_closure`s or `async_object`s. Thanks to compile-time checks,
this doesn't introduce lifetime safety risks.

A canonical use-case is collecting results via a `safe_async_scope`:

```cpp
namespace bind = folly::bind;
namespace coro = folly::coro;
namespace collect = folly::coro::collect;

value_task<Out> computeAnswerForIndex(size_t i) { /*...*/ }

size_t n = 10;
std::vector<Out> output(n, 0);
co_await coro::async_now_closure(
    bind::args{n, bind::capture_mut_ref(output), coro::safe_async_scope()},
    [](size_t n, auto out, auto scope) -> coro::closure_task<> {
      for (size_t i = 0; i < n; ++i) {
        coro::spawn(
            collect::redirect(
                computeAnswerForIndex(i),
                collect::log_and_ignore_non_values >>=
                collect::fn_result_sink{folly::safe_closure(
                    bind::args{out, i},
                    [](auto out2, size_t i2, value_only_result<int>&& r) {
                      (*out2)[i2] = std::move(r).value_only();
                    })}),
            scope);
      }
      co_return;
    });
```

At a high level, this plumbs `capture<vector<Out>&>` into the innermost
`safe_closure`, which ultimately stores computed values. This repeats a typical
algorithm from the legacy `folly/coro/Collect.h`, with some remarkable
differences:
  - This pattern is completely flexible -- you can change the output container,
    the task-spawning pattern, the locally available values & references, the
    cancellation or executor policy for the tasks, all without needing to invent
    a new "coro algorithm".
  - Thanks to the `collect::` machinery, the async scope only needs to manage
    `void`, noexcept-awaitable completions. This avoids log-but-only-sometimes,
    and crash-but-only-sometimes kludges of the legacy `coro::AsyncScope`.
  - The entire computation properly handles `safe_alias` annotations, which
    means that you *can* take `coro::capture` references inside scope-spawned
    tasks without risking lifetime safety bugs. And if you accidentally take an
    unsafe ref, the compile will fail.
  - `async_now_closure` provides async RAII, so the whole thing is also
    exception-safe, and handles "whether to cancel outstanding scope work on
    exception" simply & naturally in the API.

The value of `safe_closure` is that it makes it simple to set the destination
for the value computed on the scope -- or even a transformation of the value.
`collect::map_result` is the counterpart to `fn_result_sink` that also takes a
callable, but goes in the middle of `redirect` chains.

## Future Work

  - Add a linter that checks that `bind::` qualifiers in the `safe_closure`
    agree with the signature of the callable.  For example, it's a likely
    performance bug to bind args by reference to an argument declared `auto` --
    this will incur an unnecessary copy.  Or, if you use `bind::copy`, it is
    unlikely that you meant to bind it to an `&` or `const&` arg.

  - Plain lambdas-with-ref-captures are unsafe-and-movable.  We could later
    generalise `coro/AwaitImmediately.h` to `must_use_immediately_v`, and define
    an unsafe-but-immovable `safe_now_closure`. This isn't very useful on its
    own, but it would simplify generic code.
