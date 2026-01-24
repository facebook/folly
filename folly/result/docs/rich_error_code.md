# Rich error codes: generic, type-erased, small, and fast

Modern C++ applications must handle exceptions (because nearly everything
throws), but they may also want to be more disciplined with internal error
flows -- for those, returning "value or error code" is a common pattern.

Rich error codes integrate with `folly::result` & rich errors.  Compared to
`<system_error>`, they provide similar-but-better functionality.

  - `Code`s are value types that fits in a `uint64_t`.

  - A rich error type may export one or more code types (e.g. general &
    specific, or OS & application-internal), with efficient access via the
    `rich_error_base::retrieve_code()` virtual call.

  - Users can query any error container supporting `folly::get_exception` via
    `get_rich_error_code<Code>(container)`.

  - Rich code access is fast & ergonomic when used with `folly/result` tools.

## Why use `rich_error_code.h`?

If you already use rich errors -- for the automatic source locations, for the
ergonomic error-provenance enrichment, for the speedy mostly-no-RTTI
performance, or for any of its other benefits -- then the reason to adopt
`rich_error_code` is that it is built-in!  Key integrations:

  - Rich error formatting automatically displays all the codes from an error.

  - `...coded_rich_error.h` and `errc_rich_error.h` cover all the common uses.
    But, the underlying protocols support custom composition & inheritance, too.

  - `get_rich_error_code<Code>(...)` works on all standard error-containers
    (including anything that speaks `folly::get_exception`).  It is extra-fast
    for `result` / `error_or_stopped`, and works for `std::exception_ptr`, etc.

  - Fast type-unerasure -- checking if a type-erased error has a `Code` is much
    faster than RTTI (<5ns typically).  First, we `get_rich_error(container)`,
    which is specifically optimized for `*result` containers.  Once you have a
    `rich_error_base*`, code retrieval is reliably RTTI-free.

If you do not yet use rich errors, you may want to scroll down to "Prior art"
for a quick comparison with other error code designs.

## Basic usage

Define a code type (typically, an enum class) and specialize `rich_error_code`:

```cpp
#include <folly/result/rich_error_code.h>

enum class FruitCode { BAD, UNRIPE, READY, OVERRIPE };

template <>
struct folly::rich_error_code<FruitCode> {
  // Generate with: python3 -c 'import random;print(random.randint(0, 2**64 - 1))'
  // DO NOT CHANGE once committed - this is part of your ABI!
  static constexpr uint64_t uuid = 16014278773182690925ULL;
};

// Optional / encouraged. Use an exhaustive switch from `folly/lang/Switch.h`.
struct fmt::formatter<FruitCode> { /* ... */ };
```

Now, you can use `coded_rich_error<FruitCode>` for typical error-handling. Power
users:
  - Yes, an error can have multiple code types, you can query any/all of them.
  - Yes, you can inherit & combine error codes across error type hierarchies.
  - Check out `nestable_coded_rich_error.h` if `coded_rich_error` isn't enough.
  - If you do need your own coded error, follow the docs in `rich_error_code.h`.

Since all errors with the same code are logically interchangeable, you can get
significant efficienty wins from using immortal error instances in hot code --
these are *almost* as cheap as regular integer codes!

```cpp
using namespace folly::string_literals; // for `_litv` suffix
static constexpr badFruit = immortal_rich_error<
    coded_rich_error<FruitCode>,
    FruitCode::BAD,
    "Rotten, moldy, or damaged"_litv>.ptr();

result<double> fruitToCalories(Fruit f) {
  if (!f.isGood()) {
    return error_or_stopped{badFruit};
  }
  // ...
};
```

The beauty of immortal errors is that they quack just like their dynamic
counterparts. You can still `enrich_non_value` to add context, convert them to a
dynamic `std::exception_ptr`, throw them, etc.

But, if you need a dynamic error, you can change the above code like so,
without breaking any contracts.  The dominant cost will be ~60ns to allocate
(and later free) an extra `std::exception_ptr` on the heap.

```cpp
  if (f.isMoldy()) {
    return error_or_stopped{make_coded_rich_error(
        FruitCode::BAD, "Moldy {}: {}", f.name(), diagnoseMold(f))};
  }
```

## Querying for codes

Get `std::optional<Code>` from any error container using `get_rich_error_code`:

```cpp
struct Human {
  // ...
  result<> eatLunch(Lunch l) {
    // ...
    {
      auto res = fruitToCalories(l.fruit_);
      if (auto code = get_rich_error_code<FruitCode>(res)) {
        // Real code might use `FOLLY_EXHAUSTIVE_SWITCH`
        if (code == FruitCode::BAD) {
          makeYuckFace();
        } else { /* ... */ }
      } else {
        energy_ += co_await or_unwind(res);
      }
    }
  }
}
```

This API is particularly fast with `folly/result` containers, but will works on
anything with `folly::get_exception` support.

## Prior art: Why this custom design instead of <my favorite header>?

For a high-level comparison of `result` with `std::expected`, `boost::outcome`,
and other coded-error alternatives, see `design_notes.md`.

The closest thing to standard error-code handling is `<system_error>`.  Sadly,
it has many defects.  For details, see [P0824](wg21.link/p0824), or a summary
in Niall Douglas's [`status_code` docs](https://github.com/ned14/status-code)).

For end-users, perhaps the greatest downside is the high conceptual complexity.
If in doubt, here are two *long* blog posts on defining an application-specific
error category using `std`:
[1](https://akrzemi1.wordpress.com/2017/07/12/your-own-error-code/),
[2](http://blog.think-async.com/2010/04/system-error-support-in-c0x-part-5.html).

For other prior art, read the front matter to Niall Douglas's [P1028
status_code](https://wg21.link/P1028).  Standardization of this design was
abandoned by the author as of [August 2024](
https://discourse.bemanproject.org/t/p1028-status-code/175).  The code is
available on [Github](https://ned14.github.io/status-code/), and also available
as [`boost/outcome/experimental`](
https://www.boost.org/doc/libs/latest/libs/outcome/doc/html/experimental.html).

Herb Sutter's [P0709](https://wg21.link/P0709) is also worth reading for a
thorough exploration of the problem space.

Instead of repeating what has been said before, let's cover what `folly/result`
does differently from `std` and `status_code`:

  - Our code types (`enum class FruitError`) are self-contained -- you don't
    need to know a "category" or "domain" to use a code.  For value-type `T` to
    be usable, it must fit in `uint64_t` and specialize `rich_error_code<T>`.

  - `folly/result` supports type erasure of errors via `error_or_stopped`.
    Going from `error_or_stopped` to `rich_error_base*` is deliberately fast.
    We piggyback on this to avoid introducing any additional notion of
    "category" or "domain".  Each rich error class is its own domain.  This
    lets us synthesize extremely fast lookup of `Code` from `rich_error_base*`.

  - In P1028, `std::errc` / `generic_code` is special, in that cross-domain
    comparisons become possible by uniformly converting application codes to
    `errc`.  We don't bring this complexity into the core design.  Either make
    your codes comparable, or provide multiple codes per error, and compare the
    compatible ones.

As a result, the rich error code design works much better in `folly/result`:
  - Lacking any notion of domain, it is simpler to understand.
  - Integrates perfectly with the `folly/result` ecosystem.
  - Quality-of-life features like formatting.
  - Codes fit in 8 bytes, not 16.
  - Undoing type erasure ("query") is comparably fast to P1028, but supports
    multiple codes per error.
  - Scope creep into "handle status codes, not just error codes" is
    deliberately avoided.  We focus on errors.
  - No obvious regrets compared to `<system_error>` or P1028 `status_code`.
