# Scoped coroutine metadata

Scoped coroutine metadata attaches an opaque machine-word value to a
`folly::coro::Task` and the work that it awaits. It is intended for attributing
an asynchronous stack to a request, tenant, job, or another application-defined
identity without passing that value through every function.

The value is diagnostic context, not coroutine-local storage. Code should not
use it to control program behavior. Its observable representation is the async
stack: a metadata-aware stack reader returns ordinary stack frames with sparse
metadata annotations.

## Add a metadata scope

Include `<folly/coro/WithMetadata.h>` and wrap the `Task` whose dynamic extent
should carry the value:

```cpp
folly::coro::Task<Response> handle(Request request) {
  auto response = co_await folly::coro::co_withMetadata(
      request.tracingId(), fetchResponse(request));
  co_return response;
}
```

`co_withMetadata(value, task)` has the same result and exception behavior as
awaiting `task` directly. The metadata scope starts when the wrapper runs and
ends before the wrapper completes. Normal completion, exceptions, and
cancellation all remove the scope and restore the previous async-stack ancestry.

The value type is `uintptr_t`. Every bit pattern, including zero, is a valid
value. Absence is represented separately from the value, and Folly does not
interpret a present value as an address.

The current overload accepts `Task<T>`. It does not accept `now_task`,
`safe_task`, other Task wrappers, or a Task that already has an executor. To run
tagged work on an executor, bind the executor outside the metadata wrapper:

```cpp
co_await folly::coro::co_withExecutor(
    executor, folly::coro::co_withMetadata(value, std::move(task)));
```

Reversing these wrappers is not supported because `co_withExecutor()` no longer
produces a plain `Task<T>`.

`Task` is move-only. Pass a named task as `std::move(task)`; temporary tasks can
be passed directly as in the examples.

## Inheritance and nesting

Metadata follows Folly's logical async-stack parent chain. It therefore remains
in scope across suspension, resumption, and executor changes. A normally awaited
child `Task` inherits the scope because its async frame retains the awaiting
task as an ancestor. Structured fanout that preserves that ancestry, such as
ordinary `collectAll` branches, inherits it as well.

Nested scopes do not overwrite each other. A metadata-aware stack capture
contains both the inner and outer annotations. For a leaf running under an inner
value of `9` and an outer value of `4`, the logical result is:

```text
leaf                         no annotation
frame that introduced 9     9
frame that introduced 4     4
older ancestors             no annotation
```

The effective value for the leaf is the first present annotation when scanning
from the leaf toward the root. This is `9` in the example. When the inner scope
ends, the outer value becomes effective again. Concurrent sibling scopes use
separate async-stack frames, so one sibling's value does not replace another's.

Capture is deliberately sparse. A scope's value is attached to the coroutine
frame that introduced it; it is not copied onto every descendant frame. A
consumer that needs an effective value for every frame can scan from root to
leaf, carrying the active value and replacing it at each present annotation.

## Where inheritance stops

Metadata follows an async-stack edge, not lexical scope or a later join. Work
that starts a new, detached async stack does not inherit the caller's active
metadata, even if the caller eventually waits for that work.

In particular, `AsyncScope::add()` starts its work through a detached barrier
task. That task is rooted at Folly's detached async-stack frame rather than at
the frame that called `add()`. The added work therefore does not inherit
metadata active around the `add()` call, and `joinAsync()` does not reconnect
the ancestry. Tag each added operation explicitly when it needs attribution:

```cpp
scope.add(folly::coro::co_withExecutor(
    executor, folly::coro::co_withMetadata(value, std::move(task))));
```

`CancellableAsyncScope` has the same boundary because it delegates to
`AsyncScope`.

An executor change alone does not create this boundary. The boundary is the
creation of a new async stack whose root is detached from the caller. Other
fire-and-forget or escaping APIs with that behavior likewise do not inherit the
scope.

## Reading metadata

A metadata-aware in-process async-stack capture is conceptually a sequence of
`(address, optional<uintptr_t>)` entries ordered from leaf to root. Address and
metadata outputs have the same length and corresponding indexes. A present
optional may contain zero. An absent optional means that the frame did not
introduce a metadata scope.

Metadata markers used inside the async-stack chain are an implementation detail.
They are not executable frames. Marker-aware readers remove them before
rendering the stack.

`getAsyncStackTraceFromInitialFrame()` is not marker-aware. On a tagged stack,
it can return the marker cookie as a bogus address and count the marker against
the output depth. Code that may observe tagged stacks should not use that path
until it is repaired.

The entry semantics and an optional-valued accessor are settled, but the final
public C++ type names and function signature for metadata-aware capture are not
yet specified. Callers must not use zero as the absence marker.

The capture is bounded by the caller's stack capacity. If truncation occurs
before the first applicable annotation, the sample is unattributed; a consumer
must not substitute an older value or zero. Each active scope also adds an
internal marker node and a wrapper coroutine frame to the physical walk, even
though the marker is removed from the returned stack.

## Observation and profiler support

The contract defines observation through metadata-aware in-process stack
capture, subject to the capture and target limitations described here. The heap
profiler and sampling profiler do not yet transport the metadata value.

- The sampling-profiler path may recognize and remove the internal marker, but
  its event does not carry the value. A sampled stack therefore cannot be
  attributed from that profiler's output.
- The heap profiler captures an address-only stack. Allocations made under
  different metadata values can consequently be merged into one aggregate when
  their address stacks match.

This means `co_withMetadata()` must not yet be used as a promise of heap- or
sampling-profiler attribution. Adding those paths requires value and presence to
survive capture, and requires heap aggregation to include metadata in its
identity before samples are combined.

The async-stack link is published for observation by the executing thread, a
handler interrupting that same thread, or an external observer while the target
thread is stopped. Reading another running thread's chain concurrently with
updates is outside the contract.

## Portability

The API exists only when Folly coroutine support is enabled.

On 64-bit targets, metadata-aware readers expose the full `uintptr_t` value
through an optional-valued getter. On 32-bit targets, Folly remains buildable
and the metadata representation uses a 32-bit `uintptr_t`, but the getter is
omitted. This intentionally leaves 32-bit observation semantics unspecified
until there is evidence that users need either 32- or 64-bit payloads there.
Targets whose pointer width is neither 32 nor 64 bits are unsupported.

The public wrapper and value semantics are intended to be independent of the
operating system and C++ standard library. Availability of stack capture still
depends on Folly's coroutine and async-stack support for the target. Metadata
capture has not been validated across every Windows, macOS, libc++, and
libstdc++ configuration.

The raw marker is not yet a portable external-reader ABI. Its cookie has not
been shown to be an impossible instruction address on every target, existing
LLDB recognition is 64-bit-specific, and no versioned mechanism tells an
external reader which cookie and enclosing-object layout a Folly build uses.
Debuggers and profilers that inspect raw async-stack memory must not assume that
the marker representation is stable across architectures or Folly versions.
