`folly/Locked.h`
----------------

`folly/Locked.h` introduces a simple abstraction for mutex-
based concurrency. It replaces convoluted, unwieldy, and just
plain wrong code with simple constructs that are easy to get
right and difficult to get wrong.

### Motivation

Many of our multithreaded C++ programs use shared data structures
associated with locks. This follows the time-honored adage of
mutex-based concurrency control "associate mutexes with data, not code".
Examples are abundant and easy to find. For example:

``` Cpp

    class AdPublisherHandler {
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
      adsToBeUpdated_->insert(dbId, adId);
      adPublisherMonitor_->addStatValue("request_adId_update", 1, dbId);
      LOG(INFO) << "received request to update ad id " << adId;
    }
```

However, the correctness of this technique is entirely predicated on
convention.  Developers manipulating these data members must be take
care to manually acquire the correct lock for the data they wish to
access.  There is no ostensible error for code that:

* manipulates a piece of data without acquiring its lock first
* acquires a different lock instead of the intended one
* acquires a lock in read mode but modifies the guarded data structure

### Introduction to `folly/Locked.h`

The same code sample could be rewritten with `Locked`
as follows:

``` Cpp
    class AdPublisherHandler : public AdPopulatorIf,
                               public fb303::FacebookBase,
                               public ZkBaseApplication {
      ...
      RWLocked<OnDemandUpdateIdMap> adsToBeUpdated_;
      RWLocked<OnDemandUpdateIdMap> limitsToBeUpdated_;
      RWLocked<OnDemandUpdateIdMap> campaignsToBeUpdated_;
      ...
    };

    void AdPublisherHandler::requestUpdateAdId(const int64_t adId,
                                               const int32_t dbId) {
      checkDbHandlingStatus(dbId);
      adsToBeUpdated_.wlock()->insert(dbId, adId);
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
* If you tried to use `adsToBeUpdated_` without acquiring the lock you
  wouldn't be able to; it is virtually impossible to access the map
  object without acquiring the correct lock.
* The lock is released immediately after the insert operation is
  performed, and is not held for operations that do not need it.

If you need to perform several operations while holding the lock,
`Locked` provides a couple ways of doing this.

The `wlock()` method (or `lock()` if you have a non-shared mutex type)
returns a `LockedPtr` object that can be stored in a variable.  The lock
will be held for as long as this object exists, similar to a
`std::unique_lock`.  This object can be used as if it were a pointer to
get access to the locked data:

``` Cpp
    {
       auto lockedAds = adsToBeUpdated_.wlock();
       lockedAds->insert(dbId1, adId1);
       lockedAds->insert(dbId2, adId2);
    }
```

Alternatively, `Locked` also provides mechanisms to run a function while
holding the lock.  This makes it possible to use lambdas to define brief
critical sections:

``` Cpp
    void AdPublisherHandler::requestUpdateAdId(const int64_t adId,
                                               const int32_t dbId) {
      checkDbHandlingStatus(dbId);
      adsToBeUpdated_.withWLock([](auto& lockedAds) {
        lockedAds.insert(dbId, adId);
      });
      adPublisherMonitor_->addStatValue("request_adId_update", 1, dbId);
      LOG(INFO) << "received request to update ad id " << adId;
    }
```

One advantage of the `withWLock()` approach is that it forces a new
scope to be used for the critical section, making the critical section
more obvious in the code, and helping to encourage code that releases
the lock as soon as possible.

### Template class `Locked<T>`

##### Template Parameters

`Locked` is a template with two parameters, the data type and a mutex
type: `Locked<T, Mutex>`.

If not specified, the mutex type defaults to `std::mutex`.  However, any
mutex type supported by `folly::LockTraits` can be used instead.
`folly::LockTraits` can be specialized to support other custom lock
types that it does not know about out of the box.

`RWLocked<T>` is a template alias for `Locked<T, folly::SharedMutex>`.
This provides shared lock functionality for callers that benefit from
separate read/write locking behavior.

`Locked` provides slightly different APIs when instantiated with a
shared lock type than with a plain exclusive lock type.  When used with
a shared lock type, it has separate `wlock()` and `rlock()` methods.
When used with a plain exclusive lock it only has a single `lock()`
method.

##### Constructors

The default constructor default-initializes the data and its
associated mutex.

If the `Locked<T>` constructor is called with any arguments, it forwards
those arguments to the `T` constructor to initialize the data.

The copy constructor and move constructor are disabled, since the mutex
itself cannot be copy or move constructed.  However, you can pass in a
`const T&` to copy construct the internal data, as long as that is
supported by the data type.

#### Assignment

Since the mutex inside the `Locked<T>` object is not movable or
assignable, the assignment operators for `Locked<T>` are disabled,
just like the copy constructor.

However, you can assign a `Locked<T>` from another `T` object as long as
the data type supports the corresponding copy or move assignment
operator.

If you are assigning one `Locked<T>` to the value of data from another
`Locked<T>` object, make sure to always acquire the locks in a
consistent order to avoid deadlocks.  The `acquireLocked()` helper
function can be used for this purpose.  It always acquires the lock on
the object with the lowest address first.

To get a copy of the guarded data, there are two methods
available: `void copy(T*)` and `T copy()`. The first copies data
to a provided target and the second returns a copy by value. Both
operations are done under a read lock. Example:

``` Cpp
    Locked< vector<string> > syncVec;
    vector<string> vec;

    // Copy to given target
    syncVec.copy(&vec);
    // Get a copy by value
    auto copy = syncVec.copy();
```

#### `lock()`

If the mutex type used with `Locked` is a simple exclusive lock (as
opposed to a read/write lock), `Locked<T>` provides a `lock()` method
that returns a `LockedPtr` to access the data while holding the lock.

The `LockedPtr` object returned by `lock()` holds the lock for as long
as it exists.  Whenever possible, prefer declaring a separate inner
scope to make sure the `LockedPtr` is destroyed as soon as the lock is
no longer needed:

``` Cpp
    void fun(Locked<std::vector<std::string>>& vec) {
      {
        auto locked = vec.lock();
        locked->push_back("hello");
        locked->push_back("world");
      }
      LOG(INFO) << "successfully added greeting";
    }
```

#### `wlock()` and `rlock()`

If the mutex type used with `Locked` is a shared lock, `Locked<T>`
provides a `wlock()` method that acquires an exclusive lock, and an
`rlock()` method that acquires a shared lock.

The `LockedPtr` returned by `rlock()` only provides const access to the
internal data, to ensure that it cannot be modified while only holding a
shared lock.

``` Cpp
    int computeSum(const RWLocked<std::vector<int>>& vec) {
      int sum = 0;
      auto locked = *vec.rlock();
      for (int n : *locked) {
        sum += locked;
      }
      return sum;
    }

    void doubleValues(RWLocked<std::vector<int>>& vec) {
      auto locked = vec.wlock();
      for (int& n : *locked) {
        n *= 2;
      }
    }
```

This example brings us to a cautionary discussion.  The `LockedPtr`
object returned by `lock()`, `rlock()`, or `wlock()` only holds the lock
as long as it exists.  This object makes it difficult to access the data
without holding the lock, but not impossible.  In particular you should
never store a raw pointer or reference to the internal data for longer
than the lifetime of the `LockedPtr` object.

In particular, if we had written the following code in the examples
above, this would have continued accessing the vector after the lock had
been released:

``` Cpp
    // No. NO. NO!
    for (int& n : *vec.wlock()) {
      n *= 2;
    }
```

The `vec.wlock()` return value is destroyed in this case as soon as the
internal range iterators are created.  Therefore the lock is not held
for the duration of the loop bocy.

Needless to say, this is a crime punishable by long debugging nights.

Range-based for loops are slightly subtle about the lifetime of objects
used in the initializer statement.  Most other problematic use cases are
a bit easier to spot than this, since the lifetime of the `LockedPtr` is
more explicitly visible.

#### `withLock()`

As an alternative to the `lock()` API, `Locked` also provides a
`withLock()` method that executes a function or lambda expression while
holding the lock.  The function receives a reference to the data as its
only argument.

This has a few benefits compared to `lock()`:

* The lambda expression requires its own nested scope, making critical
  sections more visible in the code.  Callers can always define a new
  scope when using `lock()` if they choose to.  `withLock()` ensures
  that a new scope must always be defined.
* Because a new scope is required, `withLock()` also helps encourage
  users to release the lock as soon as possible.  Because the critical
  section scope is easily visible in the code, it is harder to
  accidentally put extraneous code inside the critical section without
  realizing it.
* The separate lambda scope makes it more difficult to store raw
  pointers or references to the protected data and continue using those
  pointers outside the critical section.

For example, `withLock()` makes the range-based for loop mistake from
above much harder to accidentally run into:

``` Cpp
    vec.withLock([](auto& locked) {
      for (int& n : locked) {
        n *= 2;
      }
    });
```

This code does not have the same problem as the counter-example with
`wlock()` above, since the lock is held for the duration of the loop.

When using `Locked` with a shared mutex type, it provides separate
`withWLock()` and `withRLock()` methods instead of `withLock()`.

### Timed Locking

When `Locked` is used with a mutex type that supports timed lock acquisition,
`lock()`, `wlock()`, and `rlock()` can all take an optional
`std::chrono` duration argument.  This argument specifies a timeout to
use for acquiring the lock.  If the lock it not acquired before the
timeout expires, a null pointer will be returned.  Callers must
explicitly check the return value before using it:

``` Cpp
    void fun(Locked<std::vector<std::string>>& vec) {
      {
        auto locked = vec.lock(10_ms);
        if (!locked()) {
          LOG(ERROR) << "failed to acquire lock";
          throw std::runtime_error("failed to acquire lock");
        }
        locked->push_back("hello");
        locked->push_back("world");
      }
      LOG(INFO) << "successfully added greeting";
    }
```

### `Locked` and `std::condition_variable`

When used with a `std::mutex`, `Locked<T>` supports using a
`std::condition_variable` with its internal mutex.  This allows a
`condition_variable` to be used to wait for a particular change to occur
in the internal data.

The `LockedPtr` returned by `lock()` has a `getUniqueLock()` method that
returns a reference to a `std::unique_lock<std::mutex>` which can be
given to the `std::condition_variable`:

``` Cpp
    Locked<std::vector<std::string>>& vec;
    std::condition_variable emptySignal;

    // Asuming some other thread will put data on vec and signal
    // emptySignal, we can then wait on it as follows:
    auto locked = vec.lock();
    while (locked->empty()) {
      emptySignal.wait(locked.getUniqueLock());
    }
```

### `scopedUnlock()`

`withLock()` is a good mechanism for enforcing scoped
synchronization, but it has the inherent limitation that it
requires the critical section to be, well, scoped. Sometimes the
code structure requires a fleeting "escape" from the iron fist of
synchronization. Clearly, simple cases are handled with sequenced
`withLock()` calls:

``` Cpp
    Locked<map<int, string>> dic;
    ...
    dic.withLock([](auto& locked) {
      if (locked.find(0) != locked.end()) {
        return;
      }
    });
    LOG(INFO) << "Key 0 not found, inserting it."
    dic.withLock([](auto& locked) {
      locked[0] = "zero";
    });
```

For more complex, nested flow control, you may want to use the
`scopedUnlock()` method.  This method is available on the `LockedPtr`
object returned by `lock()`.  It returns an object that will release the
lock for as long as it exists, and re-acquire the lock when it is
destroyed.

Since the `withLock()` method does not provide you with access to
`LockedPtr` object, you can instead use `withLockPtr()`.
`withLockPtr()` is similar to `withLock()`, but passes a `LockedPtr`
argument to your function instead of passing the reference to the data
directly.

``` Cpp

    Locked<map<int, string>> dic;
    ...
    dic.withLockPtr([](auto& lockedPtr) {
      auto i = lockedPtr->find(0);
      if (i != lockedPtr->end()) {
        {
          auto unlocker = lockedPtr->scopedUnlock();
          LOG(INFO) << "Key 0 not found, inserting it."
        }
        (*lockedPtr)[0] = "zero";
      } else {
        *i = "zero";
      }
    });
    LOG(INFO) << "Key 0 not found, inserting it."
    dic.withLock([](auto& locked) {
      dic[0] = "zero";
    });
```

Clearly `scopedUnlock()` comes with specific caveats and
liabilities. You must assume that during the `scopedUnlock()`
section, other threads might have changed the protected structure
in arbitrary ways. In the example above, you cannot use the
iterator `i` and you cannot assume that the key `0` is not in the
map; another thread might have inserted it while you were
bragging on `LOG(INFO)`.

### `acquireLocked()`

Sometimes locking just one object won't be able to cut the mustard.
Consider a function that needs to lock two `Locked` objects at the
same time - for example, to copy some data from one to the other.
At first sight, it looks like sequential `lock()` statements
will work just fine:

``` Cpp
    void fun(Locked<vector<int>>& a, Locked<vector<int>>& b) {
      auto lockedA = a.lock();
      auto lockedB = b.lock();
      ... use lockedA and lockedB ...
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
order of their address, which is what we'll do, too. The
`acquireLocked()` function locking of two objects and offering their
innards.  It returns a `std::tuple` of `LockedPtr`s

``` Cpp
    void fun(Locked<vector<int>>& a, Locked<vector<int>>& b) {
      auto ret = folly::acquireLocked(a, b);
      auto& lockedA = std::get<0>(ret);
      auto& lockedB = std::get<1>(ret);
      ... use lockedA and lockedB ...
    }
```

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
      Locked<BiMap> bimap_;
      ...
    };
    ...
    bymap_.withLock([](auto& locked) {
      locked.direct[0] = "zero";
      locked.inverse["zero"] = 0;
    });
```

With this code in tow you get to use `bimap_` just like any other
`Locked` object, without much effort.

#### Using `std::tuple`

If you won't stop short of using a spaceship-era approach,
`std::tuple` is there for you. The example above could be
rewritten for the same functionality like this:

``` Cpp
    class Server {
      Locked<tuple<map<int, string>, map<string, int>>> bimap_;
      ...
    };
    ...
    bimap_.withLock([](auto& locked) {
      get<0>(locked)[0] = "zero";
      get<1>(locked)["zero"] = 0;
    });
```

The code uses `std::get` with compile-time integers to access the
fields in the tuple. The relative advantages and disadvantages of
using a local struct vs. `std::tuple` are quite obvious - in the
first case you need to invest in the definition, in the second
case you need to put up with slightly more verbose and less clear
access syntax.

### Summary

`Locked` and its supporting tools offer you a simple,
robust paradigm for mutual exclusion-based concurrency. Instead
of manually pairing data with the mutexes that protect it and
relying on convention to use them appropriately, you can benefit
of encapsulation and type checking to offload a large part of that
task and to provide good guarantees.
