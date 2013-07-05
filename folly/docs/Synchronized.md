`folly/Synchronized.h`
----------------------

`folly/Synchronized.h` introduces a simple abstraction for mutex-
based concurrency. It replaces convoluted, unwieldy, and just
plain wrong code with simple constructs that are easy to get
right and difficult to get wrong.

### Motivation

Many of our multithreaded Thrift services (not to mention general
concurrent C++ code) use shared data structures associated with
locks. This follows the time-honored adage of mutex-based
concurrency control "associate mutexes with data, not code".
Examples are abundant and easy to find. For example:

``` Cpp

    class AdPublisherHandler : public AdPopulatorIf,
                               public fb303::FacebookBase,
                               public ZkBaseApplication {
      ...
      OnDemandUpdateIdMap adsToBeUpdated_;
      ReadWriteMutex adsToBeUpdatedLock_;

      OnDemandUpdateIdMap limitsToBeUpdated_;
      ReadWriteMutex limitsToBeUpdatedLock_;

      OnDemandUpdateIdMap campaignsToBeUpdated_;
      ReadWriteMutex campaignsToBeUpdatedLock_;
      ...
    };
```

Whenever the code needs to read or write some of the protected
data, it acquires the mutex for reading or for reading and
writing. For example:

``` Cpp
    void AdPublisherHandler::requestUpdateAdId(const int64_t adId,
                                               const int32_t dbId) {
      checkDbHandlingStatus(dbId);
      RWGuard g(adsToBeUpdatedLock_, RW_WRITE);
      adsToBeUpdated_[dbId][adId] = 1;
      adPublisherMonitor_->addStatValue("request_adId_update", 1, dbId);
      LOG(INFO) << "received request to update ad id " << adId;
    }
```

The pattern is an absolute classic and present everywhere.
However, it is inefficient, makes incorrect code easy to
write, is prone to deadlocking, and is bulkier than it could
otherwise be. To expand:

* In the code above, for example, the critical section is only
  the line right after `RWGuard`'s definition; it is frivolous
  that everything else (including a splurging `LOG(INFO)`) keeps
  the lock acquired for no good reason. This is because the
  locked regions are not visible; the guard's construction
  introduces a critical section as long as the remainder of the
  current scope.
* The correctness of the technique is entirely predicated on
  convention. There is no ostensible error for code that:

    * manipulates a piece of data without acquiring its lock first
    * acquires a different lock instead of the intended one
    * acquires a lock in read mode but modifies the guarded data structure
    * acquires a lock in read-write mode although it only has `const`
      access to the guarded data
    * acquires one lock when another lock is already held, which may
      lead to deadlocks if another thread acquires locks in the
      inverse order

### Introduction to `folly/Synchronized.h`

The same code sample could be rewritten with `Synchronized`
as follows:

``` Cpp
    class AdPublisherHandler : public AdPopulatorIf,
                               public fb303::FacebookBase,
                               public ZkBaseApplication {
      ...
      Synchronized<OnDemandUpdateIdMap>
        adsToBeUpdated_,
        limitsToBeUpdated_,
        campaignsToBeUpdated_;
      ...
    };

    void AdPublisherHandler::requestUpdateAdId(const int64_t adId,
                                               const int32_t dbId) {
      checkDbHandlingStatus(dbId);
      SYNCHRONIZED (adsToBeUpdated_) {
        adsToBeUpdated_[dbId][adId] = 1;
      }
      adPublisherMonitor_->addStatValue("request_adId_update", 1, dbId);
      LOG(INFO) << "received request to update ad id " << adId;
    }
```

The rewrite does at maximum efficiency what needs to be done:
acquires the lock associated with the `OnDemandUpdateIdMap`
object, writes to the map, and releases the lock immediately
thereafter.

On the face of it, that's not much to write home about, and not
an obvious improvement over the previous state of affairs. But
the features at work invisible in the code above are as important
as those that are visible:

* Unlike before, the data and the mutex protecting it are
  inextricably encapsulated together.
* Critical sections are readily visible and emphasize code that
  needs to do minimal work and be subject to extra scrutiny.
* Dangerous nested `SYNCHRONIZED` statements are more visible
  than sequenced declarations of guards at the same level. (This
  is not foolproof because a method call issued inside a
  `SYNCHRONIZED` scope may open its own `SYNCHRONIZED` block.) A
  construct `SYNCHRONIZED_DUAL`, discussed later in this
  document, allows locking two objects quasi-simultaneously in
  the same order in all threads, thus avoiding deadlocks.
* If you tried to use `adsToBeUpdated_` outside the
  `SYNCHRONIZED` scope, you wouldn't be able to; it is virtually
  impossible to tease the map object without acquiring the
  correct lock. However, inside the `SYNCHRONIZED` scope, the
  *same* name serves as the actual underlying object of type
  `OnDemandUpdateIdMap` (which is a map of maps).
* Outside `SYNCHRONIZED`, if you just want to call one
  method, you can do so by using `adsToBeUpdated_` as a
  pointer like this:

    `adsToBeUpdated_->clear();`

This acquires the mutex, calls `clear()` against the underlying
map object, and releases the mutex immediately thereafter.

`Synchronized` offers several other methods, which are described
in detail below.

### Template class `Synchronized<T>`

##### Constructors

The default constructor default-initializes the data and its
associated mutex.


The copy constructor locks the source for reading and copies its
data into the target. (The target is not locked as an object
under construction is only accessed by one thread.)

Finally, `Synchronized<T>` defines an explicit constructor that
takes an object of type `T` and copies it. For example:

``` Cpp
    // Default constructed
    Synchronized< map<string, int> > syncMap1;

    // Copy constructed
    Synchronized< map<string, int> > syncMap2(syncMap1);

    // Initializing from an existing map
    map<string, int> init;
    init["world"] = 42;
    Synchronized< map<string, int> > syncMap3(init);
    EXPECT_EQ(syncMap3->size(), 1);
```

#### Assignment, swap, and copying

The canonical assignment operator locks both objects involved and
then copies the underlying data objects. The mutexes are not
copied. The locks are acquired in increasing address order, so
deadlock is avoided. For example, there is no problem if one
thread assigns `a = b` and the other assigns `b = a` (other than
that design probably deserving a Razzie award). Similarly, the
`swap` method takes a reference to another `Synchronized<T>`
object and swaps the data. Again, locks are acquired in a well-
defined order. The mutexes are not swapped.

An additional assignment operator accepts a `const T&` on the
right-hand side. The operator copies the datum inside a
critical section.

In addition to assignment operators, `Synchronized<T>` has move
assignment operators.

An additional `swap` method accepts a `T&` and swaps the data
inside a critical section. This is by far the preferred method of
changing the guarded datum wholesale because it keeps the lock
only for a short time, thus lowering the pressure on the mutex.

To get a copy of the guarded data, there are two methods
available: `void copy(T*)` and `T copy()`. The first copies data
to a provided target and the second returns a copy by value. Both
operations are done under a read lock. Example:

``` Cpp
    Synchronized< fbvector<fbstring> > syncVec1, syncVec2;
    fbvector<fbstring> vec;

    // Assign
    syncVec1 = syncVec2;
    // Assign straight from vector
    syncVec1 = vec;

    // Swap
    syncVec1.swap(syncVec2);
    // Swap with vector
    syncVec1.swap(vec);

    // Copy to given target
    syncVec1.copy(&vec);
    // Get a copy by value
    auto copy = syncVec1.copy();
```

#### `LockedPtr operator->()` and `ConstLockedPtr operator->() const`

We've already seen `operator->` at work. Essentially calling a
method `obj->foo(x, y, z)` calls the method `foo(x, y, z)` inside
a critical section as long-lived as the call itself. For example:

``` Cpp
    void fun(Synchronized< fbvector<fbstring> > & vec) {
      vec->push_back("hello");
      vec->push_back("world");
    }
```

The code above appends two elements to `vec`, but the elements
won't appear necessarily one after another. This is because in
between the two calls the mutex is released, and another thread
may modify the vector. At the cost of anticipating a little, if
you want to make sure you insert "world" right after "hello", you
should do this:

``` Cpp
    void fun(Synchronized< fbvector<fbstring> > & vec) {
      SYNCHRONIZED (vec) {
        vec.push_back("hello");
        vec.push_back("world");
      }
    }
```

This brings us to a cautionary discussion. The way `operator->`
works is rather ingenious with creating an unnamed temporary that
enforces locking and all, but it's not a panacea. Between two
uses of `operator->`, other threads may change the synchronized
object in arbitrary ways, so you shouldn't assume any sort of
sequential consistency. For example, the innocent-looking code
below may be patently wrong.

If another thread clears the vector in between the call to
`empty` and the call to `pop_back`, this code ends up attempting
to extract an element from an empty vector. Needless to say,
iteration a la:

``` Cpp
    // No. NO. NO!
    FOR_EACH_RANGE (i, vec->begin(), vec->end()) {
      ...
    }
```

is a crime punishable by long debugging nights.

If the `Synchronized<T>` object involved is `const`-qualified,
then you'll only be able to call `const` methods through `operator->`. 
So, for example, `vec->push_back("xyz")` won't work if `vec`
were `const`-qualified. The locking mechanism capitalizes on the
assumption that `const` methods don't modify their underlying
data and only acquires a read lock (as opposed to a read and
write lock), which is cheaper but works only if the immutability
assumption holds. Note that this is strictly not the case because
`const`-ness can always be undone via `mutable` members, casts,
and surreptitious access to shared data. Our code is seldom
guilty of such, and we also assume the STL uses no shenanigans.
But be warned.

#### `asConst()`

Consider:

``` Cpp
    void fun(Synchronized<fbvector<fbstring>> & vec) {
      if (vec->size() > 1000000) {
        LOG(WARNING) << "The blinkenlights are overloaded.";
      }
      vec->push_back("another blinkenlight");
    }
```

This code is correct (at least according to a trivial intent),
but less efficient than it could otherwise be. This is because
the call `vec->size()` acquires a full read-write lock, but only
needs a read lock. We need to help the type system here by
telling it "even though `vec` is a mutable object, consider it a
constant for this call". This should be easy enough because
conversion to const is trivial - just issue `const_cast<const
Synchronized<fbvector<fbstring>>&>(vec)`. Ouch. To make that
operation simpler - a lot simpler - `Synchronized<T>` defines the
method `asConst()`, which is a glorious one-liner. With `asConst`
in tow, it's very easy to achieve what we wanted:

``` Cpp
    void fun(Synchronized<fbvector<fbstring>> & vec) {
      if (vec.asConst()->size() > 1000000) {
        LOG(WARNING) << "The blinkenlights are overloaded.";
      }
      vec->push_back("another blinkenlight");
    }
```

QED (Quite Easy Done). This concludes the documentation for
`Synchronized<T>`.

### `SYNCHRONIZED`

The `SYNCHRONIZED` macro introduces a pseudo-statement that adds
a whole new level of usability to `Synchronized<T>`. As
discussed, `operator->` can only lock over the duration of a
call, so it is insufficient for complex operations. With
`SYNCHRONIZED` you get to lock the object in a scoped manner (not
unlike Java's `synchronized` statement) and to directly access
the object inside that scope.

`SYNCHRONIZED` has two forms. We've seen the first one a couple
of times already:

``` Cpp
    void fun(Synchronized<fbvector<int>> & vec) {
      SYNCHRONIZED (vec) {
        vec.push_back(42);
        CHECK(vec.back() == 42);
        ...
      }
    }
```

The scope introduced by `SYNCHRONIZED` is a critical section
guarded by `vec`'s mutex. In addition to doing that,
`SYNCHRONIZED` also does an interesting sleight of hand: it binds
the name `vec` inside the scope to the underlying `fbvector<int>`
object - as opposed to `vec`'s normal type, which is
`Synchronized<fbvector<int>>`. This fits very nice the "form
follow function" - inside the critical section you have earned
access to the actual data, and the name bindings reflect that as
well. `SYNCHRONIZED(xyz)` essentially cracks `xyz` temporarily
and gives you access to its innards.

Now, what if `fun` wants to take a pointer to
`Synchronized<fbvector<int>>` - let's call it `pvec`? Generally,
what if we want to synchronize on an expression as opposed to a
symbolic variable? In that case `SYNCHRONIZED(*pvec)` would not
work because "`*pvec`" is not a name. That's where the second
form of `SYNCHRONIZED` kicks in:

``` Cpp
    void fun(Synchronized<fbvector<int>> * pvec) {
      SYNCHRONIZED (vec, *pvec) {
        vec.push_back(42);
        CHECK(vec.back() == 42);
        ...
      }
    }
```

Ha, so now we pass two arguments to `SYNCHRONIZED`. The first
argument is the name bound to the data, and the second argument
is the expression referring to the `Synchronized<T>` object. So
all cases are covered.

### `SYNCHRONIZED_CONST`

Recall from the discussion about `asConst()` that we
sometimes want to voluntarily restrict access to an otherwise
mutable object. The `SYNCHRONIZED_CONST` pseudo-statement
makes that intent easily realizable and visible to
maintainers. For example:

``` Cpp
    void fun(Synchronized<fbvector<int>> & vec) {
      fbvector<int> local;
      SYNCHRONIZED_CONST (vec) {
        CHECK(vec.size() > 42);
        local = vec;
      }
      local.resize(42000);
      SYNCHRONIZED (vec) {
        local.swap(vec);
      }
    }
```

Inside a `SYNCHRONIZED_CONST(xyz)` scope, `xyz` is bound to a `const`-
qualified datum. The corresponding lock is a read lock.

`SYNCHRONIZED_CONST` also has a two-arguments version, just like
`SYNCHRONIZED`. In fact, `SYNCHRONIZED_CONST(a)` simply expands
to `SYNCHRONIZED(a, a.asConst())` and `SYNCHRONIZED_CONST(a, b)`
expands to `SYNCHRONIZED(a, (b).asConst())`. The type system and
`SYNCHRONIZED` take care of the rest.

### `TIMED_SYNCHRONIZED` and `TIMED_SYNCHRONIZED_CONST`

These pseudo-statements allow you to acquire the mutex with a
timeout. Example:

``` Cpp
    void fun(Synchronized<fbvector<int>> & vec) {
      TIMED_SYNCHRONIZED (10, vec) {
        if (vec) {
          vec->push_back(42);
          CHECK(vec->back() == 42);
        } else {
            LOG(INFO) << "Dognabbit, I've been waiting over here for 10 milliseconds and couldn't get through!";
        }
      }
    }
```

If the mutex acquisition was successful within a number of
milliseconds dictated by its first argument, `TIMED_SYNCHRONIZED`
binds its second argument to a pointer to the protected object.
Otherwise, the pointer will be `NULL`. (Contrast that with
`SYNCHRONIZED`), which always succeeds so it binds the protected
object to a reference.) Inside the `TIMED_SYNCHRONIZED` statement
you must, of course, make sure the pointer is not null to make
sure the operation didn't time out.

`TIMED_SYNCHRONIZED` takes two or three parameters. The first is
always the timeout, and the remaining one or two are just like
the parameters of `SYNCHRONIZED`.

Issuing `TIMED_SYNCHRONIZED` with a zero timeout is an
opportunistic attempt to acquire the mutex.

### `UNSYNCHRONIZED`

`SYNCHRONIZED` is a good mechanism for enforcing scoped
synchronization, but it has the inherent limitation that it
requires the critical section to be, well, scoped. Sometimes the
code structure requires a fleeting "escape" from the iron fist of
synchronization. Clearly, simple cases are handled with sequenced
`SYNCHRONIZED` scopes:

``` Cpp
    Synchronized<map<int, string>> dic;
    ...
    SYNCHRONIZED (dic) {
      if (dic.find(0) != dic.end()) {
        return;
      }
    }
    LOG(INFO) << "Key 0 not found, inserting it."
    SYNCHRONIZED (dic) {
      dic[0] = "zero";
    }
```

For more complex, nested flow control, you may want to use the
`UNSYNCHRONIZED` macro. It (only) works inside a `SYNCHRONIZED`
pseudo-statement and temporarily unlocks the mutex:

``` Cpp

    Synchronized<map<int, string>> dic;
    ...
    SYNCHRONIZED (dic) {
      auto i = dic.find(0);
      if (i != dic.end()) {
        UNSYNCHRONIZED (dic) {
          LOG(INFO) << "Key 0 not found, inserting it."
        }
        dic[0] = "zero";
      } else {
        *i = "zero";
      }
    }
    LOG(INFO) << "Key 0 not found, inserting it."
    SYNCHRONIZED (dic) {
      dic[0] = "zero";
    }
```

Clearly `UNSYNCHRONIZED` comes with specific caveats and
liabilities. You must assume that during the `UNSYNCHRONIZED`
section, other threads might have changed the protected structure
in arbitrary ways. In the example above, you cannot use the
iterator `i` and you cannot assume that the key `0` is not in the
map; another thread might have inserted it while you were
bragging on `LOG(INFO)`.

### `SYNCHRONIZED_DUAL`

Sometimes locking just one object won't be able to cut the mustard. Consider a
function that needs to lock two `Synchronized` objects at the
same time - for example, to copy some data from one to the other.
At first sight, it looks like nested `SYNCHRONIZED` statements
will work just fine:

``` Cpp
    void fun(Synchronized<fbvector<int>> & a, Synchronized<fbvector<int>> & b) {
      SYNCHRONIZED (a) {
        SYNCHRONIZED (b) {
          ... use a and b ...
        }
      }
    }
```

This code compiles and may even run most of the time, but embeds
a deadly peril: if one threads call `fun(x, y)` and another
thread calls `fun(y, x)`, then the two threads are liable to
deadlocking as each thread will be waiting for a lock the other
is holding. This issue is a classic that applies regardless of
the fact the objects involved have the same type.

This classic problem has a classic solution: all threads must
acquire locks in the same order. The actual order is not
important, just the fact that the order is the same in all
threads. Many libraries simply acquire mutexes in increasing
order of their address, which is what we'll do, too. The pseudo-
statement `SYNCHRONIZED_DUAL` takes care of all details of proper
locking of two objects and offering their innards:

``` Cpp
    void fun(Synchronized<fbvector<int>> & a, Synchronized<fbvector<int>> & b) {
      SYNCHRONIZED_DUAL (myA, a, myB, b) {
        ... use myA and myB ...
      }
    }
```

To avoid potential confusions, `SYNCHRONIZED_DUAL` only defines a
four-arguments version. The code above locks `a` and `b` in
increasing order of their address and offers their data under the
names `myA` and `myB`, respectively.

### Synchronizing several data items with one mutex

The library is geared at protecting one object of a given type
with a mutex. However, sometimes we'd like to protect two or more
members with the same mutex. Consider for example a bidirectional
map, i.e. a map that holds an `int` to `string` mapping and also
the converse `string` to `int` mapping. The two maps would need
to be manipulated simultaneously. There are at least two designs
that come to mind.

#### Using a nested `struct`

You can easily pack the needed data items in a little struct.
For example:

``` Cpp
    class Server {
      struct BiMap {
        map<int, string> direct;
        map<string, int> inverse;
      };
      Synchronized<BiMap> bimap_;
      ...
    };
    ...
    SYNCHRONIZED (bymap_) {
      bymap_.direct[0] = "zero";
      bymap_.inverse["zero"] = 0;
    }
```

With this code in tow you get to use `bimap_` just like any other
`Synchronized` object, without much effort.

#### Using `std::tuple`

If you won't stop short of using a spaceship-era approach,
`std::tuple` is there for you. The example above could be
rewritten for the same functionality like this:

``` Cpp
    class Server {
      Synchronized<tuple<map<int, string>, map<string, int>>> bimap_;
      ...
    };
    ...
    SYNCHRONIZED (bymap_) {
      get<0>(bymap_)[0] = "zero";
      get<1>(bymap_)["zero"] = 0;
    }
```

The code uses `std::get` with compile-time integers to access the
fields in the tuple. The relative advantages and disadvantages of
using a local struct vs. `std::tuple` are quite obvious - in the
first case you need to invest in the definition, in the second
case you need to put up with slightly more verbose and less clear
access syntax.

### Summary

`Synchronized` and its supporting tools offer you a simple,
robust paradigm for mutual exclusion-based concurrency. Instead
of manually pairing data with the mutexes that protect it and
relying on convention to use them appropriately, you can benefit
of encapsulation and typechecking to offload a large part of that
task and to provide good guarantees.
