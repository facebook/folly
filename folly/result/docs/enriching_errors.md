## Error provenance via `enrich_non_value`

An `ENOENT` (File not found) can be anything from a "normal user error" to a
"serious bug", so application logs need context.

`folly/result` lets you enrich errors with annotations as they propagate
through your code.

With traditional exceptions, stack traces are automatic, but stack unwinding is
slow (1usec+) **[*]**. With `enrich_non_value`, you explicitly add *custom*
context as the error propagates. Hot code can opt out, though eventually adding
an entry should amortize to a few nanoseconds (today's V0 implementation has a
~60ns lifetime cost: ctor + dtor, but see `future_enrich_in_place.md`).

### Basic usage

See `enrich_non_value.h` for the full API:

```cpp
co_await or_unwind_rich(resultFn(), "in {} due to {}", place, reason);
// ... syntax sugar for:
co_await or_unwind(enrich_non_value(
    resultFn(), "in {} due to {}", place, reason));
```

If the inner result contains a non-value (error or stopped), the enrichment
wrapper adds a source location and message.
  - Value results pass through unchanged.
  - Formatting runs only for non-value results.
  - String literal messages without format arguments do not allocate or format.

### Enrichment is transparent: APIs access the underlying error

Internally, `enrich_non_value` wraps the error with a different type. But all
public APIs (`get_exception<Ex>()`, `get_rich_error()`, `get_rich_error_code()`,
etc) access the **underlying** error -- the original being propagated.

```cpp
result<> resultFn() {
  return non_value_result{std::logic_error{"oops"}};
}
```

There is no way to add rich context *into* the `logic_error`, so we wrap it.
Internally, `enrich_non_value` stores:
  - A `rich_exception_ptr` owning the original `logic_error`, accessible via
    `underlying_error()` for O(1) unwrapping by `get_exception<Ex>()`.
  - A `source_location` of the enrichment call-site.
  - A `rich_msg` message (empty, literal, or heap-formatted).

**Important:** Enrichment **cannot** add error codes. Codes direct control flow,
but enrichments are discardable annotations. Since codes could be accidentally
lost (via throwing, converting to `std::exception_ptr`, etc), we deliberately
prevent this footgun. To change codes, use `nestable_coded_rich_error`.

### The return of `get_exception<Ex>()` is (richly) formattable

When called on `result` or `non_value_result`, `get_rich_error()` and
`get_exception<Ex>()` return `rich_ptr_to_underlying_error<Ex>`, which quacks
like a pointer to the underlying `Ex`. Unlike `Ex*`, it is both
`fmt`-formattable and `<<(ostream&)`-printable, including the full enrichment
stack:

```cpp
auto res = enrich_non_value(resultFn(), "context");
if (auto ex = get_exception<std::logic_error>(res)) { // NOT `auto*`
  LOG(INFO) << "Oh no: " << ex; // includes "context" and source location
  static_assert(std::is_same_v<decltype(*ex), const std::logic_error&>);
}
```

**Caution:** Converting `rich_ptr<Ex>` to a raw `Ex*`, or converting
`non_value_result` to `std::exception_ptr` or `exception_wrapper` will **lose
the enrichment info**, keeping only the underlying error.

### Formatted output

Enrichment chains render with `[via]` and `[after]` separators:

```
OriginalErr [via] last annotation @ src.cpp:50 [after] first @ src.cpp:40
```

Here, `[via]` precedes the enrichment stack, and `[after]` separates its entries
(most recent annotation first).

To emulate `std::nested_exception`, any rich error can store a "caused-by" error
and expose it via `next_error_for_enriched_message()`. See
`nestable_coded_rich_error.h` for an example.

When nesting, you may see multiple `[via]` separators, since each nested error
may have its own enrichment chain.

### Future: Semi-automatic enrichment for `result` coroutines

Here are two ideas to make `result` coroutines easier to debug:

 1. **Hi-pri:** Thrown exceptions go to `result_promise::unhandled_exception`,
    which currently does **not** add enrichment.

    This gap is easy to close. The cost of `throw` (1usec+) dwarfs the
    enrichment overhead, so `unhandled_exception` should always enrich. A
    special enrichment wrapper type should capture the exception's call stack,
    and expose it via `format_to()`.

 2. **Lo-pri:** `result` coroutines must manually enrich via `enrich_non_value`
    before `co_await`. But, in some coros, the best UX may be to add a default
    enrichment info to all `co_await` points, without annotating each one.

    One idea for automatic enrichment would be to introduce a special
    "enrichment function argument" along these lines:

    ```cpp
    result<> myFn(enrich_non_value_on_co_await<"my fn"_litv>, ...);
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
