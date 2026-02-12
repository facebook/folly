# Future ideas

This document collects *some* of the possible future extensions for `result`
and rich errors.  The most important ideas have their own `docs/future_....md`.

Lacking a dedicated `CONTRIBUTING.md`, here are some principles to keep in mind
when building new features:
  - As much as possible, try to build `constexpr` features, and test them as
    `constexpr`.  There are a few good reasons:
       - C++26 makes `std::exception` work in constant-evaluated code.
       - Writing `constexpr` tests (see `test()` from `common.h`) gives you
         a very strong UB detector -- the consteval interpreter won't even
         compile code that relies on undefined behavior.
       - Since a lot of errors are static, pushing their allocation &
         construction to build-time is good for performance.  For this reason,
         immortal `rich_exception_ptr` is already working in `constexpr` code.
  - Rich errors have a huge test matrix: (packed/separate storage) x
    (owned/immortal/misc errors) x (with-epitaphs/underlying errors) x
    (move/copy/assign/compare/format fundamentals).  To deal with this, current
    rich error unit tests prioritize "testing in depth" -- this means that we
    carefully build up helper functions to exercise a large test matrix.
      - For the bulk of the testing burden, avoid creating a hodge-podge of
        ad-hoc test scenarios.  It should be easy to tell that the whole test
        matrix is covered, without doing extensive book-keeping.
      - **Do** provide "usage example" integration-style tests as well.
  - Get expert review, especially when touching bit-discriminated variant code,
    which is (by necessity) teetering on the edge of "acceptable UB".

These are sorted from "near future" to "far future".

  - I am unsure whether `result<T>` should become `fmt`- and
    `<<(ostream&)`-formattable whenever `T` is.  It's convenient, but it's not
    obvious what convention should disinguish value from non-value output,
    which may just mean it's bad to sugar this.

  - Automatic epitaphs for `result` coroutines as in `epitaphs.md`.
    I'm thinking of symbolizing the stack and attaching it to the exception in
    `unhandled_exception`.  Perf-wise this should be "fine" since `throw` is
    already stupid-expensive.  Update docs, since this is Very Useful.
      * This would likely be implemented as a public helper function, so that
        user `catch` clauses can also do this.  We probably don't want to
        integrate `folly/experimental/exception_tracer`, but the implementation
        is instructive.  Also see https://fburl.com/cpp_debug_only_stack_trace.
      * A further extension would be to also add epitaphs to the error with
        the coroutine stack (see `AsyncStack` code in `folly/coro`).

  - Thoughtfully add `in_place` / `in_place_t` support to the public API as
    appropriate.  When `result` gets it, clean up `TEST(Result, throwingMove)`
    where we reassign `.value`.

  - Make it easy to mark certain epitaphs debug-only.  Ideally it would also
    be easy to toggle some epitaphs at runtime for debugging production
    issues, in the spirit of `VLOG`. Some words here may be useful
    https://fburl.com/epitaph_scopes_fast_or_slow

  - Are we happy with the moved-out behavior of `error_or_stopped`?  Today it's
    "dfatal crash" / empty eptr.

  - Start `result/containers.h` with `result<T&> map_at(Map, Key)` and similar
    functions that mimic throwing `std` patterns, but return `result`.

  - Support a flavor of `get_rich_error_code` returning a `rich_code<Code>`
    that is formattable with epitaphs (provenance).  One API idea is to add
    a `get_rich_error_code<rich_code<Code>>()` overload, another is to just add
    a new verb.

  - `result_generator`: Similar to `std::generator` but yields `result`s, so
    generator errors don't have to throw.  Any error ends the stream.  Supports
    `or_unwind` semantics.  https://fburl.com/result_generator_impl has a draft
    implementation (based on `folly::coro::Generator`); it needs updates.

  - Rich error / result formatters may want to parse out some options to
    customize the output style (separator / indentation).  Another important
    one would be to omit epitaphs (e.g. `checkEptrRoundtrip` wants this).
    Before doing this, make sure the default output style is broadly readable &
    useful -- with time, automation will rely on parsing that, so it will be
    hard to change.

  - `rich_exception_ptr` should also be formattable, but since it's not (very)
    user-visible, this is lower-priority than `result` / `error_or_stopped`..

  - We already have `epitaph.h` and `nestable_coded_rich_error.h`,
    but neither is a direct counterpart to `std::nested_exception`. We
    don't really need anything to support that "intrusive" behavior, current
    users can just implement `next_error_for_epitaph()`.

    But, a ready-made verb like this would not be hard to add -- for example:
      nest_error(underlying_rep, next_rep)
      nest_error_inheriting_codes(underlying_rep, next_rep) // Is this useful?
    In contrast to the intrusive solution, this would wrap both provided errors
    in a 3rd error that would just exist for plumbing.  This
    `detail::nesting_error` ought to be mostly transparent, delegating to the
    underlying, but **should**, at least, automatically capture the source
    location where the nesting took place (and maybe a message).  This
    implementation would follow or extend `detail::epitaph_non_value`.

  - (*C++23 required*) The internals of `result` should migrate to
    `std::expected` to avoid needing to handle the "empty by exception" state.

  - Implement the epitaphs optimization from `future_epitaph_in_place.md`.

  - A specialized `rich_exception_ptr::operator bool` might be faster than
    comparing to the default-constructed object.  The idiom isn't currently
    used in any hot code, but only in `nestable_coded_rich_error.h` and tests.

  - Using immortals instead of `Indestructible` in `result.cpp` to make those
    errors more robust (static vs heap allocation).

  - Consider adding a bit state for storing errors without `std::exception_ptr`.
    The upside of eptrs is that they can reference exception stacks, when
    thrown.  The downsides are:
      * They use as much as 120-160 bytes (x86 / ARM) of heap for data (e.g.
        most of `__cxa_exception`) that result-oriented programs won't need.
      * They won't work in no-exceptions codebases (see next bullet).

  - In order for `result` to become the backing implementation of `Try`,
    we must support no-RTTI / no-exceptions codebases.  This is both feasible
    and useful for embedded systems, but hasn't been prioritized yet. When
    working on this, a few breadcrumbs may help:
      - Start by setting up a corresponding CI environment / test protocol.
      - You will almost certainly want to implement eptr-free error storage
        first (see previous bullet).
      - `folly::throw_exception` abstracts "throw if supported, terminate
        otherwise".  There are many other similar utilities.  Lacking a
        utility, code can always gate on `kHasExceptions` and `kHasRtti`.

  - Error handling via the `if`-`get_exception` pattern is fine, but sometimes
    declarative is better. One idea is to tag lambdas via `operator=`:
    ```cpp
    auto v = res.value_or_handle_via(
        folly::type<YourErr> = [](const YourErr& ex) { ... },
        folly::type<OtherErr> = [](const OtherErr& ex) { ... },
        [](const error_or_stopped& eos) { /* catch-all */ });
    ```
    This could also be `co_await handle_or_unwind(res, ...)` without the
    catch-all. As syntax "sugar", this does not have much urgency. But,
    statically knowing the full list of queried types would support faster
    dynamic type resolution, either via `exception_ptr_get_object_hint` or via
    `future_fast_rtti.md`.

  - Implement `future_small_value.md`, the small value optimization for
    `result<int>`, `result<T*>`, `result<T&>` et al, and for
    `result<unique_ptr<T>>`.

  - Either implement `future_fast_rtti.md`, or build `std::type_info` caching
    (`F14ValueSet` with some eviction strategy) for `rich_error_base`, likely in
    `lang/Exception.h` via another member type plugin.

  - `result<T&&>` is not equality-comparable, since
    `folly::rvalue_reference_wrapper` is neither implicitly convertible to the
    ref, nor provides `operator==`. If there's a strong use-case for this, the
    right fix is probably to add that operator. At that point, `result_test.cpp`
    already has `// FIXME: Implement rvalue_reference_wrapper::operator==`
    commented-out test coverage. Also update `value_only_result_test.cpp`.

## Auto-capture locations for immortals on C++23

A "nice to have" would be a way to automatically capture `source_location`
while supplying a `rich_msg` to an `immortal_rich_error<MyErr, ...>` template
parameter list.

You can see a not-very-satisfactory example of what *can* be done today in
`immortal_rich_error_test.cpp`. Roughly:

```cpp
constexpr static auto myLoc = source_location::current();
auto rep = immortal_rich_error<MyErr, &myLoc>.ptr();
```

Since `rich_msg` is non-structural, any auto-capture must be done via a
structural helper type implicitly-convertible to `rich_msg` (as with `vtag<Str>`
today). We do want to use `rich_msg` in the user types, since that offers a
consistent & good experience for runtime errors.

The trouble is that `source_location` is not structural, and so the helper type
can only store its pointer. But, in C++20, there is no way to allocate static
storage from a variable -- unless it's `constexpr`, which an auto-captured
`source_location::current()` could not be. So, we have to wait for C++23 support
of `static constexpr` locals. As of late 2025, this sort of thing only works on
GCC. Clang wrongly garbage-collects the `loc` symbol, getting a linker error.
And the MSVC on Godbolt doesn't seem to support the C++23 feature yet.

```cpp
template<const source_location* Loc>
struct SourceTag { ... };
#define HERE() \
([] { \
  static constexpr auto loc = source_location::current(); \
  return SourceTag<&loc>{}; \
}())
```

Technically, one could side-step these issues by creating a custom structural
type that stores filename & function name as char arrays, and is constructed via
a macro. Essentially, roll-your-own-`source_location` (be sure to indirect it
through a pointer so that `rich_msg` stays 8 bytes!). However, using a
non-standard type is a heavy interface cost, for a use-case that doesn't seem
that critical. After all, one can usually easily search for an immortal string
to find the source location.
