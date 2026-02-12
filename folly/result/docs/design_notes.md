# `result` design notes

This is for folks who thoroughly read `README.md` and `result.md`, and want
to understand **why** the overall design is this way.

## Why `result` instead of \<*alternative*\>?

`result` addresses the high-level problem that some codebases want to avoid
exceptions...  while existing error-handling approaches tend to be verbose,
complex, incompatible (all-or-nothing), and/or brittle.  The preface to Barry
Revzin's [P2561](https://wg21.link/P2561) speaks more to the issue.

### Exceptions are imperfect & inevitable

`result` does not say you must avoid exceptions. It deliberately interoperates
with them, while giving you a viable alternative, where needed.

C++ is built around exceptions. Using them has important upsides:
  - Many `std::` APIs throw. It is the most common error-handling paradigm.
  - Most code is "transparent" to exceptions -- they don't add verbosity.
  - They are structured -- you can throw classes that include rich data.

Simultaneously, exceptions have considerable downsides:

  - Programmers are liable to forget their subroutines can fail.  This can have
    catastrophic consequences when a program fails to use RAII for mandatory
    cleanup. This problem is especially bad when you must `co_await` the
    cleanup, which is incompatible with RAII.

    Absent catastrophic bugs, forgetfulness can cause the errors to be caught at
    an excessively high-level error boundary, degrading reliability.

  - Writing exception-safe C++ is tricky, testing for exceptions is tedious, so
    the net result is that typical code has many exception-safety bugs. Patterns
    for the "basic" exception-safety guarantee (scope guards, RAII, composition
    of safe components) simply need to be learned & enforced in code review --
    this alone creates ongoing reliability risks.  Furthermore, the number of
    advanced topics that programmers need to concern themselves with is [quite
    high](https://isocpp.org/wiki/faq/exceptions).  Some highlights:

      * Synchronization patterns are not always trivially representable
        via RAII.  Any manual handling of mutex locks, condition variables, and
        batons is liable to cause deadlocks on unhandled exceptions.
      * Constructor exception safety has [considerable fine print](
        https://isocpp.org/wiki/faq/exceptions#ctors-can-throw).
      * Code called from destructors requires [extreme care](
        https://isocpp.org/wiki/faq/exceptions#dtors-shouldnt-throw) --
        unless everything you call is guaranteed `noexcept`.

  - Throwing is slow, over 1usec per `ExceptionWrapperBenchmark.cpp`.  This can
    really ruin your day in a latency-sensitive application.

### Why not use `expected<T, std::exception_ptr>` directly?

The precursor to `result` started out as a template alias to `folly::Expected`
(the ancestor of `std::expected`). The folly version is already usable as a
short-circuiting coroutine (`co_await` propagates errors).

With tens of thousands of lines of code using this idiom, a few deficiencies
became clear. Many of these would be expensive or impossible to address with
`folly::Expected` coroutines, so we went for a dedicated type. While the overall
behavior is similar, there are many valuable improvements:

  - **Much better usability:**
    * `res.value_or_throw()` rethrows the actual exception instead of a
      non-debuggable `BadExpectedAccess`.
    * `folly::get_exception<Ex>(result)` makes error-checking easy.
    * `result` supports epitaphs / provenance, which lets users capture
      error-propagation stacks (details in `epitaphs.md`).
    * Full integration with `folly::coro`, so that `result` can supersede `Try`
      in that usage.
    * `co_await expectedFn()` is easily confused with async `folly::coro` code,
      while `co_await or_unwind(resultFn())` is not.

  - **Automatic exception boundary**: Since an error-state `result` can store
    `std::exception_ptr`, a `result` coroutine can implicitly wrap and return
    unhandled exceptions.

  - **Reference semantics, fewer copies**: The `folly::Expected<>`
    short-circuiting coroutine only implements await-as-value.  As a
    consequence, we saw many unwanted copies crop up, enough to impact perf in
    some cases.  `result` addresses this via (i) almost no implicit copying,
    (ii) full support for reference types (`result<T&>`, `result<T&&>`).

  - **`[[nodiscard]]`** on `result` forces error checking.  Theoretically, an
    `Expected` template alias could perhaps achieve the same result by extending
    compilers & the standard to [allow marking template aliases
    `[[nodiscard]]`](https://github.com/llvm/llvm-project/issues/68456).

  - **Other API polish**:
    * More API surfaces are explicitly non-throwing.
    * `result`'s API hides the empty-by-exception (mis)behavior of
      `folly::Expected` well enough that it can be truly guaranteed nonempty as
      soon as `std::expected` is available.

### When to consider `expected<T, std::error_code>` or `boost::outcome`?

Of course, `result`'s jack-of-all-trades error-handling paradigm may not be what
you want -- sometimes you must prioritize speed, maximize legacy-code
compatibility, or avoid heap allocations on the error path.

For such cases, consider:
  - `expected<T, std::error_code>`. Per above, `folly::Expected` may be
    preferable to `std::expected`, since its `co_await` can propagate errors.
  - `boost::outcome::result<T>` -- much like `expected<T, std::error_code>`.
  - `boost::outcome::outcome<T, std::error_code, std::exception_ptr>` -- a rough
     analog of `folly::result`, but with higher UX complexity, and higher
     customizability. While `folly::result` tries to be good for everyone by
     default (to be a useful standard for a large codebase), `outcome` lets you
     tune behaviors extensively.
  - Some mappings from `folly::result` to `boost::outcome`:
       - `BOOST...TRY` macros provide short-circuiting operations similar to
         `co_await folly::or_unwind()`. The authors are [against co-opting
         `co_await` for short-circuiting](
          https://www.boost.org/doc/libs/latest/libs/outcome/doc/html/tutorial/essential/coroutines/co_await.html)
          meaning that syntax sugar is blocked on the standardization of
          [P2561](https://wg21.link/P2561) or similar.
       - `boost::outcome` synchronous coroutines (`lazy<T>`, `eager<T>`,
         `generator<T>`, etc) have special behavior when `T` is an
         `outcome::result` or `outcome::outcome`. In contrast, `folly::coro`
         async coroutines like `now_task<T>` can always be awaited as
         `folly::result<T>` -- so `some_task<result<T>>` is redundant.

### Why not extend `folly::Try` instead?

A few reasons, in order of importance:

  - **Tri-state is a poor fit for short-circuiting coroutines**: Unlike
    `expected`, `Try` has three states: default-empty, value, error.  Most
    business logic should only handle "value or error". The need to handle empty
    states adds cognitive burden, as well as boilerplate. The boilerplate
    shrinks if `co_await` for `Try` short-circuits both empty and error states,
    but that makes it easy to forget to handle empty states in code that
    actually differentiates them. For example, there's no longer a meaningful
    catch-all -- `get_exception<std::exception>(emptyTry)` would have to return
    null.  A robust API for a tri-state would also have to make it explicit
    whether it short-circuits on empty:
    ```cpp
    co_await or_unwind_or_empty(...) // errors unwind, `std::optional<T>`
    co_await or_unwind(...) // both errors and empty unwind, `T`
    ```
    While this could be made to work, it is a less compelling offering.

  - **Better API**: As a clean-slate design for coroutine plumbing, `result`
    gets API enhancements that are prohibitive to make in `Try`:
    * Implicitly-throwing `value()` was renamed to `value_or_throw()`, and
      operators `*` / `->` were omitted. This encourages explicit, non-throwing
      error handling and/or `or_unwind` usage.
    * `error_or_stopped()` + `has_stopped()` instead of `exception()` encourages
      [C++26-aligned](https://wg21.link/P2300) separation of "stopped" and
      "error" error handling.

      Typical code isn't made verbose -- test `result`s for errors via
      `get_exception<Ex>()`; in coroutines, obtain `result`s via
      `co_await value_or_error()` or `value_or_error_or_stopped()`.
    * `Try` lacks useful implicit conversions from its value type -- and prior
      debates settled on **not** adding them.
    * These *could* maybe be backported to `Try`, but `result` already has them:
       - Support for reference types.
       - Fallible implicit conversions, enabling range-`for` loops over `result`
         generators.

  - **Two-state aligns with `std::expected`**: `result`'s two-state semantics
    should, long-term, be less surprising since `expected` is standard C++23 and
    is guaranteed nonempty.

### Don't `folly::coro` tasks provide the same functionality?

Technically, folly async tasks have long supported return-oriented,
short-circuiting error handling, in addition to their default throwing style:

```cpp
// Get value, or propagate error / stopped
auto v = co_await co_nothrow(asyncMightFail());
```

Before `result`, correct non-throwing error handling looked like this:

```cpp
// Test for value / error / stopped / empty (can happen in buggy code!)
auto t = co_await co_awaitTry(asyncMightFail());
if (t.hasValue()) {
  // handle t.value()
} else if (!t.hasException()) {
  // somehow handle empty `Try` -- DFATAL?
} else if (auto* ex = t.tryGetExceptionObject<MyError>()) {
  // NEVER handle `std::exception` since that interrupts cancellation!
  LOG(ERROR) << ex->what();
} else {
  co_yield co_error(t.exception()); // propagate unhandled or stopped
}
```

This is both verbose and hard to get right, so most `folly::coro` users write
throwing code with `try`-`catch` exception boundaries as needed.

You can now use `result` to clean up non-throwing async error handling:

```cpp
auto r = co_await value_or_error(asyncMightFail());
// Handling `std::exception` won't interrupt cancellation
if (auto ex = get_exception<MyError>(r)) { // a rich ptr, NOT `auto*`
  LOG(ERROR) << ex; // don't write `->what()` here!
} else {
  auto& v = co_await or_unwind(r);
  // handle `v`
}
```

While this works fine, you should prefer `result` coroutines + `or_unwind` for
synchronous code with pervasive error handling -- they are simpler and cheaper.
  - As suspending coroutines, `folly::coro` tasks will likely never optimize as
    well as short-circuiting coroutines, which only suspend on destruction.
    Also, `folly::coro` plumbing is inherently more complex in order to support
    asynchrony (executors, cancellation tokens, etc).
  - It may be some time before `folly::coro` has native support for adding
    epitaphs to `result`s in error-or-stopped states (`epitaphs.md`) --
    currently, round-tripping through `Try` discards epitaphs.

## Details of the `result` API

If you're wondering "why does `result` do \<*specific thing*\>?", and cannot
find it in a code comment, the rationale may be documented here.

### Why does `result<V&>` have deep-const copy semantics?

`result` is movable iff `T` is, and copyable iff `T` is.  The exception is
reference types: `result<V&&>` is move-only (following
`rvalue_reference_wrapper`), and `result<V&>` has a "deep const" restriction.

Since `const result<V&>` only exposes `const V&` (see next section), copying
`result<V&>` from `const result<V&>&` would silently grant mutable `V&` access
through what was behind a const barrier.  To prevent this, `result<V&>` is only
copyable from a mutable source (`result<V&>&`), not from `const result<V&>&`.
`result<const V&>` is fully copyable, since the inner `const` cannot be lost.
See also `DefineMovableDeepConstLrefCopyable.h` for the general pattern.

### Why does `const result<Ref>` propagate `const`?

When you access the contents of `const result<V&>`, you get `const V&`,
not `V&`. This differs from `std::reference_wrapper`, which does NOT
propagate const.

This choice was made because this sort of code is common:

```cpp
const auto& r = obj.foo();  // returns `result<V&>`
auto& v = r.value_or_throw();  // Without const propagation: Bug! `v` is mutable
```

If `foo()` returns `result<V&>`, the user may expect `r` to be deeply const.
But, without const propagation, they'd get mutable access to the referenced `V`
through what looks like a const reference -- a const-correctness bug.

On the flip-side, propagating `const` is relatively straightforward, and has
few negative UX consequences.  The main limitation is that you can only copy a
`result<T&>` from a **mutable** reference to `result<T&>` (because the copy
would otherwise grant mutable access that wasn't originally available).

If const-propagation isn't what you want, store `result<V*>` or
`result<std::reference_wrapper<V>>` / `rvalue_reference_wrapper` instead.

### Why store `std::exception_ptr` instead of error codes?

Wouldn't modeling errors as integer codes, like `std::error_code`, be 🚀
*obviously faster*? Actually... `result` does not give up much speed, and gains
a lot of flexibility.

  - **Compatible:** Storing type-erased exceptions interoperates with
    exception-based code (throwing & `folly::Try`, synchronous & `folly::coro`).

  - **Supports codes:** Per `rich_error_code.md`, `result` provides < 5ns
    RTTI-free retrieval of coded errors, despite the type erasure. Use
    `errc_rich_error.h`, `coded_rich_error.h` or `nestable_coded_rich_error.h`
    to construct coded errors, and test them via `get_rich_error_code<>()`.

  - **Small:** The model of `rich_error_code` is deliberately simpler than
    `std::error_code` -- its category / domain for type erasure is the
    *exception type* itself, meaning that we don't have to store an extra
    category pointer. Today's 64-bit `result<uintptr_t>` is 16 bytes -- just
    like `std::error_code` alone. If minimizing `result` size is of paramount
    importance, see `future_small_value.md` for an optimization idea.

  - **Fast enough:** Using `error_or_stopped{Error{}}`, construct-destruct costs
    ~60ns. This is usually fast enough. If not, `immortal_rich_error<Error>` has
    construct-destruct costs of under 5ns. Calling `get_exception<Error>()` on
    both kinds of errors takes ~5ns thanks to a no-RTTI optimization -- we may
    later extend this per `future_fast_rtti.md`.

  - **Error provenance:** Our type-erasure also powers `epitaphs.md`.

### Why does `result` distinguish "stopped" and "error"?

`result` exposes `error_or_stopped()`.  This distinguishes with "stopped" (for
cancellation) from "error" via `has_stopped()` and `stopped_result`.  This
aligns with [P1677]( https://wg21.link/p1677) and
[C++26/P2300](https://wg21.link/p2300) `std::execution`, which hold that
cancellation is **not** an error.  Instead, we learned that "stopped" is early,
serendipitous success that typically only requires quick RAII cleanup --
**not** handling.

Today's `folly::coro` treats cancellation as an exception
(`OperationCancelled`), which has several problems:

  - `catch (...)` and `catch (const std::exception&)` accidentally swallow
    cancellation.
  - `co_awaitTry` returns cancellation as an error, requiring explicit checks.

This causes many subtle bugs where user code inadvertently interrupts
cancellation propagation.

By making "stopped" a distinct state, `result` encourages correct handling:
  - In async coros, `co_await value_or_error()` automatically propagates
    cancellation, producing a `result` that is never stopped.
  - If you have a `result` from another source (`value_or_error_or_stopped()` or
    `try_to_result()`):
      - Normal checks like `get_exception<UserEx>()` cannot match stopped.
      - Testing for `OperationCancelled` is a compile error, users are told to
        call `has_stopped()` explicitly.
      - The one error-handling pattern that may interrupt cancellation is
        `get_exception<std::exception>()`. A migration is planned to fix this,
        but will take time.

See the `folly/OperationCancelled.h` docblock for example code.
