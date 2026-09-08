## USER (2026-08-06T22:46:37.326Z)

Let's discuss the contract without editing it yet. The current review is in
`working-review.md`.

- it uses zero as absence: is this in any way required by the implementation --
  I was imagining that the metadata frames don't care what uint64_t they
  contain, so why is this modeling absence at all?

- what is "the raw marker index"? explain this issue in plain langauge /
  mechanistically.

- "exposes every nested scope rather than one effective value" -- why is this a
  problem? IIUC the minimal contract was "give me the top / currently active
  value", but is it bad to expose more here? Is this a cost concern, Hyurum
  concern, or pedantry?

- "That API should not become the external contract by accident" -- are you
  talking about the 3 points above, or something else too?

## ASSISTANT (2026-08-06T22:47:21.132Z)

No doc edits. The important distinction is:

1. **Carrier:** metadata frame in the parent chain.
2. **Capture API:** D115061390’s parallel address/metadata arrays.
3. **Profiler storage:** not implemented yet.

The concerns are mostly about layer 2, not the carrier.

**1. Zero as absence**

Nothing in the frame representation requires this. Marker presence identifies
metadata; the payload may be any `uint64_t`, including zero.

The ambiguity comes solely from D115061390:

```text
addresses:   [leaf, tagged_frame, parent]
metadata:    [0,    value,        0]
```

It fills untagged slots with zero and tells callers to find the first nonzero
slot. Consequently, `value == 0` disappears.

A single optional `{value, index}` or an explicit presence bit fixes this. The
carrier itself is fine.

**2. Raw marker index**

Mechanically, the linked chain might be:

```text
raw link 0: leaf
raw link 1: wrapper frame
raw link 2: metadata marker(value = 42)
raw link 3: parent
```

D115061390 removes the marker and reports:

```text
addresses: [leaf, wrapper, parent]
metadata:  [0,    42,      0]
```

So it reports metadata against the wrapper at filtered index 1, rather than
explicitly saying “the marker was raw link 2.”

The raw marker index anchors the exact scope boundary before synthetic frames
are filtered or downstream formatting changes the stack. However, it is not
intrinsically needed for simple sample attribution. If all we need is “current
value is 42,” no index is necessary. If associating metadata with a stack
position matters, we should decide whether the meaningful position is the marker
or the tagged real frame.

The report treated raw marker index as settled. It is not required by the
carrier design.

**3. Exposing every nested scope**

This is not inherently incorrect. For:

```text
inner(value = 9) -> outer(value = 4)
```

the array may expose both values. A consumer can select the first present value
and obtain the effective value, `9`.

The concerns are:

- **Semantics:** a careless profiler could attribute the sample to both scopes.
- **Cost:** full per-frame metadata is O(stack depth), versus one optional
  scalar result. This matters more if copied into BPF events or heap-profiler
  storage than in the opt-in C++ walker.
- **Hyyrum’s Law:** once public callers consume full ancestry, Folly must
  preserve it even if the intended facility only needs one effective value.

So I would characterize this as a deliberate API expansion, not a correctness
defect. If full ancestry is useful and affordable, exposing it is reasonable.

**4. “External contract by accident”**

I meant those three choices plus their packaging:

- zero means absent;
- markers are removed and values are assigned to real frames;
- every nested value is exposed;
- results use parallel per-frame arrays.

D115061390 is already a public C++ API. If the heap and sampling profilers adopt
that shape directly, those choices become difficult to change. I was not
referring to the underlying metadata frame or `co_withMetadata()` contract.

## USER (2026-08-06T22:51:46.140Z)

2. Can you explain what you would RATHER have the API do? Instead of the
   (address, metadata) list-of-pairs model you showed?

## ASSISTANT (2026-08-06T22:52:17.768Z)

Under the currently recorded contract, I would return the ordinary stack plus
one optional effective-metadata record:

```cpp
struct AsyncStackTraceMetadata {
  uint64_t value;
  size_t rawMarkerIndex;
};

ssize_t getAsyncStackTraceSafe(
    uintptr_t* rawAddresses,
    size_t capacity,
    std::optional<AsyncStackTraceMetadata>& metadata);
```

For:

```text
leaf -> wrapper -> marker(value=0) -> parent
```

it would produce:

```text
rawAddresses = [leaf, wrapper, marker-cookie, parent]
metadata = {value: 0, rawMarkerIndex: 2}
```

No marker means `metadata == nullopt`. Nested markers return only the first,
innermost one. A presentation step can then remove marker cookies before
symbolization.

This is preferable because:

- Presence is separate from the value, so zero works.
- The API directly expresses “one effective value.”
- Storage is O(1), rather than one `uint64_t` per stack frame.
- Full nested ancestry does not accidentally become supported API.
- The index explicitly identifies the scope boundary.

I would probably give this raw-capture API a distinct name, rather than make an
overload unexpectedly return different address contents.

If we conclude that model attribution does **not** need a stack position, I
would simplify further: return the filtered address array plus
`optional<uint64_t> effectiveMetadata`. That is my preference for the minimal
attribution requirement; the index is only justified by a real frame-association
requirement.

## USER (2026-08-06T22:53:52.278Z)

Ok but if we assume that we want to render the entire stack, plus metadata per
frame, then what would you do for 2?

## ASSISTANT (2026-08-06T22:54:15.165Z)

Then I would drop the raw marker index requirement. The marker is an internal
encoding, not part of the rendered stack.

Return displayed frames with explicit optional metadata:

```cpp
struct RenderedAsyncFrame {
  uintptr_t address;
  std::optional<uint64_t> metadata;
};
```

For the raw chain:

```text
leaf
inner-owner
marker(9)
outer-owner
marker(4)
parent
```

return:

```text
leaf          none
inner-owner   9
outer-owner   4
parent        none
```

The walker removes each marker and attaches its payload to the real frame
directly above it. Zero works because `optional{0}` differs from `nullopt`. The
effective value is the first present value when scanning leaf to root.

This is essentially the patch's per-frame model, with explicit presence instead
of zero as a sentinel. If the UI wants effective metadata repeated on every
enclosed frame, derive that while rendering; I would keep capture sparse.

Only if the marker itself must appear as a boundary row would I return typed
entries such as `variant<CodeFrame, MetadataBoundary>`. I would not expose a
fake address plus a raw index.

## USER (2026-08-06T22:56:35.624Z)

Pivoting to other design / contractual issues -- the main one seems to be "no
profiler transport" which I assume is the same as "the sampling profiler does
not capture metadata".

Is that the main thing to discuss?

Can you explain that plainly / mechanistically too?

## ASSISTANT (2026-08-06T22:57:00.326Z)

Yes. Once the desired output shape is settled, profiler transport is the main
missing piece.

**Sampling-profiler BPF path**

Suppose the target process contains:

```text
frame A -> marker(value=42) -> frame B
```

The BPF walker currently:

1. Reads each frame’s return address.
2. Emits something like:

```text
[address(A), marker-cookie, address(B)]
```

3. Does **not** read `value=42`.
4. D114735088 removes the marker cookie in userspace:

```text
[address(A), address(B)]
```

At that point `42` is irretrievably gone. Userspace received neither the payload
nor the address of the marker object from which it could read the payload.

A complete transport path must:

1. Recognize the marker while BPF still has its frame pointer.
2. Read the adjacent `uint64_t`.
3. Put its presence, value, and frame association into the BPF event.
4. Decode that into the sampling-profiler sample.
5. Preserve it through symbolization and output.

If we want metadata for every tagged frame, the event needs a sparse list such
as `{frameIndex, value}` entries, or equivalent per-frame presence/value arrays.

**Heap-profiler path**

The heap profiler has a parallel gap. At allocation time it still calls the
address-only walker and aggregates using the resulting stack ID.

Therefore:

```text
allocation under value 42 + stack S
allocation under value 99 + stack S
```

can be merged into the same aggregate for stack `S`. Once merged, later display
code cannot separate their memory usage.

The heap profiler must capture metadata at allocation time and include it in the
aggregation identity, then store it in the versioned shared format read by the
sampling profiler.

So the central discussion is:

- What metadata representation crosses the profiler boundary?
- How is it associated with rendered frames?
- How is presence represented so zero works?
- How does the heap profiler include it in aggregation?

The bottom-marker root-stitching bug is separate and locally repairable. The
transport gap is the part that currently prevents the feature from accomplishing
its original purpose.

## USER (2026-08-06T23:42:14.498Z)

For std::optional, the main reason to avoid it was to keep the BPF simple.
However, IIUC we could do this, and have a fixed layout, solving the BPF issue:

class Metadata { uint64_t value_; bool hasValue_{1}; public:
std::optional<uint64_t> get() const { ... } };

My q is -- are there any cost implications to doing that?

## ASSISTANT (2026-08-06T23:42:57.588Z)

A custom fixed-layout type works, and `get()` returning `std::optional` has
negligible CPU cost and no allocation. But it only solves the **presence
encoding**, not transport itself: BPF must still read and emit the fields.

The main cost is layout:

```cpp
struct Metadata {
  uint64_t value;
  uint8_t present;
  uint8_t reserved[7];
};
static_assert(sizeof(Metadata) == 16);
```

On 64-bit systems, the flag plus padding makes this 16 bytes, versus 8 bytes for
the value alone.

- One result per sample: negligible.
- One object per active metadata scope: roughly 8 extra bytes, possibly an
  allocator size-class bump.
- One slot per 127-frame trace: about 2 KiB instead of 1 KiB for the metadata
  sidecar, increasing BPF event bandwidth and userspace cache traffic.

Use `uint8_t`, explicit padding, and `offsetof` assertions rather than `bool`
for a BPF/shared ABI.

For the current raw BPF walk, an even cheaper representation exists: the marker
cookie already supplies presence.

```text
addresses[i] == marker-cookie  => metadataValues[i] is present
metadataValues[i]              => may contain any uint64_t, including zero
```

BPF can emit a fixed `uint64_t metadataValues[MAX_DEPTH]`; userspace converts
marker slots into `optional<uint64_t>` annotations on real frames and removes
the cookies. That costs 8 bytes per raw frame and needs no separate presence
flag.

So your representation is viable. I would use it for an already-filtered
per-frame ABI; for the raw BPF event, I would use the cookie itself as the
presence bit. The remaining verifier risk is the extra per-frame reads and
writes, not `std::optional`.

## USER (2026-08-06T23:48:49.098Z)

Let's prepare a narrower-scope review, and do a new-reader / plain-language pass
over it. The bar for that is -- a reader who's not familiar with the earlier
design notes or other async-stack details should be able to follow the
mechanism. We're doing something fairly simple, so the explanation and words
should be simple to follow.

The scope reductions are:

- Accept that the new API returns the richer [(fn, metadata), ...]

- Resolve the "0 metadata is reserved" via the Metadata-modeling-optional,
  below. This ONLY applies to the in-process readers IIUC.

- Yes, it's expected that we don't have heap- or sampling-profiler transport in
  this stack yet. The review target is -- would anything here block adding
  heap-profiler support on top (that's the primary goal)?

The review should also check that this builds and works on 32-bit and 64-bit
platforms, Windows and macOS, and libc++ and libstdc++. I don't anticipate
problems, but do flag real issues if you find any.

The one 32/64-bit question we don't know the answer to is whether 32-bit clients
would need 64-bit metadata. This could go both ways. Right now, I'm considering
doing something like this to procrastinate on this issue. If this approach is
low-cost and effective, add that recommendation to the easy fixes section of the
review:

```cpp
class Metadata {
  uintptr_t data_;

 public:
#if 64-bit
  static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
  std::optional<uintptr_t> get() const { ... }
#elif 32-bit
  // The goal is to defer deciding whether 32-bit platforms store uint64_t.
#error "TODO: define contract: uint64_t or uintptr_t?"
#endif
};

```

For easy fixes, retain the content, but put it into a separate .md (xref'd from
the main review) -- and include more detail / content / patches where
appropriate. This bit should be sub-agentable. The added detail should prevent
confusion when the implementer applies the fixes.

## ASSISTANT (2026-08-06T23:54:01.112Z)

The first portability result is concrete: the proposed wrapper is cheap on
64-bit, but an unconditional 32-bit `#error` in `AsyncStack.h` is not. It would
break unrelated Folly users merely by including a core header. The narrow review
will recommend either retaining the existing `uint64_t` storage on 32-bit or
consistently compiling the metadata feature out there; it will not recommend a
global build failure.
