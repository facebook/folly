# Binding API inputs to storage with `folly/lang/bind/`

## User guide

Are you trying to call a `folly::bind`-enabled API?  For simple usage,
you should not need to read this file at all!  Read the API docs instead.

NEVER write functions that pass `folly::bind` helper types (`constant`,
`const_ref`, `args`, etc) by reference.  These immovable objects must
only exist in the statement that constructed them.

The high-level idea of `folly::bind` is to offer **the caller** a
vocabulary to describe the storage types to be used by the callee.

For example, if a callee wants to store a generic tuple, the caller may
write `constant{5}, const_ref{b, c}, mut_ref{d}` to define the storage as
`std::tuple<const int, const int&, const int&, int&>{5, b, c, d}`.

Your specific API's docs are authoritative -- a callee can change modifier
meanings, or define new ones.  That said, suggested modifier semantics are:
  - Pass non-movable, non-copyable types via `bind::in_place<T>()` or
    `bind::in_place_with(fn)`.
  - The API decides whether `const_ref` / `mut_ref` are supported.
    If NOT, then the callee's signature decides between by-value & by-ref.
      * Using `const_ref` etc should cause a compile error (`static_assert`).
    If YES, they tell the callee to take the argument by reference. Then:
      * WATCH OUT: `const_ref` / `mut_ref` is explicit unlike regular C++.
        Rationale: Programming best practice is to minimize "implicit"
        mutation.  When writing modern C++, two argument passing styles
        predominate: `T` -- you own this, and `const T&` -- a read-only
        input.  Any mutable "output" references, like `T&`, ought to be
        plainly visible at the callsite to avoid bugs, and `mut_ref` is.
      * Passing a variable without modifiers will be pass-by-value.
  - Unlike `std::as_const`, which changes the `const`ness of the input,
    `bind::constant` / `bind::mut` say whether the *destination* is `const`.
    So, `constant` is like `const T var` in the callee's signature. E.g.
      * `constant(std::move(var))` moves in the value, and stores it as `const`.
      * `mut` never removes a `const` qualifier from the underlying
        data.  Rather, it can override the "references default to `const`"
        behavior, or to override a `constant` modifier that it surrounds.

## Library authors only

### What is this for?

You are developing a new API, and need your caller to ergonomically pass a
list of arguments to be stored in your callee code.  Today's options are:
  - Pass regular arguments
  - Pass a struct
  - Pass a tuple, potentially as `auto`
  - Pass a callable that constructs a tuple or a struct

These options have tradeoffs, but none provide ALL of these features:
  - Easy in-place construction
  - Let the caller set the target storage (value or ref, const or not) for
    the binding, with the callee just specifying just `auto`.  Consequences:
      * The callee can reflect on the supplied args, without C++26 P2996
      * `folly::bind::constant` lets you move a non-`const` object into
        `const` storage, while `std::as_const` cannot.
  - Define custom binding logic for some args, like `as_capture` in
    `async_closure`, or named arguments in `folly/lang/named`.
  - Allow helper functions to return several adjacent arguments -- as if
    your helpers could return an argument pack, without needing to pack them
    in tuples & having the consumer recursively `std::tuple_cat`.

`folly::bind` addresses all of the above.  It is a customizable tool to
uniformly bind:
  - input references & in-place constructors
  - to reference or value storage inside your API.

### When might you use it?

Consider this `folly::bind` expression:

  bind::args ba{5, constant{a}, mut_ref{b, std::move(c)}, const_ref{d}};

Stored with the default `bind_to_storage_policy`, this is akin to:

  std::tuple<int, const A, B&, C&&, const D&> tup =
      std::forward_as_tuple{5, a, b, std::move(c), d};

Regular C++ arguments are fine (and preferred!) when the destination types
are known in advance, and the types are movable.  But, in trickier cases
`folly::bind` saves the day:
  - It lets the caller ergonomically declare a structure at the same time it
    is constructed or passed (`async_closure`, named scopes).
  - In order to in-place construct an immovable type `A` by-value inside the
    caller's storage, you need an implicit conversion operator.
    `bind::in_place*` implements one on your behalf.
  - API-specific binding customizations can be provided via helper classes.
    For example, `folly/lang/named` enables kwargs support.

### What changes in the UX over standard C++ bindings?

That depends on how you integrate `folly::bind`, but...  the recommended
way is to use the standard `bind_to_storage_policy`, possibly extending it in a
careful way.  The goal should be "low user surprisal", so we stay close to
standard C++ semantics, except for a couple of restrictions to make argument
passing less bug-prone.

With the vanilla `bind_to_storage_policy`, besides `bind::in_place*` support, you get:
  - Arguments are bound by value, unless the callsite includes `const_ref` /
    `mut_ref`.  This prevents bugs from callers not expecting aliasing.
  - While `copy{}` and `move{}` modifiers aren't yet provided, `category_t`
    and `bind_to_storage_policy` allow for the notion of the caller restricting that
    an argument be passed by copy, or move-copy.

In "synchronous" APIs, where your code only runs while the user's original
statement is active, another viable integration is to define a policy that
defaults to bind-by-reference.  This policy could be added to `Bind.h`.

### To integrate `Bind.h`, take `bind::args` via CTAD in an immovable class

The simple way to make a `folly::bind` API is to take one `bind::args`:

```cpp
template <typename T>
void foo(bind::args<T> args) {
  // Read "Using your bound args" for how to access `args`
}
// User code
foo(bind::args{5, constant(bind::in_place<Bar>(x), std::move(y))});
```

#### Alternative: API with no user-visible `bind::args`

If the one-`bind::args` pattern does not work for you, there's another way.
Before going down this road, read this section *carefully*.  Needing to depend
on CTAD limits your template deduction capabilities, and forces you to
implement each API method as a class.

In `Bind.h`, all the modifiers (like `constant`) look similar to this:

```cpp
template <typename... Ts>
class YOUR_TYPE : private bind::args<Ts...> { // (1)
  // (2) -- the ctor must take `bind::args<Ts>...` **by value**.
  using bind::args<Ts...>::args;
};
template <typename... Ts>
YOUR_TYPE(Ts&&...) -> YOUR_TYPE<bind::ext::deduce_args_t<Ts>...>; // (3)
```

Your API's implementation could follow a similar pattern, so you should
understand how (1-3) work together to bind the input arguments.  Afterwards,
I will describe how to add your business logic.

(1) Deriving from a `NonCopyableNonMovable` base (`bind::args`) encourages
users to pass the outer type **only** via prvalue semantics.  This is
required in order to minimize lifetime bugs.  These "binding" types
typically store references, which (thanks to lifetime extension) are valid
in the current statement, but can easily become invalid later.  Insistiing
on pass-by-prvalue makes it harder for the user to accidentally write
lifetime bugs.  Why go this far to prevent pass-by-move?  Implementation
experience shows that assigning a binding type to a named variable feels
"natural", but using it later can cause bugs that are hard to spot.  For one
example, see the `#if 0` in the test under `all_tests_run_at_build_time`.

Notes:
  - `private` inheritance prevents `YOUR_TYPE` from being nested inside
    binding modifiers like `constant()`.  You can relax to `public`, but
    only if your type has the inherited ctor shown above, and can logically
    be thought of as a bag of args.
  - A linter is proposed, but not yet implemented, for detecting cases
    where `Bind.h` helpers are being taken by-reference.  With this linter, the
    prvalue-only protection against lifetime bugs will be robust.

(2) Prvalue semantics only allows us to take arguments by-value.  C++20
lacks perfect forwarding for prvalues, and there is not even an accepted
proposal for future releases (though P2785 would help).  Yet, in generic
code, we want to allow packs of arguments where some are binding helpers
(like `constant(5)`) and others are perfect-forwarded references (like `x`).
Without prvalue perfect forwarding, the next best trick is to implicitly
convert every arg to a `bind::args<T>` value, as done by this ctor.
  - If the argument type `T` derives from `bind::ext::like_args`, we wrap it in
    `struct args<T> : T`, moving the underlying data via the
    implementation-detail `unsafe_move_args` protocol.  This handles the case
    when the user passes a modifier like `constant(5)` as an arg.
  - For all other `T`, `args<T>` simply captures a forwarding
    reference to the input.

IMPORTANT:
  - To be lifetime-safe, this ctor takes the `args<Ts>` by-value.
  - Do **NOT** assume that `Ts` are your input types.  A single `args`
    may represent a sequence of arguments to bind (e.g.  `constant(5, x)`,
    and the base class `args<Ts...>` takes care of flattening these
    for you.  The usage is explained below.

(3) The deduction guide is necessary so that the class's ctor (2) can
implicitly convert each argument into a `args<T>` value.  In other
words, it arranges for `T` to record the type of the argument as provided.

Once the base `bound_arg<Ts...>` is constructed, you can access the
**flattened** bound args:
  - The "input" and "storage" types for each argument via the class template
    `bind_to_storage_policy`, and member type list `binding_list_t`.
  - The argument-to-store via the `unsafe_tuple_to_bind()` method.  Each
    tuple entry is either a reference, or a type that is implicitly
    convertible to the storage type.  In order to use this value, you
    **must** coerce each entry to the policy-computed storage type.  The
    method's `unsafe_` prefix signals that it is only lifetime-safe when
    called within the statement that instantiated your class.

#### Using the bound args in your business logic

There are two natural choices for **when** to run your business logic:

  - With the immovable `YOUR_TYPE` shown above, you can think of
    the type as an ephemeral "list of bound args", and provide one or more
    methods (or even `operator()`) to operate on the args.  The methods
    should be `&&`-qualified (i.e. destructive) to prevent binding reuse.
    For example, `AsyncClosure.h` follows this pattern.

  - If `YOUR_TYPE` can represent or store the API's result, you could run
    the business logic in the ctor directly.  This is not a commonly
    recommendable pattern, since error handling gets trickier (either your
    class models both value & error states, or your ctor throws).  However,
    it can give a simpler UX.  Implementing this requires some adjustments:
      - Stop inheriting from `args`, since you would no longer want to
        store the bound args tuple, but only the API's result.
      - Instead, lightly refactor `Bind.h` to expose the argument-flattening
        logic by composition.
      - So long as it doesn't store any input references, `YOUR_TYPE` can
        now be movable or copyable.

Here is a method for `YOUR_TYPE` that stores the args as a tuple of values:

```cpp
auto store_tuple() && {
  return [&]<typename... Bs>(tag<Bs...>) {
    return std::tuple<bind_to_storage_policy<Bs>::storage_type...>{
      std::move(*this)->unsafe_tuple_to_bind()};
  }(args<Ts...>::binding_list_t{});
}
```
