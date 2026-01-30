# Rich Errors

A **rich error** implements `rich_error_base`, providing a common interface for
throwing and `result`-returning code. The happy path is nearly as fast as
`std::expected<T, EnumCode>`, while smoothly interoperating with exceptions.

The pitch for `result` + `rich_error` is "best of both worlds":
  - As explicit as C-style error codes, almost as fast.
  - Almost as concise as thrown exceptions.

In contrast to the minimal `std::exception`, `rich_error_base` efficiently
addresses **all** the common error-handling needs of services:
  - **Error codes:** Like `std::system_error`, but cheaper & easier. See
    `coded_rich_error.h`, `errc_rich_error.h`, `nestable_coded_rich_error.h`,
    and `rich_error_code.md`.
  - **Logging:** `fmt` and `<<` to supersede the kludgy `what()`. Hot errors can
    store structured data & literals, only paying for formatting when needed.
  - **Compatibility:** Works transparently with standard exception-based code --
    when needed, you can use inheritance, throwing, and RTTI.
  - **Provenance:** Captures epitaphs -- a propagation message & source
    location. Think of it as customizable stack traces for the return-`result`
    paradigm. See `epitaphs.md`.

## Tutorial

Define an error type by inheriting from `rich_error_base`, or from a
feature-added type like `coded_rich_error`. Query the type directly via
`get_exception<Err>()` or in `catch` clauses. Wrap it as `rich_error<Err>` or
`immortal_rich_error<Err, ...>` **only** when instantiating:

```cpp
enum class FruitCode { BAD, UNRIPE, OVERRIPE };

template <>
struct folly::rich_error_code<FruitCode> { /* See `rich_error_code.md` */ };

// Most programs use `coded_rich_error` directly; this shows how to extend it.
struct FruitError : coded_rich_error<FruitCode> {
  using coded_rich_error::coded_rich_error;
  using folly_get_exception_hint_types = rich_error_hints<FruitError>;
};
```

Now, let's peel some fruit:

```cpp
struct Fruit {
  std::string str_;
  bool isGood() const;
  result<Food> peel() {
    if (!isGood()) {
      // `make` auto-captures source location; see `coded_rich_error::make`
      return error_or_stopped{
          FruitError::make(FruitCode::BAD, "cannot peel: {}", str_)};
    }
    // ...
  }
};
auto res = Fruit{"rotten orange"}.peel();
```

Query for `FruitError`, which **is not** an `std::exception`.  Do not query for
`rich_error<FruitError>` to avoid unintentional calls to `->what()` -- that
legacy `std` API cannot efficiently log error data with epitaphs (like
propagation notes & source location).  Instead, `get_exception` on
`folly/result/` containers returns a pointer-like supporting `fmt` and `<<`:

```cpp
if (auto err = get_exception<FruitError>(res)) { // NOT `FruitError*`
  // Logs code, message, source location, and propagation notes
  LOG(ERROR) << "Failed to peel fruit: " << err;
}
```

Programs that assign codes to all errors can handle most errors via
`std::optional<Code> get_rich_error_code<Code>(errorContainer)`, letting uncoded
exceptions propagate to last-resort handlers. Rich codes are cheap and usually
RTTI-free:

```cpp
assert(FruitCode::BAD == get_rich_error_code<FruitCode>(res));
```

**Caution:** Absence of `Code` doesn't mean there is no error -- the error might
have a different code, or not support the rich codes protocol at all.

Speaking of last-resort handlers: to handle all rich errors, `get_rich_error()`
is syntax sugar for `get_exception<rich_error_base>()`. Always do that *before*
checking for `std::exception` -- it is cheaper, and gives better logging.

All of the above applies to thrown exceptions, too. Prefer to catch
`FruitError` or `rich_error_base` rather than `rich_error<FruitError>` or
`std::exception` -- though the latter is still useful for catch-alls.

## Summary of best practices

  - **Error types:** Inherit from `rich_error_base` or its descendants.
  - **Queries:** Use `get_exception<Err>` and `catch (const Err&)`, not
    `get_exception<rich_error<Err>>`.
  - **Instantiation:** Use `rich_error<Err>` / `immortal_rich_error<Err, ...>`
    *solely* to construct errors:
      - `rich_error<Err>` for runtime errors.
      - `immortal_rich_error<Err, Args...>` for `constexpr` errors that (mostly)
        avoid RTTI.
  - **Catch-alls:** Query `rich_error_base` via `get_rich_error()` before
    checking `std::exception` -- better speed & logging.
  - **Logging:** Prefer `operator<<` or `fmt::format` over `what()`.
  - **Inheritance:** Derive from `Err`, not `rich_error<Err>`. Hints are
    mandatory; list the current type and likely derived types:

    ```cpp
    struct SpecificErr;
    struct BaseErr : public coded_rich_error<MyCode> {
      using coded_rich_error::coded_rich_error;
      // Hint the base last, since it is rarely used directly
      using folly_get_exception_hint_types = rich_error_hints<SpecificErr, BaseErr>;
    };
    struct SpecificErr : public BaseErr {
      using BaseErr::BaseErr;
      using folly_get_exception_hint_types = rich_error_hints<SpecificErr>;
    };
    ```

  - **Source location:** Rich error constructors should auto-capture source
    location by default. Taking `rich_msg` is the simplest approach. For a
    sleeker UX, see `coded_rich_error`'s use of `ext::format_string_and_location`.

## Immortal errors

Immortal errors are `constexpr` singletons with predictably low overhead for
high-error-rate applications:

```cpp
using namespace folly::string_literals; // for `_litv` suffix
static constexpr auto badFruit =
    immortal_rich_error<coded_rich_error<FruitCode>,
                        FruitCode::BAD,
                        "Rotten, moldy, or damaged"_litv>.ptr();

result<double> Fruit::toCalories() {
  if (!f.isGood()) {
    return error_or_stopped{badFruit};
  }
  // ...
}
```

Immortal errors work just like their dynamic counterparts: you can still
`epitaph` to add context, convert them to a dynamic
`std::exception_ptr`, throw them, etc.

Switching to a dynamic error is straightforward and breaks no contracts:

```cpp
if (f.isMoldy()) {
  return error_or_stopped{make_coded_rich_error(
      FruitCode::BAD, "Moldy {}: {}", f.name(), diagnoseMold(f))};
}
```

The dominant cost is ~60ns to allocate and free a heap `std::exception_ptr`.

## Provenance / epitaphs

Errors can carry contextual information as they propagate through your code.
Stack traces help debug exceptions; error codes need an equivalent facility.
For example, `ENOENT` (file not found) can range from "normal user error" to
"serious bug" -- provenance is essential. See `epitaphs.md` for
details; here's the gist:

```cpp
co_await or_unwind(epitaph(
    resultFn(), "in {} due to {}", place, reason));
```

If the inner result contains an error, the wrapper adds a source location and
message. Formatting includes the full epitaph stack:

```
MauledErr [via] in CRYPT due to ZOMBIES @ src.cpp:50 [after] apocalypse @ src.cpp:40
```

Key properties:
  - Epitaphs are **transparent**: APIs like `get_exception<Ex>()` access the
    **underlying** error, not the wrapper storing the context.
  - Epitaphs **cannot** add error codes (codes direct control flow; epitaphs
    are discardable). Use `nestable_coded_rich_error` to change codes.
  - Hot code can opt out. Adding epitaphs currently costs ~60ns; see
    `docs/future_epitaph_in_place.md` for a design that amortizes to 5-10ns.

## Performance

When used well, rich errors are ~10x slower than integer codes, and 10-200x
faster than thrown exceptions. Common cases are optimized; slow paths fall back
to `exception_wrapper`-like performance.

Today's baseline:
  - **~1ns** for most operations on integer codes.
  - **>1000ns** to throw an exception **[*]**.
  - **10-200ns** for RTTI to catch / `dynamic_cast` / `get_exception` a thrown
    exception.
  - Non-throwing `folly` code (like `coro::co_awaitTry` or `co_nothrow`) uses
    `exception_wrapper`, an optimized `std::exception_ptr`:
      - **30ns** to construct or destruct
      - **~7ns** to copy
      - **~1ns** for moves
      - **10-200ns** to `get_exception<Ex>()`, as above. Drops to **4-6ns** when
        `Ex` exactly matches the stored type, or when `Ex` correctly implements
        `folly_get_exception_hint_types`.

Rich errors:
  - **~1ns** for moves
  - Immortal rich errors are `constexpr`, avoid RTTI, and are almost as cheap as
    integer codes:
      - **<5ns** for copy-construct-then-compare-then-destroy
      - **2-5ns** for most `get_exception` queries
  - Dynamic rich errors (thrown or allocated) follow `exception_wrapper`
    performance, with one improvement:
      - **3-10ns** for some `get_exception` queries with no-RTTI optimizations.
  - See `rich_exception_ptr.md` for implementation details, including a special
    optimization for `folly::coro` cancellation.

## Keeping the error path fast

To maintain performance:
  - **Prefer immortal errors** for frequently-returned error conditions.
  - **Use `rich_error_hints`** to enable RTTI-free lookup. Each error type
    should declare `using folly_get_exception_hint_types = rich_error_hints<T>;`.
  - **Use `get_rich_error_code<Code>()`** instead of `get_exception<>()` when
    only the code matters.
  - **Query `rich_error_base` before `std::exception`** -- you do **not** need
    `.what()`, just `fmt::format` or `<<` the `get_exception` return instead.

For some RTTI-avoidance implementation details, see `rich_exception_ptr.md`. For
codes, see `rich_error_code.md`.

## Design rationale: Why query the non-leaf type?

Above, we suggest querying `FruitError` directly, wrapping it with `rich_error<>`
only for instantiation. This is to discourage aliases like:

```cpp
// DO NOT DO THIS
using FruitError = rich_error<FruitErrorBase>;
```

Queries for `rich_error<T>` do work correctly whether the eptr is dynamic or
immortal, but the recommended style is better:

  - **Expected inheritance semantics:** Usually, `get_exception<Base>()` and
    `catch (const Base&)` are expected to match `Derived` as well. Since
    `rich_error<Base>` is a `final` leaf class, it cannot match
    `rich_error<Derived>`.

  - **More readable error handlers:** Querying for `T` is cleaner than querying
    for `rich_error<...>`.

  - **Discourages calling `what()`:** For compatibility with legacy catch-alls,
    `rich_error<T>` inherits from `std::exception`, exposing `what()`. That stub
    only logs `partial_message()`, far less useful than `fmt` or `<<`. Since
    `what()` returns `const char*`, it cannot be improved efficiently -- we'd
    have to preallocate the full formatted string, including epitaphs, which
    is quadratic in call depth.

  - **More robust immortal queries:** For `immortal_rich_error<T>`, querying
    type `T` (consteval, no `std::exception`) never allocates, while querying
    the leaf `rich_error<T>` has to allocate a `std::exception`-derived
    singleton at runtime.

## Footnotes

**[*]** C++ throw performance could almost certainly be made better, given
extensive compiler / toolchain work. Here's an exploration of the upper bound
of possible improvement: [Khalil Estell - CppCon 2025](
https://github.com/CppCon/CppCon2025/blob/main/Presentations/Cutting_C++_Exception_time_by_93.4_percent_.pdf).
