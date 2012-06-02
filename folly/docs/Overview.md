`folly/`
------

### Introduction

Folly (acronymed loosely after Facebook Open Source Library) is a
library of C++11 components designed with practicality and efficiency
in mind. It complements (as opposed to competing against) offerings
such as Boost and of course `std`. In fact, we embark on defining our
own component only when something we need is either not available, or
does not meet the needed performance profile.

Performance concerns permeate much of Folly, sometimes leading to
designs that are more idiosyncratic than they would otherwise be (see
e.g. `PackedSyncPtr.h`, `SmallLocks.h`). Good performance at large
scale is a unifying theme in all of Folly.

### Logical Design

Folly is a collection of relatively independent components, some as
simple as a few symbols. There is no restriction on internal
dependencies, meaning that a given folly module may use any other
folly components.

All symbols are defined in the top-level namespace `folly`, except of
course macros. Macro names are ALL_UPPERCASE. Namespace `folly`
defines other internal namespaces such as `internal` or `detail`. User
code should not depend on symbols in those namespaces.

### Physical Design

At the top level Folly uses the classic "stuttering" scheme
`folly/folly` used by Boost and others. The first directory serves as
an installation root of the library (with possible versioning a la
`folly-1.0/`), and the second is to distinguish the library when
including files, e.g. `#include "folly/FBString.h"`.

The directory structure is flat (mimicking the namespace structure),
i.e. we don't have an elaborate directory hierarchy (it is possible
this will change in future versions). The subdirectory `experimental`
contains files that are used inside folly and possibly at Facebook but
not considered stable enough for client use. Your code should not use
files in `folly/experimental` lest it may break when you update Folly.

The `folly/folly/test` subdirectory includes the unittests for all
components, usually named `ComponentXyzTest.cpp` for each
`ComponentXyz.*`. The `folly/folly/docs` directory contains
documentation.

### Compatibility

Currently, `folly` has been tested on gcc 4.6 on 64-bit installations
of Fedora 17, Ubuntu 12.04, and Debian wheezy. It might work unmodified
on other 64-bit Linux platforms.

### Components

Below is a list of Folly components in alphabetical order, along with
a brief description of each.

#### `Arena.h`, `ThreadCachedArena.h`

Simple arena for memory allocation: multiple allocations get freed all
at once. With threaded version.

#### [`AtomicHashMap.h`, `AtomicHashArray.h`](AtomicHashMap.md)

High-performance atomic hash map with almost lock-free operation.

#### [`Benchmark.h`](Benchmark.md)

A small framework for benchmarking code. Client code registers
benchmarks, optionally with an argument that dictates the scale of the
benchmark (iterations, working set size etc). The framework runs
benchmarks (subject to a command-line flag) and produces formatted
output with timing information.

#### `Bits.h`

Various bit manipulation utilities optimized for speed.

#### `Bits.h`

Bit-twiddling functions that wrap the
[ffsl(l)](http://linux.die.net/man/3/ffsll) primitives in a uniform
interface.

#### `ConcurrentSkipList.h`

An implementation of the structure described in [A Provably Correct
Scalable Concurrent Skip
List](http://www.cs.tau.ac.il/~shanir/nir-pubs-web/Papers/OPODIS2006-BA.pdf)
by Herlihy et al.

#### [`Conv.h`](Conv.md)

A variety of data conversion routines (notably to and from string),
optimized for speed and safety.

#### `DiscriminatedPtr.h`

Similar to `boost::variant`, but restricted to pointers only. Uses the
highest-order unused 16 bits in a pointer as discriminator. So
`sizeof(DiscriminatedPtr<int, string, Widget>) == sizeof(void*)`.

#### [`dynamic.h`](Dynamic.md)

Dynamically-typed object, created with JSON objects in mind.

#### `Endian.h`

Endian conversion primitives.

####`Escape.h`

Escapes a string in C style.

####`eventfd.h`

Wrapper around the
[`eventfd`](http://www.kernel.org/doc/man-pages/online/pages/man2/eventfd.2.html)
system call.

####[`FBString.h`](FBString.md)

A drop-in implementation of `std::string` with a variety of optimizations.

####[`FBVector.h`](FBVector.md)

A mostly drop-in implementation of `std::vector` with a variety of
optimizations.

####`Foreach.h`

Pseudo-statements (implemented as macros) for iteration.

####[`Format.h`](Format.md)

Python-style formatting utilities.

####[`GroupVarint.h`](GroupVarint.md)

[Group Varint
encoding](http://www.ir.uwaterloo.ca/book/addenda-06-index-compression.html)
for 32-bit values.

####`Hash.h`

Various popular hash function implementations.

####[`Histogram.h`](Histogram.md)

A simple class for collecting histogram data.

####`IntrusiveList.h`

Convenience type definitions for using `boost::intrusive_list`.

####`json.h`

JSON serializer and deserializer. Uses `dynamic.h`.

####`Likely.h`

Wrappers around [`__builtin_expect`](http://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html).

####`Malloc.h`

Memory allocation helpers, particularly when using jemalloc.

####`MapUtil.h`

Helpers for finding items in associative containers (such as
`std::map` and `std::unordered_map`).

####[`PackedSyncPtr.h`](PackedSyncPtr.md)

A highly specialized data structure consisting of a pointer, a 1-bit
spin lock, and a 15-bit integral, all inside one 64-bit word.

####`Preprocessor.h`

Necessarily evil stuff.

####`PrettyPrint.h`

Pretty-printer for numbers that appends suffixes of unit used: bytes
(kb, MB, ...), metric suffixes (k, M, G, ...), and time (s, ms, us,
ns, ...).

####[`ProducerConsumerQueue.h`](ProducerConsumerQueue.md)

Lock free single-reader, single-writer queue.

####`Random.h`

Defines only one function---`randomNumberSeed()`.

####`Range.h`

Boost-style range facility and the `StringPiece` specialization.

####`RWSpinLock.h`

Fast and compact reader-writer spin lock.

####`ScopeGuard.h`

C++11 incarnation of the old [ScopeGuard](http://drdobbs.com/184403758) idiom.

####[`SmallLocks.h`](SmallLocks.md)

Very small spin locks (1 byte and 1 bit).

####`small_vector.h`

Vector with the small buffer optimization and an ptional embedded
`PicoSpinLock`.

####`sorted_vector_types.h`

Collections similar to `std::map` but implemented as sorted vectors.

####`StlAllocator.h`

STL allocator wrapping a simple allocate/deallocate interface.

####`String.h`

String utilities that connect `folly::fbstring` with `std::string`.

####[`Synchronized.h`](Synchronized.md)

High-level synchronization library.

####`System.h`

Demangling and errno utilities.

####[`ThreadCachedInt.h`](ThreadCachedInt.md)

High-performance atomic increment using thread caching.

####[`ThreadLocal.h`](ThreadLocal.md)

Improved thread local storage for non-trivial types.

####`TimeoutQueue.h`

Queue with per-item timeout.

####`Traits.h`

Type traits that complement those defined in the standard C++11 header
`<traits>`.

####`Unicode.h`

Defines the `codePointToUtf8` function.
