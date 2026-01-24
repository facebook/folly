# Quick-start for `result.h` and `rich_error.h`

`folly/result` offers an error-handling paradigm that can be safer than
throwing, more explicit, and over 100x cheaper on the error path.  It
interoperates smoothly with exception-throwing code, and with `folly::coro`.  It
is intended to supersede `folly::Try` in new code (rationale in
`docs/design_notes.md`).

This file is just a teaser!  Before adopting `result` in your project, be sure
to review `docs/result.md` and `docs/rich_error.md`.  For API details, consult
the in-header docblocks.

Together, `result` and `rich_error` make it easy & ergonomic for C++
applications to handle errors without throwing.  Follow a few best practices to
achieve performance similar to `std::expected<T, std::error_code>`, but with:
  - A far better UX, including short-circuiting coroutines, opt-in stacks,
    and source locations with minimal boilerplate.  The example shows all 3.
  - The flexibility to fall back to exceptions wherever needed.
  - Seamless interop with `folly::coro` and legacy `Try`.

## Basketball is harder than C++

Here is a part of `result/demo/basketball.cpp`. Forgive the bugs, I don't really
know this game. But, first, a mini-glossary:
  - Functions return `result<T>` instead of throwing.
  - We typically use coroutine syntax (`co_return`) so that errors can propagate
    via `co_await or_unwind(...)`. That returns early if the inner call did not
    complete with a value -- similar to Rust's `?` operator.
  - `result` coros also wrap uncaught exceptions from their scope.
  - The `enrich_non_value` wrapper adds context to "error" and "stopped"
    completions, building a lightweight "stack" for debugging.

```cpp
result<Ball> inboundsPass(Player& passer, Player& pointGuard) {
  if (!passer.canSeeClearly(pointGuard)) {
    co_return error_or_stopped{make_coded_rich_error(
        // Don't actually use `std::errc` in a basketball simulator!
        std::errc::resource_unavailable_try_again,
        "passing lane blocked")};
  }
  co_return co_await or_unwind(passer.passTo(pointGuard));
}

result<int> runFastBreak(
    Player& inbounder, Player& pointGuard, Player& shootingGuard) {
  Ball ball = co_await or_unwind(enrich_non_value(
      inboundsPass(inbounder, pointGuard), "fast break collapsed"));
  ball = co_await or_unwind(pointGuard.bounceTo(shootingGuard));
  co_return co_await or_unwind(
      shootingGuard.layup(std::move(ball))); // 🏀
}
```

Suppose `canSeeClearly()` returns `false`, and the caller of `runFastBreak()`
calls `get_exception<>()` on the result. Thanks to `result`'s rich-error
support, that returns a pointer-like object that can be usefully printed:

> passing lane blocked - std::errc=11 (Resource temporarily unavailable) @ result/demo/basketball.cpp:39 [via] fast break collapsed @ result/demo/basketball.cpp:47

You'll find some examples of error handling in the basketball `main()`.  And,
be sure to check out the various `docs/`.

## `result` compared with other ways of handling errors

Combining `result` coroutines with `rich_error` gives us an alternate exception
paradigm. The code feels comparable to standard C++ exceptions, with some key
differences:
  - Propagation is explicit via `co_await or_unwind()`, reducing unhandled
    error bugs.
  - You get ergonomic & efficient support for type-erased error codes.
  - Natively formattable, including "enrichment stacks".
  - Opt-in error provenance via `enrich_non_value` means that hot error paths
    can opt out, while preserving debuggability everywhere else.
  - As with any result type, the "happy path" sees a 1-branch overhead per
    function call.
  - The error path is *much* cheaper -- if your program needs to survive “more
    than 1% error rate”, then `result` is preferred.

As a result, `result` shines in systems programming, where reliability and
deterministic performance are important.

Here's how `result` compares with a few other well-known error-handling paradigms:

| | C++ exceptions | `result` + `rich_error` | `std::expected` | Rust `result` |
|---|---|---|---|---|
| value path | zero-cost | low ns | low ns | low ns |
| error path | 1 usec+ **[*]** | low ns | low ns | low ns |
| type erasure | yes | yes | no | no |
| provenance | stacks | `enrich_non_value` | n/a | n/a |
| propagation | implicit | `co_await or_unwind(res)` | `if (res.has_error()) { return ...; }` | `?` |
| handling | `try`-`catch` | `get_exception<>()`, `get_rich_error_code<>()`, `has_stopped()` | `.error()` | `.err()` |
| cancellation | [wrongly](https://wg21.link/P1677), cancellation-as-error | has "stopped" state | n/a | n/a |
| async interop | `folly::coro` | `folly::coro` | n/a | yes |

> **[*]** C++ throw performance could almost certainly be made better, given
extensive compiler / toolchain work.  Here's an exploration of the upper bound
of possible improvement: [Khalil Estell - CppCon 2025](
https://github.com/CppCon/CppCon2025/blob/main/Presentations/Cutting_C++_Exception_time_by_93.4_percent_.pdf).

For brevity, this table omits some popular implementations that are not
fundamentally different:
  - `Try`, which is effectively `expected<T, exception_ptr>`
  - `boost::outcome::outcome`, similar to `expected<T, pair<error_code, exception_ptr>>`
  - `boost::outcome::result`, similar to `expected<T, error_code>`

Any discussion of this subject would also be incomplete without mentioning Herb
Sutter's [Zero-overhead deterministic exceptions: Throwing
values](https://wg21.link/p0709) and related papers (e.g.
[P2170](https://wg21.link/P2170), [P2232](https://wg21.link/P2232),
[P1028](https://wg21.link/P1028), [P1095](https://wg21.link/P1095)). However,
lacking production usage, these didn't make it into the table.
