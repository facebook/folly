## Future: In-place enrichment protocol

Currently, enrichment effectively adds a node to a dynamic linked list, costing
an `std::exception_ptr` (eptr) allocation+free (~60ns). Moreover, eptr allocates
120-160 bytes of `__cxa_exception` storage, which enrichment cannot use.

In `future_ideas.md`, we discuss bit state for storing errors without the
`std::exception_ptr` memory overhead, but having a heap allocation per
enrichment would still cost tens of nanoseconds.

In-place enrichment is an idea to amortize the allocation cost, by having rich
errors reserve "in-place" storage for an array of enrichment frames. Unlike
`enrich_non_value`, most enrichments only need to track source location &
message, so the storage can be a fixed-size array of 16-byte `rich_msg`s.

This optimization will grow in importance when we support automatic enrichment
in `result_promise::unhandled_exception()`.

### Protocol contract

The core of the implementation is this protocol:

```cpp
bool rich_error_base::maybe_enrich_in_place(rich_msg&&) noexcept;
```

This returns `true` iff the rich error is mutable, and was able to move
`rich_msg` into its storage array. The `rich_msg` is unchanged on `false`.

Given this protocol, `enrich_non_value` would be extended to try these 2 things
in turn. If either fails, it falls back to dynamic enrichment:
  - First, try *only* a fast, RTTI-free query for `rich_error_base`.
    Allocation can easily be cheaper than `dynamic_cast`.
  - Then, try to `maybe_enrich_in_place()`.

**Note:** Access to `maybe_enrich_in_place()` should be restricted to
`enrich_non_value` via friendship or passkey, preventing API misuse that would
violate the "usage requirements" documented in the thread-safety design.

### Implementers of the protocol

The `detail::enriched_non_value` wrapper would automatically come with multiple
slots to amortize the cost of allocations -- determined by benchmarking. Once we
can allocate errors without eptrs, the "sweet spot" array size will decrease.

Dynamic errors may default to providing some enrichment storage -- and
`rich_error` would provide the array. As needed, we could introduce some way for
user base classes to configure this behavior. For example, a member type or
member variable could opt into more / fewer array slots, or even allow a custom
implementation.

None of the 3 possible instances of immortal errors (constexpr, immutable /
mutable singleton) would return `true` from `maybe_enrich_in_place`. So, the
moment you enrich one, that incurs an allocation.

### Thread-safety design

#### Usage requirements

Today, `enrich_non_value` only takes REP by-value. The caller cannot retain a
reference after the call—they have moved ownership. Meanwhile,
`non_value_result::release_rich_exception_ptr` carries a large warning against
holding references, and `maybe_enrich_in_place()` will be protected.

These invariants mean `enrich_non_value()` has sole ownership of the REP during
mutation, guaranteeing "correct usage":

  - **Correct usage**: The sole owner enriches; any copy calls `lock()` first,
      disabling in-place enrichment for all aliases.
      - *Outcome*: Traces are correct. No data races.

  - **Incorrect usage**: Aliasing REPs across threads without copying.
      - *Outcome*: Traces may be garbled. No UB, no crashes, no tearing.

The design below provides safety even on the "incorrect usage" path at minimal
cost. If a future optimization requires it, the "incorrect usage" guarantees
could be relaxed.

**Performance**: No mutex. Only relaxed/release/acquire atomics.

#### How sharing arises

REP copies share the underlying exception object via `std::exception_ptr`
refcounting -- copying an eptr increments a refcount, not the object. Since the
in-place array lives inside that object, both REPs alias the same mutable
storage. Similarly, `to_exception_ptr_slow()` returns an aliasing eptr.

#### When to lock

REP copy and `to_exception_ptr_slow()` call `lock()` when they detect that the
eptr holds a `rich_error_base` (via the `IS_RICH_ERROR_BASE_masked_q`, avoiding
RTTI). This is the same condition gating `maybe_enrich_in_place()`. Unknown-type
eptrs skip in-place enrichment entirely, so they need no lock.

**Note:** The only reason to `lock()` on `to_exception_ptr_slow()` is that
future rich error enhancements could re-enable RTTI-free optimizations upon
reingesting an "unknown type" `std::exception_ptr` via
`from_exception_ptr_slow()`. This locking could also be moved into
`from_exception_ptr_slow()`.

#### Storage layout

```cpp
static constexpr uint8_t kCapacity = 4;  // tunable
std::atomic<uint8_t> next_slot_{0};
std::array<nullable_rich_msg, kCapacity> entries_{};  // default: null
```

`next_slot_` is both slot counter and lock flag:
  - `< kCapacity`: can enrich in-place
  - `>= kCapacity`: full or locked → fall back to dynamic

#### Core operations

```cpp
void lock() noexcept {
  // Release: entry writes before copy visible to readers who acquire count()
  next_slot_.fetch_add(kCapacity, std::memory_order_release);
}

uint8_t count() const noexcept {
  // Acquire: synchronizes with lock()'s release
  return std::min(next_slot_.load(std::memory_order_acquire), kCapacity);
}

bool maybe_enrich_in_place(rich_msg&& msg) noexcept {
  // Early-out: avoids incrementing after lock, preserving count accuracy
  if (next_slot_.load(std::memory_order_relaxed) >= kCapacity)
    return false;
  // fetch_add atomicity gives each caller a unique slot
  auto slot = next_slot_.fetch_add(1, std::memory_order_relaxed);
  if (slot >= kCapacity)
    return false;
  entries_[slot].set(std::move(msg));  // has release semantics internally
  return true;
}
// Readers: iterate 0..count()-1, call empty() on each, skip nulls
```

**Key invariant — single writer per slot**: `fetch_add` atomicity guarantees
each caller gets a unique slot index. At most one thread ever writes to a
given `entries_[slot]`.

#### Correctness under correct usage

All entry writes happen single-threaded before `lock()`. The release on
`lock()` synchronizes with the acquire on `count()`, so readers see all
entries.

#### Degradation under incorrect usage

The TOCTOU gap between `load` and `fetch_add` may allow stray increments after
lock, garbling the count. Mitigations:
  - `min(next_slot_, kCapacity)` caps the count.
  - `slot >= kCapacity` returns `false` before writing → no out-of-bounds.
  - Null entries are safely skipped → no torn reads (see below).

#### Nullable entries for tear-free reads

`rich_msg` has two fields: `exception_shared_string` (a pointer) and
`source_location` (8 bytes). A 16-byte atomic write is costly on most
architectures. Fortunately, the single-writer-per-slot invariant means each
entry has exactly one writer, so we only need reader-writer synchronization.

**Design**: Use the pointer as a null-indicator with release-acquire semantics.
Introduce `nullable_exception_shared_string`, a variant that wraps its pointer
in `std::atomic`:

```cpp
class nullable_rich_msg {
  source_location loc_;                    // not atomic
  nullable_exception_shared_string str_;   // contains std::atomic<char*>

public:
  constexpr nullable_rich_msg() = default;  // null: str_.ptr_ == nullptr

  void set(rich_msg&& msg) noexcept {
    loc_ = msg.source_location();
    // Release: loc_ visible to acquirer who sees non-null
    str_.ptr_.store(msg.str().ptr_, std::memory_order_release);
  }

  bool empty() const noexcept {
    return str_.ptr_.load(std::memory_order_acquire) == nullptr;
  }

  // Precondition: !empty()
  folly::source_location source_location() const noexcept { return loc_; }
};
```

**Why no tearing**: The writer stores `loc_` (ordinary write), then
release-stores the pointer. The release creates a happens-before edge: any
reader that acquire-loads non-null is guaranteed to see the complete `loc_`.
Null entries are never read.

**Incorrect usage remains safe**: Even with aliased REPs, each slot has at
most one writer (per the key invariant). Concurrent reads on different slots
are independent. The only risk is a garbled count → readers check extra
slots → null entries are skipped.
