## Error provenance via `epitaph`

An `ENOENT` (File not found) can be anything from a "normal user error" to a
"serious bug", so application logs need context.

`folly/result` lets you add epitaphs to errors as they propagate through your
code.

With traditional exceptions, stack traces are automatic, but stack unwinding is
slow (1usec+) **[*]**. With `epitaph`, you explicitly add *custom*
context as the error propagates. Hot code can opt out, though eventually adding
an entry should amortize to a few nanoseconds (today's V0 implementation has a
~60ns lifetime cost: ctor + dtor, but see `future_epitaph_in_place.md`).

### Basic usage

See `epitaph.h` for the full API:

```cpp
co_await or_unwind_epitaph(resultFn(), "in {} due to {}", place, reason);
// ... syntax sugar for:
co_await or_unwind(epitaph(
    resultFn(), "in {} due to {}", place, reason));
```

If the inner result contains an error-or-stopped, the epitaph wrapper adds a
source location and message.
  - Value results pass through unchanged.
  - Formatting runs only for error-or-stopped results.
  - String literal messages without format arguments do not allocate or format.

### Epitaphs are transparent: APIs access the underlying error

Internally, `epitaph` wraps the error with a different type. But all
public APIs (`get_exception<Ex>()`, `get_rich_error()`, `get_rich_error_code()`,
etc) access the **underlying** error -- the original being propagated.

```cpp
result<> resultFn() {
  return error_or_stopped{std::logic_error{"oops"}};
}
```

There is no way to add rich context *into* the `logic_error`, so we wrap it.
Internally, `epitaph` stores:
  - A `rich_exception_ptr` owning the original `logic_error`, accessible via
    `underlying_error()` for O(1) unwrapping by `get_exception<Ex>()`.
  - A `source_location` of the `epitaph` call-site.
  - A `rich_msg` message (empty, literal, or heap-formatted).

**Important:** Epitaphs **cannot** add error codes. Codes direct control flow,
but epitaphs are discardable annotations. Since codes could be accidentally
lost (via throwing, converting to `std::exception_ptr`, etc), we deliberately
prevent this footgun. To change codes, use `nestable_coded_rich_error`.

### The return of `get_exception<Ex>()` is (richly) formattable

When called on `result` or `error_or_stopped`, `get_rich_error()` and
`get_exception<Ex>()` return `rich_ptr_to_underlying_error<Ex>`, which quacks
like a pointer to the underlying `Ex`. Unlike `Ex*`, it is both
`fmt`-formattable and `<<(ostream&)`-printable, including the full epitaph
stack:

```cpp
auto res = epitaph(resultFn(), "context");
if (auto ex = get_exception<std::logic_error>(res)) { // NOT `auto*`
  LOG(INFO) << "Oh no: " << ex; // includes "context" and source location
  static_assert(std::is_same_v<decltype(*ex), const std::logic_error&>);
}
```

**Caution:** Converting `rich_ptr<Ex>` to a raw `Ex*`, or converting
`error_or_stopped` to `std::exception_ptr` or `exception_wrapper` will **lose
the epitaphs**, keeping only the underlying error.

### Formatted output

Epitaph stacks render with `[via]` and `[after]` separators:

```
OriginalErr [via] last annotation @ src.cpp:50 [after] first @ src.cpp:40
```

Here, `[via]` precedes the epitaph stack, and `[after]` separates its entries
(most recent annotation first).

To emulate `std::nested_exception`, any rich error can store a "caused-by" error
and expose it via `next_error_for_epitaph()`. See
`nestable_coded_rich_error.h` for an example.

When nesting, you may see multiple `[via]` separators, since each nested error
may have its own epitaph stack.

### Future: Semi-automatic epitaphs for `result` coroutines

Here are two ideas to make `result` coroutines easier to debug:

 1. **Hi-pri:** Thrown exceptions go to `result_promise::unhandled_exception`,
    which currently does **not** add epitaphs.

    This gap is easy to close. The cost of `throw` (1usec+) dwarfs the
    epitaph overhead, so `unhandled_exception` should always add epitaphs. A
    special epitaph wrapper type should capture the exception's call stack,
    and expose it via `format_to()`.

 2. **Lo-pri:** `result` coroutines must manually add epitaphs via `epitaph`
    before `co_await`. But, in some coros, the best UX may be to add default
    epitaphs to all `co_await` points, without annotating each one.

    One idea for automatic epitaphs would be to introduce a special
    "epitaph function argument" along these lines:

    ```cpp
    result<> myFn(epitaph_on_co_await<"my fn"_litv>, ...);
    ```

    Then, use `coroutine_traits` to customize the promise type for this
    coroutine, and potentially use the promise constructor to capture dynamic
    context for the annotation. TBD -- the promise ctor might even be able to
    automatically capture the function name, or its caller (via `rsp`?).

### Footnotes

**[*]** C++ throw performance could almost certainly be made better, given
extensive compiler / toolchain work.  Here's an exploration of the upper bound
of possible improvement: [Khalil Estell - CppCon 2025](
https://github.com/CppCon/CppCon2025/blob/main/Presentations/Cutting_C++_Exception_time_by_93.4_percent_.pdf).
