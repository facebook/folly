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
         immortal `rich_exception_ptr` already working in `constexpr` code.
  - Rich errors have a huge test matrix: (packed/separate storage) x
    (owned/immortal/misc errors) x (enriched/underlying errors) x
    (move/copy/assign/compare/format fundamentals).  To deal with this, current
    rich error unit tests prioritize "testing in depth" -- this means that we
    carefully build up helper functions to exercise a large test matrix.
      - For the bulk of the testing burden, avoid creating a hodge-podge of
        ad-hoc test scenarios.  It should be easy to tell that the whole test
        matrix is covered, without doing extensive book-keeping.
      - **Do** provide "usage example" integration-style tests as well.
  - Get expert review, especially when touching bit-discrimnated variant code,
    which is (by necessity) teetering on the edge of "acceptable UB".

These are sorted from "near future" to "far future".

  - Automatic `fmt` + `<<` for `non_value_result` by making
    `rich_exception_ptr` formattable (via bits and `rich_error_base` if
    available).  This would strengthen `checkEptrRoundtrip` non-aliasing
    checks.  Update other tests, like `enrich_non_value_test.cpp`.  `result<T>`
    should also do this, iff `T` is formattable.  Update `rich_error.md` /
    `result.md` / `README.md` accordingly.

  - Automatic enrichment for `result` coroutines as in `enriching_errors.md`.
    I'm thinking of symbolizing the stack and attaching it to the exception in
    `unhandled_exception`.  Perf-wise this should be "fine" since `throw` is
    already stupid-expensive.  Update docs, since this is Very Useful.

  - Are we happy with the moved-out behavior of `non_value_result`?  Today it's
    "dfatal crash" / empty eptr.

  - Start `result/containers.h` with `result<T&> map_at(Map, Key)` and similar
    functions that mimic throwing `std` patterns, but return `result`.

  - Support a flavor of `get_rich_error_code` returning a `rich_code<Code>`
    that is formattable with enrichments (provenance).  One API idea is to add
    a `get_rich_error_code<rich_code<Code>>()` overload, another is to just add
    a new verb.

  - Rich error / result formatters may want to parse out some options to
    customize the output style (separator / indentation).  Another important
    one would be to omit enrichments (e.g. `checkEptrRoundtrip` wants this).
    Before doing this, make sure the default output style is broadly readable &
    useful -- with time, automation will rely on parsing that, so it will be
    hard to change.

  - `rich_exception_ptr` should also be formattable, but since it's not (very)
    user-visible, this is lower-priority than `result` / `non_value_result`..

  - We already have `enrich_non_value.h` and `nestable_coded_rich_error.h`,
    but neither is a direct counterpart to `std::nested_exception`. We
    don't really need anything to support that "intrusive" behavior, current
    users can just implement `next_error_for_enriched_message()`.

    But, a ready-made verb like this would not be hard to add -- for example:
      nest_error(underlying_rep, next_rep)
      nest_error_inheriting_codes(underlying_rep, next_rep) // Is this useful?
    In contrast to the intrusive solution, this would wrap both provided errors
    in a 3rd error that would just exist for plumbing.  This
    `detail::nesting_error` ought to be mostly transparent, delegating to the
    underlying, but **should**, at least, automatically capture the source
    location where the nesting took place (and maybe a message).  This
    implementation would follow or extend `detail::enriched_non_value`.

  - Implement the enrichment optimization from `future_enrich_in_place.md`.

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
    working on this, a few things breadcrumbs may help:
      - Start by setting up a corresponding CI environment / test protocol.
      - You will almost certainly want to implement eptr-free error storage
        first (see previous bullet).
      - `folly::throw_exception` abstracts this choice away, and there are many
        other similar utilities.

  - Implement `future_small_value.md`, the small value optimization for
    `result<int>`, `result<T*>`, `result<T&>` et al, and for
    `result<unique_ptr<T>>`.

  - Either implement `future_fast_rtti.md`, or build type_info caching (F14ValueSet
    + some eviction strategy) for `rich_error_base`, likely in `lang/Exception.h`
    via another member type plugin.

  - Very far future: Which of my code needs to be gated on `kHasRtti`?
