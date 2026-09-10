# Coroutine-scoped metadata

Folly coroutine metadata associates an opaque, pointer-sized value with a
logical region of coroutine work. It is intended for attribution: code can tag
a task, and an in-process async-stack reader can recover that tag alongside the
logical stack.

Include `<folly/coro/WithMetadata.h>` and wrap the task before awaiting or
scheduling it:

```cpp
#include <folly/coro/WithMetadata.h>

folly::coro::Task<Result> operation();

folly::coro::Task<Result> attributed(uintptr_t requestId) {
  co_return co_await folly::coro::co_withMetadata(requestId, operation());
}
```

The metadata value is an opaque `uintptr_t`. Folly stores and reports it but
does not interpret it, manage the lifetime of anything it might identify, or
make it part of coroutine cancellation or error handling. Stable numeric IDs
are therefore usually safer than object addresses.

## Scope and propagation

The metadata scope begins while the task passed to `co_withMetadata()` is
running and ends when that wrapper finishes or is destroyed. The scope follows
Folly's logical async-stack parent chain, not a worker thread or thread-local
variable. Consequently:

- suspension and resumption preserve the metadata;
- moving the awaited chain between executors preserves it;
- ordinary transitively awaited tasks inherit it;
- awaited structured fanout, such as `collectAll`, inherits it in each branch;
- concurrent branches and separately wrapped tasks remain isolated; and
- completion, exception, or cancellation removes the scope and restores the
  surrounding one.

Nested scopes do not overwrite one shared slot. They produce nested boundaries.
The innermost live scope is the effective metadata for work beneath it; after it
ends, the enclosing scope is effective again.

### Awaited work versus a new async stack

Propagation is determined by async-stack ancestry, not by whether application
code eventually waits for the work:

| Operation | Inherits the current metadata? |
| --- | --- |
| Direct or transitive `co_await` | Yes |
| Awaited `collectAll` branch | Yes |
| Executor change within the awaited chain | Yes |
| `AsyncScope::add()` / `addWithSourceLoc()` | No |
| `CancellableAsyncScope` work started through its underlying scope | No |
| Other detached work started on a new async stack | No |

`AsyncScope::add()` currently starts a `DetachedBarrierTask` at Folly's detached
async-stack root. Its child therefore does not retain the caller's metadata,
even when the same function later calls `joinAsync()`. Joining controls
lifetime; it does not reconnect async-stack ancestry. The same rule protects
against a detached child retaining a pointer to a metadata frame after the
wrapper that owned it has completed.

If newly started work needs attribution, wrap that work with its own
`co_withMetadata()` call.

## Reading metadata from an async stack

The in-process async-stack capture returns the ordinary displayed addresses and
a parallel metadata entry for each address. Metadata marker nodes are an
internal encoding: they are omitted from the address list, and their values are
attached to the real coroutine frame immediately above each marker.

Per-frame metadata has optional semantics:

- an absent value means that the frame does not introduce a metadata scope;
- a present value of `0` is valid and is different from absence; and
- all other `uintptr_t` values, including the maximum value, are valid.

Do not test the numeric value to determine presence. Use the metadata entry's
optional-valued accessor. In particular, code equivalent to
`if (metadata != 0)` is incorrect.

Capture is sparse. A descendant frame inside a scope normally has no annotation
of its own. To find the effective value for the sampled leaf, scan the returned
frames from leaf toward root and select the first *present* metadata value. To
render all nested scopes, retain every present value:

```text
returned frame       metadata
--------------       --------
leaf                 absent
inner wrapper        present(0)
outer wrapper        present(42)
caller               absent
```

Here the effective value is `0`, not `42`. Both scope boundaries remain
available to consumers that render the complete stack.

The synthetic marker address and its position in the unfiltered parent chain
are not public results. Consumers must not depend on the marker cookie, object
layout, metadata offset, or a raw-marker index.

The metadata-aware capture has the same basic result convention as
`getAsyncStackTraceSafe()`: it returns the number of parallel entries written,
zero when no async operation is active, and `-1` on failure. The staged
decisions do not establish the final public spelling or namespace of the
metadata sidecar type and overload. They do establish the per-frame,
presence-preserving behavior above; code should not assume a scalar sentinel or
a sample-level-only result.

## Supported composition

The currently available helper accepts an unbound `Task<T>`. When an executor
must be supplied, put the metadata wrapper inside `co_withExecutor()`:

```cpp
auto scheduled = folly::coro::co_withExecutor(
    executor,
    folly::coro::co_withMetadata(requestId, operation()));
```

Putting `co_withMetadata()` around a task that already has an executor is not
supported. Support for `now_task`, `safe_task`, and other Task wrappers is an
intended extension; their current absence is not a semantic exclusion, but code
cannot rely on those overloads until they are provided.

## Profiler and debugger limitations

Metadata observation is currently an in-process C++ facility. The sampling
profiler does not transport the payload from its BPF stack walk, and the heap
profiler does not capture it at allocation time or include it in aggregation
identity. As a result:

- sampling-profiler output cannot be used to recover coroutine metadata; and
- allocations made under different metadata values may still be merged when
  their ordinary stacks are otherwise identical.

Filtering a synthetic marker out of a displayed profiler or debugger stack is
not metadata capture. Do not infer metadata support merely because such a stack
looks normal. No stable remote-reader ABI for the cookie or enclosing object
layout is part of this contract.

The publication mechanism supports a reader running on the tagged thread,
including interruption of that thread, and an external observer that has
stopped the target thread. Reading a live chain concurrently from another
thread is unsupported.

## Portability

The facility is available only when Folly coroutine support is enabled.

On 64-bit targets, metadata is a 64-bit `uintptr_t`, and the in-process getter
exposes the optional value. A 32-bit Folly build must continue to compile; it
must not fail merely because this header is included. However, the metadata
getter is omitted on 32-bit targets. Folly therefore does not currently promise
metadata readback there, nor does it promise a 64-bit payload on a 32-bit
target. Targets whose pointer width is neither 32 nor 64 bits are unsupported.

The staged material does not establish completed portability validation for
Windows, macOS, libc++, and libstdc++. No stronger platform guarantee for this
facility should be inferred until those configurations are built and tested.

## Cost and limits

Code that does not create a metadata scope does not add metadata state to every
`Task`, executor hop, `AsyncStackFrame`, or `AsyncStackRoot`. Each active
`co_withMetadata()` call does create a wrapper coroutine and an internal marker
in that coroutine frame. Nested scopes add one wrapper and marker apiece.

Stack capture remains bounded. A metadata boundary outside a walker's physical
depth limit cannot be attributed to the sample. This is especially relevant to
future profiler transport, where internal marker nodes may consume traversal
budget even though they are removed from the displayed trace.
