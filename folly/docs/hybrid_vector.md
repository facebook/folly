`folly/container/hybrid_vector.h`
---------------------------------

`folly::hybrid_vector<T>` is a fixed-capacity vector backed by stack-or-heap
storage (via `folly::HybridAlloc`). The capacity is chosen at construction time
and never grows.

Simple usage example:

```cpp
    FOLLY_HYBRID_VECTOR(int, vec, 256);
    vec.push_back(42);
    vec.push_back(43);
    assert(vec.size() == 2);
    assert(vec.capacity() == 256);
```

### Motivation
***

`std::vector` uses the heap exclusively, which adds allocation overhead for
small, short-lived buffers. `std::array` and stack arrays are fixed-size but
cannot fall back to the heap when the required capacity is unknown at compile
time.

`hybrid_vector` bridges this gap with a **stack-first** strategy: storage lives
on the stack when the requested size fits a configurable threshold (default
1024 bytes), and seamlessly switches to heap allocation when it exceeds that
threshold. This is especially useful for:

* **Runtime-sized temporary buffers** in algorithms like parallel radix sort,
  where the number of threads is known only at runtime and per-thread bucket
  storage may be small (fits stack, e.g. `Bits8` / 256 entries) or large
  (needs heap, e.g. `Bits16` / 65536 entries).

* **Fixed-capacity containers** in performance-sensitive code paths where
  avoiding a heap allocation in the common case matters.

* **Scoped allocations** that are destroyed at scope exit without an explicit
  free call (stack allocations are automatically reclaimed when the scope
  ends).

### API Overview
***

`hybrid_vector<T>` models a contiguous container similar to `std::vector` but
with a fixed capacity. All member functions that would grow the vector beyond
its capacity throw `std::bad_alloc`.

**Element access:** `operator[]`, `at()`, `front()`, `back()`, `data()`

**Iterators:** `begin()`, `end()`, `cbegin()`, `cend()`, `rbegin()`, `rend()`,
`crbegin()`, `crend()`

**Capacity:** `empty()`, `size()`, `capacity()`, `max_size()`, `reserve()` (no-op
if within capacity, throws otherwise), `shrink_to_fit()` (no-op)

**Modifiers:** `push_back()`, `emplace_back()`, `pop_back()`, `clear()`,
`reset()`, `resize()`, `assign()`, `insert()`, `emplace()`, `erase()`, `swap()`,

**Conditional insertion** (return `nullptr` when full):
`try_push_back()`, `try_emplace_back()`

**Unchecked insertion** (requires the caller to guarantee space):
`unchecked_push_back()`, `unchecked_emplace_back()`

**Range operations:** `append_range()`, `try_append_range()`,
`unchecked_append_range()`, `insert_range()`, `assign_range()`

**Conversion:** `to_array()` (with optional fill value),
`to_array_exact()` (requires exact size match)

**Persistence:** `to_persistent()` converts stack-backed storage to the heap so
the vector can be moved or outlive its declaration scope.

### Declaration Macros
***

`hybrid_vector` can be declared using one of several macros. All allocate the
storage in the caller's stack frame and produce a local variable.

| Macro | Description |
|-------|-------------|
| `FOLLY_HYBRID_VECTOR(T, name, capacity)` | Natural alignment, default threshold |
| `FOLLY_HYBRID_VECTOR_THRESHOLD(T, name, capacity, threshold)` | Custom stack threshold (bytes) |
| `FOLLY_HYBRID_VECTOR_ALIGNED(T, name, capacity, alignment)` | Explicit alignment |
| `FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(T, name, capacity, alignment, threshold)` | Full control |

Example:

```cpp
    // 256 ints, natural alignment, stack up to 1024 bytes
    FOLLY_HYBRID_VECTOR(int, vec, 256);

    // 65536 ints, aligned to 64 bytes, stack threshold 4096
    FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(int, big, 65536, 64, 4096);
```

A heap-only vector can also be created programmatically:

```cpp
    auto vec = folly::hybrid_vector<int>::persistent(1024);
```

### Storage Model
***

Each allocation carries a `HybridAllocHeader` that records:

* **Marker** — distinguishes stack (`0xCCCC` / `0xEEEE`) from heap
  (`0xDDDD` / `0xFFFF`) and detects double-frees in debug builds.

* **Raw pointer** — the base address for `sizedFree()` (heap) or `nullptr`
  (stack).

* **Allocation size** — total bytes including header and padding, used for
  sized deallocation.

Stack allocations use `FOLLY_ALLOCA` (`alloca` / `_alloca`) and are reclaimed
when the enclosing scope exits. Heap allocations use `malloc` and are freed
through `sizedFree()`.

Because stack storage is tied to the call frame, moving a stack-backed
`hybrid_vector` is not allowed. Call `to_persistent()` first to obtain a
heap-backed copy that can be moved freely.

### Custom Allocator
***

`folly/memory/HybridAlloc.h` provides the underlying `FOLLY_HYBRID_ALLOC*` /
`FOLLY_HYBRID_ALIGNED_ALLOC*` macros and `hybridFree()` / `hybridMarkPersistent()`
helpers. These can be used independently of `hybrid_vector` for any
stack-or-heap allocation pattern.

### Thread Safety
***

`hybrid_vector` is not thread-safe. Concurrent reads from multiple threads
are safe, but any concurrent write requires external synchronization.
