# Design notes for `rich_exception_ptr`

## What can `rich_exception_ptr` store?

In `folly::result`, we aim to eliminate the forced choice between "flexible
exceptions" and "simple error codes".

`rich_exception_ptr` is meant to be an analog of `std::exception_ptr` or
`folly::exception_wrapper`, which has qualitatively better performance in
common usage on 64-bit libcxx / libstdc++ systems.  It should be cheap enough
to displace less-ergonomic error-code-only systems in the less-hot 99% of code.

### Dynamic `std::exception_ptr` (aka `eptr`)

Of course, we must be able to store dynamic `std::exception_ptr`s, and retain
the optimizations for "make" and "move" from `folly::exception_wrapper`.

We will additionally provide efficient union storage for the following special
categories.  Make a note of their abbreviations, we use them below.

### Rich errors (aka `RE`)

Think of this as a better base for your exception hierarchy. It provides the
handful of features any production project is likely to want:

  - Error code support compatible with `std::error_code`, but `constexpr`.

  - `fmt` and `operator<<(ostream&)` support for convenient, allocation-free
    logging.  Note that, in contrast, the `std::exception` API of `const char*
    what()` makes it hard to avoid allocations when logging dynamic messages.

  - Annotations aka "message stacks".  These let you cheaply add debugging
    context to an exception, as it propagates, without changing the type
    identity of the underlying exception.

To make things even better, typical usage of `rich_error` APIs is able to
completely avoid RTTI, making it faster than the `std::exception` hierarchy.

### Immortal `rich_error` exceptions (aka `immortal RE`)

These are cheap-to-copy pointers to immortal `constexpr` exceptions, which can
be passed as `rich_exception_ptr`s, and expose `rich_error` APIs.

These are necessary since dynamic `std::exception_ptr`:
  - requires an allocation,
  - updates atomic refcounts when copied or destroyed,
  - cannot be `constexpr` -- whereas static variables risk SIOF, or require a
    function call to initialize.
In short, exception pointers are much costlier than `std::error_code`.  This is
why, in a closely related design, `boost::outcome::outcome` is separate from
`boost::outcome::result`.

Immortal `rich_error`s bridge that gap -- they're about as cheap as codes, but
are natively usable in the exception-style API of `result`.

### Why we special-case `OperationCancelled` (aka `OC`)

`folly/coro` has a convention by which child task may signal to its parent that
it stopped due to cancellation.  First, look through https://wg21.link/p1677 to
understand the problem space.

Today, `folly/OperationCancelled.h` propagates as an exception through
`folly/coro` coroutines. Child tasks can emit it in a number of ways, but
propagation semantics are always throwing-by-default:

```
co_yield co_cancelled;
throw OperationCancelled{}; // discouraged
co_yield co_error{OperationCancelled{}}; // discouraged
```

Most user code should not directly interact with cancellation primitives, as it
is primarily a tool for implementing higher-level algorithms.  99% of the time,
the right user code behavior is to promptly unwind the stack, and return.

Unfortunately, today's exception-like implementation leaves the details of
"cancellation completion" woefully exposed.  For example, all this
innocent-looking user code will currently interrupt cancellation without
additional special-casing:

```
try { co_await task(); } catch (...) { handleError(); }
try { co_await task(); } catch (const std::exception& ex) { handleError(ex); }
auto res = co_await co_awaitTry(task());
```

The consequence is lots of explicit, error-prone handling of cancellation in
user code.  And lots of bugs.

I soon intend to propose `co_yield co_cancelled_nothrow`, plus a migration
strategy that keeps the current code working, while encouraging new and
refactored code to adopt the new primitive.

For the purposes of this document, you just need to know that:
  - We will have 2 different `...OperationCancelled` types.
  - The type emitted by `co_cancelled_nothrow` always propagates through
    `folly::coro` code as if wrapped with `co_nothrow()`.  When going outside
    of coroutines (e.g. `blocking_wait`), it will still be thrown due to a
    lack of better alternatives.
  - Therefore, both must be representable by `rich_exception_ptr`, and cheaply
    distinguishable both from each other, and from other types.

### Small values

Another good use of the union storage is the small-value optimization, where
types like `result<int>` or `result<Foo*>` can fit in just 8 bytes.

# Bit-packed union in `rich_exception_ptr`

On supported 64-bit platforms, `rich_exception_ptr` has the same size as
`std::exception_ptr`, while packing in all the extra functionality above.

The choice of variant is stored in the bottom 3 "alignment" bits, which would
otherwise be zero.  Sometimes, we additionally check whether the other 61
"pointer" bits are null or not.  To clarify:
  - When the low 3 bits tell us we're storing a small value, then we may NOT
    treat "all top bits are zero" as special.
  - However, in all other cases, "null pointer" can be a distinguished state.

Note: Although 64-bit pointers leave the top byte (or more) unused, using those
bits can interfere with memory tagging schemes.  In contrast, it is cheap and
safe to use the 3 low bits that are always zero due to 8-byte alignment.

In all, we could therefore represent up to 14 = (2**4 - 2) non-value states,
and store 7 types of non-value pointers.  However, besides "small value", we
only need 8 more states & 6 pointers below, and can therefore make some choices
that make the packing and unpacking more CPU-efficient.

This section aims to explain WHY we ended up with the current states & bit
representation.  If you just want to see the bit-packing scheme, the table is
under "Idea 1".

## Inputs into the bit-packing design

### States to represent

  - Small value -- **must** allow arbitrary top 61 bits (all-zero, or nonzero).

  - Empty `Try` -- needed so `Try` can be reimplemented in terms of `result`.

  - Empty `exception_ptr` -- prefer to set all bits to zero.  OK to share the
    3-bit code with immortal `rich_error`s, since null ones don't exist.

  - For these, the top bits store a non-null pointer:

    * Immortal `rich_error` -- The bottom 3 bits **must** be 000 since
      C++20 does not allow pointer bit twiddling in `constexpr` code.

    * Unknown-type `exception_ptr`

    * `exception_ptr` to `rich_error`

    * `exception_ptr` to two variants of `OperationCancelled`
      - Legacy / thrown exception; currently stores a dynamic `exception_ptr`
      - New `co_cancelled_nothrow`; currently stores a leaky singleton ptr.

    * Known-type non-fast-path `exception_ptr`.

      Why is this distinguished from "unknown type"?  -- On our "happy path",
      an error is only handled by `rich_exception_ptr`.  Then, we want
      `get_exception<Ex>()` to always use the non-RTTI fast-path with `Ex` of
      `rich_error_base` and `OperationCancelled`.  It is not enough for the
      bits to answer "yes, this derives from `Ex`", but they must also say
      "this definitely is NOT `Ex`".

### Runtime efficiency priorities, highest to lowest

Our implementation ("Idea 1" below) accommodates all these priorities:
  - `has_dynamic_exception_ptr()` is a very common check, and should be fast.
    Our test is `bits_ & 0x1`.
  - Cheap test for "is rich error pointer?". We check via `!(bits_ & 0x6)`.
  - `has_stopped()` should beat RTTI. We test via `4 == bits_ & 0x6`.

### Must `rich_exception_ptr` preserve `OperationCancelled` object identity?

Per https://wg21.link/p1667, in an ideal world, we would not think of
`OperationCancelled` as an exception, but rather as a simple sigil that says
"tear down the stack as fast as possible".  Such a sigil should NOT need object
identity.  Unfortunately, the legacy `OperationCancelled` is often represented
as an `exception_wrapper` that can expose `OperationCancelled*`.  That pointer
was historically **stable** on supported platforms, unless re-thrown.  Per my
survey, it seems unlikely that any existing user code relies on object
identity.  Such code looks weird, and has no obvious reason to exist:

```cpp
OperationCancelled* innerPtr = nullptr;
auto ew = co_await co_awaitTry([&innerPtr]() -> now_task<> {
  auto innerEw = make_exception_wrapper<OperationCancelled>();
  innerPtr = get_exception<OperationCancelled>(innerEw);
  co_yield co_error(std::move(innerEw));
}()).exception();
auto* outerPtr = get_exception<OperationCancelled>(ew);

// Outcome A:
assert(innerPtr == outerPtr);
// Outcome B:
assert(innerPtr != outerPtr); // AND, `innerPtr` is no longer valid
```

Migrating `OperationCancelled` from something that's backed by an
`exception_ptr` to a simple sigil has some efficiency benefits.  The question
is whether there is unacceptable (current or future) risk in migrating from
today's "Outcome A" to the cheaper "Outcome B".

While the practical risk seems low, there's the [`std::exception_ptr_cast`
proposal](https://wg21.link/p2927), which lends credence to the idea of
preserving exception object identity in code that doesn't throw (on Windows,
thrown exceptions may be copied).  Accepting that proposal would bless the
practice of writing generic code that handles eptrs without rethrow -- and such
code could then correctly assume pointer stability.

For now, our implementation ("Idea 1" below) resolves the puzzle in the most
conservative way:
  - Preserve dynamic object identity for the legacy, thrown
    `OperationCancelled`.
  - Use a leaky singleton for the new `co_cancelled_nothrow`, but still
    store its pointer so that, on the off-chance that two DSOs end up with
    different instances of the singleton, object identity is preserved.
If a compelling performance argument comes up, we can revisit this choice.

### Requirements rejected, or shelved indefinitely

We could use, or repurpose, some of the extra bits to make more use-cases
efficient.  Here are some ideas of this sort, and why they're not part of the
current implementation.

  - RTTI-free `std::exception`: This already works for immortal REs, and would
    be quite easy to do for dynamic eptrs.  It is rejected because:
    * It comes with implementation tradeoffs --  for example, we cannot combine
      this with "Idea 1" below without hurting "is eptr?" performance.
    * Furthermore, all it would enable is RTTI-free code to log `what()`.  As
      discussed above, `what()` is a poor API, and logging `rich_error` -- with
      detailed context & stacked annotations -- should be strongly preferred.

  - It would be technically straightforward to support immortal exceptions
    of non-`rich_error` type, but:
    * **This is blocked** until C++ (or all toolchains of interest) allow
      overwriting the alignment bits of constexpr pointers.
    * Most meaningful uses of this feature would involve errors deriving
      from `std::exception`, which is not constexpr until C++26.
    * Finally, there's usually no downside -- and lots of advantages -- to
      making your immortal error speak the `rich_error` protocol.

  - As noted in the `rich_error_base` docblock, if we had another free bit in
    `rich_exception_ptr`, we could use it to cache the absence of a parent
    error in the annotation stack.  However, the current solution is pretty
    good, and it's not trivial to eke out another cross-platform bit.

## Design space

The current implementation uses Idea 1.  We may re-explore the others, but this
would likely require reconsidering the performance design constraints, or the
desire to preserve OC object identity.

### Idea 0: Store dynamic eptrs for both OCs

Use two 3-bit positions on OCs, preserving dynamic eptrs for both.
  - **Not doing this** -- "has dynamic eptr?" would no longer be a 1-bit test.
  - Also, we don't even WANT normal `co_cancelled_nothrow` usage to allocate a
    dynamic eptr.

### Idea 1: Only store dynamic eptr for the legacy thrown OC

  - Use one 3-bit position to allow the eptr of the legacy thrown OC to vary,
    match existing behavior & simplifying migration.

  - The new "nothrow OC" stores an eptr, but its ctor is restricted so it can
    only point a leaky singleton.

    CAVEAT: If you throw one of these, catch it in an `exception_ptr` and then
    ingest the eptr into `rich_exception_ptr`, we must not put it in an
    RTTI-free state, or we'd lose object identity.

  - For now, write OC user docs as if OC pointers are NEVER guaranteed to be
    stable, to preserve the option value of migrating to "Idea 2".  That is, do
    not acknowledge that P2927 acceptance might force a stronger contract.

```
                    Top 61 bits     1-bit           2-bit          4-bit
                                    (dynamic eptr?) (is RE?; is OC?)

small value         any             0               1              1

immortal RE         not all zero    0               0    <-RE->    0
empty eptr          0...0           0               0    <-RE->    0

Constants --
empty Try sigil     0...0           0               1              0
(future sigils)     not all zero    0               1              0
nothrow OC          not all zero    0               0    <-OC->    1

Dynamic eptrs --
unknown-type eptr   not all zero    1               1              0
RE eptr             not all zero    1               0    <-RE->    0
thrown OC           not all zero    1               0    <-OC->    1
non-fast-path eptr  not all zero    1               1              1
```

## Idea 2: Store eptrs for neither OC

Avoid dynamic eptrs for both OCs, but with a 3-bit code distinct both from
"empty `Try`" and from "immortal RE".
  - **Not doing this yet** -- The main reason is that it'd be more costly
    to migrate all the existing places that construct the legacy OC object to
    use "leaky singleton eptr" semantics. Plus, if you're throw the legacy OC
    even once, that is 50x more expensive than allocating the dynamic eptr.

## Idea 3: Both OCs share 3-bit code with "empty `Try`"

Treat both OCs as eptr-free constants, **and** use the same 3-bit code as `Try`.
  - **Not doing this** -- discards OC object identity, we're not short on bits,
    and it would make "is OC?" tests slower.

## Idea 4: Both OCs & "empty `Try`" share 3-bit code with immortal REs

Allocate constexpr pointers to colocate OCs and empty `Try` in the "immortal
RE" namespace.
  - Saves one 3-bit code in exchange for having to test for "is empty `Try`?"
    every time we want to get an RE out of a `Try`.
  - Would have to make OCs comply with the RE interface.  Test for it via "is
    RE? && is OC?  (via vtable)".
  - Requires migrating legacy OCs to the singleton eptr model.
  - **Not doing this** -- no need, higher complexity & runtime cost.
