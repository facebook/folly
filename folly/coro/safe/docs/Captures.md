## Simple guide to `as_capture()`, `capture<T>` and friends

If you are just reading some code with `as_capture()` arguments -- think of
these as smart pointers, each owned by the `async_closure` taking the arg.

For example, `n` below is a `capture<int>`.  It is an **owned capture**, a
wrapper around `int` whose lifetime is tightly bound to the closure.

```cpp
assert(15 == co_await async_closure(
    // NB: Could omit this `bound_args`, since the 1 arg is `like_bound_args`
    bound_args{as_capture(5)},
    [](auto n) -> closure_task<int> {
      co_await async_closure(
        bound_args{n},
        [](auto nRef) -> closure_task<void> {
          *nRef += 10;
          co_return;
        });
      co_return *n;
    }));
```

On the other hand, `nRef` is `capture<int&>`.  It is a **capture reference**
that was implicitly made from `n`.  Per `LifetimeSafetyDesign.md`, there are
various compile-time checks that make it harder to construct invalid capture
references.

### When & how to use `as_capture()`

 1. If type `T` requires async RAII (`co_cleanup`), you will need
    `capture_in_place<T>()`.  For a working example, see `BackgroundTask.h` or
    `SafeAsyncScope.h`.

 1. Suppose you passed a `co_cleanup` type `T` into an async closure (example:
    `safeAsyncScope<>()`).  Then, the closure will internally own
    `co_cleanup_capture<T>`, and the closure's coroutine will get a capture
    reference `c`.

    Now imagine your closure wants to pass a **reference** to a variable `v`
    into the cleanup object, something like `c.someMethod(v)`.  Any correctly
    implemented `co_cleanup` type should require that its inputs are valid
    beyond the point where its cleanup is awaited.  For example, cleanup runs
    after your closure's coroutine exits, so any references to your coro's
    stack are unsafe.  The `capture` type system causes such safety bugs not to
    compile.  See `LifetimeSafetyBenefits.md` for more.

    `capture`s are our mechanism for making lifetime-safe references.  In order
    to make `c.someMethod(v)` work, you will need to make `v` itself a capture,
    by having your closure take `auto v`, and make it either:
      - `as_capture()` for an owned capture, **OR**
      - `parentA` to make a capture reference from a parent's capture.

### Accessing `capture<T>`s

If your function takes a capture, here is all you need to know:
  - All `capture<T>` class templates act like pointers.  Use `->` and `*` to
    access your `T`.
  - Don't worry about the difference between the `capture` templates -- either
    pass by `auto`, or use the type from the compiler error.
  - Never move the capture wrapper, always move the `T` inside it.
    In other words -- good: `*std::move(cap)`, bad: `std::move(*cap)`.
  - Captures should be transparent to value-category modifiers. That is:
      * `const capture<T>` acts like & converts to `capture<const T>`.
      * `capture<T>&` converts to `capture<T&>`.
      * `capture<T>&&` converts to `capture<T&&>`.
  - If you pass `capture<Value> c` into an `async_closure`, it is always passed
    by-reference.  That is, the child closure automatically gets
    `capture<Value&>`, or `capture<Value&&>` from `std::move(c)`.
  - `capture<T>` behaves much like `T`, besides the above caveats (must
    dereference; pass-by-reference in async closures):
      * For value type `V`, `capture<V>` represents ownership. The capture
        wrapper belongs to whatever constructed it, and **should not be moved**
        -- but, if `V` is movable, you can of course move the inner type:
        ```cpp
        V dst = *std::move(srcCap);
        ```
      * `capture<V&>` is copyable & movable.
      * `capture<V&&> rcap` is move-only, but can **explicitly** convert to `capture<V&>`.

        *Caveat*: To reduce use-after-move errors, dereferencing requires rvalues.
        That is, `*rcap` won't work -- you must `*std::move(rcap)`.

### `safe_alias` warning: the "composition hole" & lambda captures

It bears repeating the "composition hole" warning from `SafeAlias.h`.
  - If a type stores any kind of reference (like `capture<Ref>`) or pointer, or
    anything else that's not a straight-up value type, then it **must**
    correctly specialize `safe_alias_of`.
  - When your child closure gets a lambda (or another object) from a parent,
    it is **particularly risky** to pass `capture<Ref>`s into its `operator()`
    (or other member function).  If the parent stored any reference in that
    object, (as easy as `[&](...) { ...  }`, then the child can incorrectly
    plumb through its own short-lived references into the parent's scope.

### Syntax sugar: `capture_indirect<SOME_PTR<T>>`

To access `capture<shared_ptr<int>> capSharedN`, you need to dereference twice:
```cpp
**capSharedN += 10;
```

Writing `as_capture_indirect()` gives you `capture_indirect<shared_ptr<int>>`,
which needs just one dereference, and can still access the `shared_ptr` via
`get_underlying_unsafe()` -- but see its docblock for **RISKS**.

**Watch out:** Be sure to null-check `capture_indirect` via its `operator bool`.
This is important, since, the underlying type is typically nullable!

### Escape hatch: Capture-by-reference

Use via `capture_const_ref{}`, `as_capture{const_ref{}}`, `capture_mut_ref{}`,
etc in your closure's `bound_args{}` list.


This mechanism solves problems similar to `AfterCleanup.h`, but after-cleanup
is strictly safer, so you should prefer it when applicable.

Capture-by-ref is a way of turning a reference from a parent scope into a
`capture<T&>` or `<T&&>` inside a child `async_now_closure`.  While the
`now_task` restriction aids lifetime safety, the user must still be careful to
avoid giving the child the ability to store short-lived child refs in the
parent's scope.  To fix a concrete instance of this problem, the
`AsyncClosureBindings.h` implementation blocks the capture-by-ref mechanism
from passing `co_cleanup` refs & `captures`.

#### Design notes for capture-by-reference

This section discusses potential relaxations of the capture-by-ref safety
rules, which would make it more broadly applicable.  However, the relaxations
would come with both new footguns, and new complexity, so I'm currently
thinking of them as "rejected designs" rather than future work.

**Note 1:** It would be within the spirit of regular RAII to defer awaiting the
closure to a later point in the current scope.  That is, the `safe_task` taking
these `capture_ref()` args would be marked down to `<= lexical_scope_ref`
safety.  This could be a new safety level with:

```cpp
after_cleanup_ref >= lexical_scope_ref >= shared_cleanup
```

The valid lifetime for this body-only `safe_task` is clearly shorter than
`after_cleanup_ref` -- it's invalid whenever the captured refs are destroyed,
which (under typical RAII) is a bit longer than the lexical lifetime of the
task.  In the `lexical_scope_ref` scenario, the user can, of course, invalidate
the reference before awaiting the task, but it takes a bit of effort, and might
be covered if the [P1179R1 lifetime safety profile](https://wg21.link/P1179R1)
is standardized.  For example:

```cpp
std::optional<safe_task<safe_alias::lexical_scope_ref, void>> t;
{
  int i = 5;
  t = async_closure([](auto i) -> closure_task<void> {
    std::cout << *i << std::endl;
    co_return;
  }, capture_ref(i));
  ++i;
}
// BAD: The reference to `i` is now invalid!
co_await std::move(*t);
```

**Note 2:** The way that `async_closure(...  capture_const_ref(...) ...)`
behaves, it seems like we could just universally allow creating
`lexical_scope_capture<T&>` from `T&` -- even outside `async_closure`
invocations.  `Captures.h` would need to support auto-upgrade of
`lexical_scope_capture` to `capture` or `after_cleanup_capture`, depending on
`shared_cleanup` status.  This "universal" implementation would be more
complex, but without `async_closure()`'s capture-upgrade semantics, there's not
a lot of value in obtaining a `lexical_scope_capture<Ref>` -- for example, you
can't use it to schedule work on a nested `safe_async_scope`.

### Debugging lifetime safety compile errors

If you're working with captures, and get a compile error about `safe_task`,
`safe_alias_of`, or similar, there is a good chance that you triggered a
lifetime safety check. Read `LifetimeSafetyDebugging.md` for what to do next --
it also covers the lifetime safety design of `Captures.h`.

### Implementation gaps

  - While `Captures.h` mentions `restricted_co_cleanup_capture`, the
    implementation is not finished. See `FutureWork.md` for more details.
  - `FutureLinters.md` describes several linters that help achieve maximum
    lifetime safety when using captures.

---
---
---

## Notes for advanced users

### Why does `capture` even exist?

`async_closure` is a lexical scope with guaranteed async RAII. When implementing
such a thing, you end up needing to store two kinds of values that live strictly
longer than the coroutine function scope itself. Specifically:
  - Values with `co_cleanup` (details in `CoCleanupAsyncRAII.md`). The
    archetypal type is `safe_async_scope`, which is immovable to allow an
    efficient implementation -- so the storage mechanism also needs to support
    in-place construction.
  - Values that outlive the cleanup, so they can be safely referenced by the
    `co_cleanup` types.
  - Both kinds of values are passed by-reference into the inner coro.  And,
    since `async_closure` tasks often need to be movable (e.g.  to run on
    scopes), the value storage **must provide stable pointers**.
  - In addition to the above two special types of data, we want to be able to
    pass regular arguments into async closures, without ambiguity.

At its core, `capture<T>` addresses those "must have" needs.

However, it also provides some important "bonus" features:
  - As discussed in "your own `co_cleanup` type" below, types supporting
    `co_cleanup` should not be usable outside of a "managed" context that always
    awaits cleanup.

    Without captures, passing a "passkey" type into the in-place constructor for
    `co_cleanup` type could address this need (with some static assertions).
    However, with captures, `capture_proxy` gives us a cleaner solution.
  - `capture`s also help us enforce `safe_alias` lifetime safety heuristics
    using the type system. Doing something equivalent with static analysis on
    "plain" arguments would be prohibitive, e.g. because it would require
    chasing references across compilation units. In contrast, `capture` types
    automatically embed a lifetime safety contract.

### `shared_cleanup` closures downgrade `capture` safety to `after_cleanup_`

*tl;dr* If you see `after_cleanup_SOME_capture`, know that it quacks just like
`SOME_capture`, but with a lower `safe_alias` level.  If this behavior is
blocking you, you may benefit from finishing `restricted_co_cleanup_capture`.

#### Problem solved

When a child closure takes a reference to a parent's async scope, it can easily
give a short-lived reference to a longer-lived task on that scope:

```cpp
co_await async_closure(
    safeAsyncScope<CancelViaParent>(),
    [](auto scope) -> closure_task<void> {
      co_await async_closure(
          bound_args{scope, as_capture(5)},
          [](auto outerScope, auto n1) -> closure_task<void> {
              outerScope->with(co_await co_current_executor).schedule(
                  [](capture<int&> n2) -> co_cleanup_safe_task<void> {
                    assert(*n2 == 5); // Invalid memory access!
                    co_return;
                  }(n1));
          });
    });
```

Note that the lifetime of `n1` aka `n2` is shorter than that of `scope`.  That
is, by the time `*n2` happens, the closure owning `n1` may have been destroyed.

Fortunately, this code doesn't compile thanks to the `after_cleanup_` downgrade
described below:
```
no known conversion from 'after_cleanup_capture<int>' to 'capture<int &>'
```
Changing the inner lambda to `after_cleanup_capture` still won't compile:
```
Bad safe_task: check for unsafe aliasing in arguments or return type
```
Relaxing the inner task to `safe_task<safe_alias::after_cleanup_ref, void>` also
won't let the bug through, since `schedule()` won't take a less-safe task.
```
constraints not satisfied ... schedule( ...
is_void_safe_task<
    safe_task<safe_alias::after_cleanup_ref, void>,
    safe_alias::co_cleanup_safe_ref>' evaluated to false
```

To understand the solution, let's reformulate this bug more abstractly:

  - Any closure taking `co_cleanup_capture<T&>` is vulnerable to the problem,
    **unless** the API of `T` specifically ensures that it only takes inputs of
    safety `maybe_value`.  In this section, we focus on `co_cleanup` types that
    must be able to take references, like `safe_async_scope`.

    NB: Types with value-only APIs should expose `capture_restricted_proxy()`.

  - Actually, closures sometimes reference `co_cleanup` types in ways besides
    `co_cleanup_capture<T&>` -- for example, `capture<AsyncObjectPtr<T>>`.  For
    this reason, we define a brand-new level `safe_alias::shared_cleanup`,
    which must be used for any type that may give a child closure access to the
    parent's longer-lived lexical scope.

    **Note:** The name `shared_cleanup` aims to evoke that a child taking such
    an argument must take the parent's perspective on safety measurements.

  - By definition, APIs of `co_cleanup` types must guard against inputs less
    safe than `co_cleanup_safe_ref`, so the problem can only occur if a child
    is able to obtain a plain `capture<>` (or equivalently `capture_heap<>` /
    `capture_indirect<>).

  - Plain `capture<>`s come about in two ways:
      * Getting a `capture<>` reference from a parent. This scenario is fine --
        **if** the parent could safely hold the `capture<>` together with the
        `co_cleanup_capture<T&>` that creates the risk, then it's no less safe
        for the child to handle that `capture` ref.
      * Making an owned `capture`. By default, owned captures are plain, but
        in the above example, `after_cleanup_capture<>` makes an appearance.
        That is, in fact, the fix!

        **Anytime a closure takes a `shared_cleanup` input, it loses the
        abilitity to instantiate plain captures.** Its owned captures get the
        `after_cleanup_` prefix (the "downgrade"), and it can no longer
        "upgrade" `after_cleanup_capture` references that it gets from a parent
        -- more on both below.

Now that you saw the problem, and the solution, let's review the formalism.

#### Upgrade/downgrade rules

The reference downgrade rules rely on the fact that `co_cleanup_capture` APIs
(per "your own `co_cleanup` type" below) are required to check that the lifetime
safety of each input is `>= co_cleanup_safe_ref`.

An `async_closure` is considered `shared_cleanup` if any of its external
(non-owned) arguments have `shared_cleanup` safety. Such closures deviate from
normal `capture`-passing rules in two ways:
  - **Own `capture`s are downgraded:** In the example, you will note that `n1` is
    of type `after_cleanup_capture<int>`. Whereas, in the absence of `outerScope`,
    the type would be `capture<int>`.
  - **Parent `capture`s are not upgraded:** Suppose the example scheduled a
    closure: `.schedule(async_closure(bound_args{n1}, ...))`.  Then, **inside
    that closure** `n1` would be visible as `capture<int&>` because it can't be
    exfiltrated to `outerScope`.  That's the upgrade behavior[†].  But, when
    passing `bound_args{outerScope, n1}`, the `shared_cleanup` argument blocks
    the upgrade, and the closure would still see `after_cleanup_capture<int&>`.

    > [†] Note that when a closure upgrades its refs, e.g. from
    > `after_cleanup_capture` to `capture`, the safety of the closure's task is
    > **not** affected. That is, reference upgrades are an internal matter.

#### **Why is the downgraded name `after_cleanup_`?**

In short, because such `capture`s can safely be used in `co_return
move_after_cleanup()` and similar constructs.

We need a new `after_cleanup_ref` level because:
  - `after_cleanup_capture<int&>` is safer than a `shared_cleanup` ref, which is
    *not* allowed in `move_after_cleanup` et al.
  - `after_cleanup_ref` is less safe than `co_cleanup_safe_ref`, since we don't
    want the downgraded references to be scheduled on scopes.

### Avoid safety downgrades via `restricted_co_cleanup_capture`

**NB:** This feature isn't fully implemented yet (see "Implementation gaps"
in this doc, and `FutureWork.md`), but what remains is quite simple, just
search the code for "restricted".

In some scenarios -- e.g. passing around a fire-and-forget logger -- it is
important to avoid the safety downgrade.  For example, a closure taking a
`co_cleanup_capture<Logger&>` would be unable to pass any of its own captures to
a `co_cleanup_capture<safe_async_scope&>` that it owns.

To avoid downgrades, pass `restricted_co_cleanup_capture<Logger&>` to the child
closure.  This `capture` uses ADL customization point
`capture_restricted_proxy()` to dereference, returning a proxy object that
**only** accepts inputs with `maybe_value` safety. Obviously, such a proxy
cannot accidentally pass short-lived refs from a child closure to a parent
`co_cleanup_capture`, and thus it needs no downgrades.

**IMPORTANT:** The implementation must pick between one of:
  - "once restricted, always restricted"
  - "restricted->unrestricted also downgrades `capture`s from parents"

What we cannot do is "let a closure take a formerly `restricted` ref as
unrestricted **and** take non-downgraded refs from parents." If both were
possible at once, then the following lifetime safety violation could occur:
  - depth 0: creates a `co_cleanup_capture<S&> x0`
  - depth 1: takes restricted `x1` and owns `capture<int> y1`
  - depth 2: unrestricts `x1` as `x2`, takes ref to `y1` as `y2`
The problem is that short-lived `y2` can now be passed to `x0`. Either
solution above eliminates the safety gap.

### The no-cleanup closure optimization & `_heap` captures

Although `async_closure` was built to support async RAII, it should also see
usage *just* because of `LifetimeSafetyBenefits.md`.

Closures are implemented in such a way that users don't have to choose between
safety and performance. Specifically, an async closure that takes no
`co_cleanup` captures should perform exactly the same as the bare inner
coroutine.

This zero-cost behavior is called the "no-cleanup closure optimization". When
implementing async RAII, it is hard (or perhaps impossible) to avoid allocating
a second coro frame, the one that awaits cleanup. But, when `async_closure` sees
that it owns no `co_cleanup_capture`s, it will:
  - Omit the outer coro frame (which would now be no-op)
  - Move in its own captures into the inner coro.
  - For owned captures that are `make_in_place`, automatically use the
    `capture_heap` variation.

From most perspectives, a no-cleanup closure quacks just like its
outer-coro-awaits-inner-coro cousin. However, its own capture args' signatures
will differ:
  - `capture<Value>` is passed instead of `capture<Value&>`.
  - `make_in_place` captures use the `capture_heap` template.

By design, reference and value, plain and `_heap` captures have identical
interfaces, letting `async_closure` freely pick the storage for those typical
inner coros that take all captures by `auto`.

In the unlikely event of a no-cleanup closure taking lots of `make_in_place`
captures, you can try `async_closure::force_outer_coro` to coalesce allocations.

### Integrating your own `co_cleanup` type

This section assumes you're familiar with `CoCleanupAsyncRAII.md`.

A properly implemented `co_cleanup` type `T` should:

  - Construct `T` in a state that does not yet require cleanup.

  - Be immovable, e.g. derive from `private folly::NonCopyableNonMovable`.

    This prevents lifetime issues, since all our safety checks assume that
    the `T` is cleaned up by its original owner.

  - Restrict public APIs that affect `T`'s need for cleanup to **only** be
    accessible by dereferencing `co_cleanup_capture<T&>`. This guarantees that
    if cleanup is needed, it will be called.

    When `capture` types evaluate `operator*` and `operator->`, they look
    for an ADL customization point. Declare it like so:
    ```cpp
    template <capture_proxy_kind Kind, const_or_not<YourType> T>
    friend auto capture_proxy(capture_proxy_tag<Kind>, T&);
    ```
    This should return a proxy type implementing your public API appropriate
    for both `Kind`, and the `const`-qualification of `T` -- `forward_like`
    may be useful when accessing members of `T`.  The proxy type should be
    `NonCopyableNonMovable` with a constructor restricted to your class.

  - For public APIs, require `lenient_safe_alias_of_v` of at least
    `co_cleanup_safe_ref` for any input that may be stored until cleanup time.

  - If `restricted_co_cleanup_capture<T&>` support is desired, ADL-customize
    `capture_restricted_proxy()` as above, and enforce that all API inputs have
    `lenient_safe_alias_of_v` of `maybe_value`.

### How many templates are in this type zoo? Can't you type-erase?

Look at `is_any_capture`. There are 8 as of this writing. Only the 3
`after_cleanup_` ones are specific to lifetime-safety tracking.

We deliberately chose distinct templates instead of template parameters, since
this should result in more readable compiler errors.

Type-erasure isn't a very practical idea for simplifying the type signatures.
There are two reasons:
  - `folly/coro/safe` should be zero-cost so teams can adopt it confidently.
  - We shouldn't type-erase the lifetime safety level, and at present 4
    different ones need to be distinguished.

Shrinking the capture type zoo from 8 to 4 doesn't seem worth the runtime &
complexity cost of type erasure.

### Why do `capture`s quack like pointers?

Morally, they are either values or references, and the "pointer-like" UX hurts
ergonomics.

Let's consider the "no wrapper" alternative.  It would be possible for
`as_capture()` args to `async_closure` to just pass a reference to the
underlying type into the inner coro.  This comes with many downsides:
  - Bug farm: The inner coro has to remember to write `auto&` / `ActualType&` --
    **except** when you have the no-cleanup closure optimization. Pass-by-value
    for non-cleanup captures **will** compile, making a copy, but the runtime
    behavior is wrong. This is fragile enough that I wold abandon the no-cleanup
    optimization in this scenario.
  - Clarity: The inner coro signature doesn't distinguish between captures and
    plain arguments, even though they have important lifetime differences (only
    capture refs can be passed into async scope tasks, e.g.).
  - Raw reference: no lifetime safety checks are possible.
  - To prevent `co_cleanup` types from being used in unmanaged contexts, we'd
    have to use the "passkey constructor" pattern, which is less flexible than
    the `capture_proxy` design.

While [C++20 lacks a good story for generic reference-like wrapper
types](https://medium.com/@snowp/transparent-phantom-types-in-c-de6ac5bed1d1),
per above, the uglier dereferenceable wrapper style provides benefits that far
outweigh the syntactic boilerplate.
