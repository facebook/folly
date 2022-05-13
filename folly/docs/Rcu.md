`folly/synchronization/Rcu.h`
----------------------

C++ read-copy-update (RCU) functionality in folly.

## Overview
***

Read-copy-update (RCU) is a low-overhead synchronization mechanism that provides
guaranteed ordering between operations on shared data. In the simplest usage
pattern, readers enter a critical section, view some state, and leave the
critical section, while writers modify shared state and then defer some cleanup
operations.

Proper use of the APIs provided by folly RCU will guarantee that a cleanup
operation that is deferred during a reader critical section will not be executed
until after that critical section is over.

## Usage
***

### folly::rcu_domain

The main synchronization primitive in folly RCU is a `folly::rcu_domain`.  A
`folly::rcu_domain` is a "universe" of deferred execution. Each domain has an
executor on which deferred functions may execute. `folly::rcu_domain` provides
the requirements to be
[BasicLockable](https://en.cppreference.com/w/cpp/named_req/BasicLockable), and
readers may enter a read region in a `folly::rcu_domain` by treating the domain
as a mutex type that can be wrapped by C++ locking primitives.

For example, to enter and exit a read region using non-movable RAII semantics,
you could use an `std::scoped_lock`:

```Cpp
{
  std::scoped_lock<folly::rcu_domain> lock(folly::rcu_default_domain());
  protectedData.read();
}
```

Alternatively, if you need to be able to move the lock, you could use
`std::unique_lock`:

```Cpp
class ProtectedData {
  private:
    std::unique_lock<folly::rcu_domain> lock_;
    void* data_;
}
```

### Default vs. custom domains

There is a global, default domain that can be accessed using
`folly::rcu_default_domain()` as in the example above. If required, you can also
create your own domain:

```Cpp
{
  Executor* my_executor = getExecutor();
  folly::rcu_domain domain(my_executor /* or nullptr to use default */);
}
```

In general, using the default domain is strongly encouraged as you will likely
get better cache locality by sharing a domain that it used by other callers in
process. If, however, you can't avoid blocking during reader critical sections,
your own custom domain should be used to avoid delaying reclamation from other
updaters. A custom domain can also be used if you want update callbacks to be
invoked on a specific executor.

### Updates and retires

A typical reader / updater synchronization will look something like this:

```Cpp
void doSomethingWith(IPAddress host);

static std::atomic<ConfigData*> globalConfigData;

void reader() {
  while (true) {
    IPAddress curManagementServer;
    {
      // We're about to do some reads we want to protect; if we read a
      // pointer, we need to make sure that if the writer comes along and
      // updates it, the writer's cleanup operation won't happen until we're
      // done accessing the pointed-to data. We get a Guard on that
      // domain; as long as it exists, no function subsequently passed to
      // invokeEventually will execute.
      std::scoped_lock<folly::rcu_domain> guard(folly::rcu_default_domain());
      ConfigData* configData = globalConfigData.load(std::memory_order_consume);
      // We created a guard before we read globalConfigData; we know that the
      // pointer will remain valid until the guard is destroyed.
      curManagementServer = configData->managementServerIP;
      // RCU domain via the scoped mutex is released here; retired objects
      // may be freed.
    }
    doSomethingWith(curManagementServer);
  }
}

void writer() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(60));
    ConfigData* oldConfigData = globalConfigData.load(std::memory_order_relaxed);
    ConfigData* newConfigData = loadConfigDataFromRemoteServer();
    globalConfigData.store(newConfigData, std::memory_order_release);
    folly::rcu_retire(oldConfigData);
    // Alternatively, in a blocking manner:
    //   folly::rcu_synchronize();
    //   delete oldConfigData;
  }
}
```
In the example above, a single writer updates `ConfigData*` in a loop, and then
defers freeing it until all RCU readers have exited their read regions. The
writer may use either of the following two APIs to safely defer freeing the
old `ConfigData*`:

* `rcu_retire()`: To schedule the asynchronous deletion of the `oldConfigData` pointer when all readers have exited their read regions.
* `rcu_synchronize()`: To block the calling thread until all readers have exited their read regions, at which point the pointer is safe to be deleted.

If you expect there to be very long read regions, it may be required to use
`rcu_synchronize()` or a periodic `rcu_barrier()` (described below) to avoid
running out of memory due to delayed reclamation.


### folly::rcu_barrier()

Another synchronization primitive provided by the folly RCU library is
`rcu_barrier()`. Unlike `rcu_synchronize()`, which blocks until all outstanding
readers have exited their read regions, `rcu_barrier()` blocks until all
outstanding *deleters* (specified in a call to `rcu_retire()`) are completed.

As mentioned above, one example of where this may be useful is avoiding
out-of-memory errors due to scheduling too many objects whose reclamation is
delayed. Taking our example from above, we could avoid OOMs using a periodic
invocation to `rcu_barrier()` as follows:

```Cpp
static std::atomic<ConfigData*> globalConfigData;

void writer() {
  uint32_t retires = 0;
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(60));
    ConfigData* oldConfigData = globalConfigData.load(std::memory_order_relaxed);
    ConfigData* newConfigData = loadConfigDataFromRemoteServer();
    globalConfigData.store(newConfigData, std::memory_order_release);
    if (retires++ % 1000 == 0) {
      folly::rcu_barrier();
    }
    folly::rcu_retire(oldConfigData);
  }
}
```

### Custom deleter in folly::rcu_retire

When invoking `folly::rcu_retire()`, you may optionally also pass a custom
deleter function that is invoked instead of
[`std::default_delete`](https://en.cppreference.com/w/cpp/memory/default_delete):

```Cpp
#include <folly/logging/xlog.h>

static std::atomic<ConfigData*> globalConfigData;

void writer() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(60));
    ConfigData* oldConfigData = globalConfigData.load(std::memory_order_relaxed);
    ConfigData* newConfigData = loadConfigDataFromRemoteServer();
    globalConfigData.store(newConfigData, std::memory_order_release);
    folly::rcu_retire(oldConfigData, [](ConfigData* obj) {
      XLOG(INFO) << "Deleting retired config " << oldConfigData->version);
      delete obj;
    });
  }
}
```

### API limitations of updaters

Exceptions may not be thrown at any point in a retire callback. This includes
both the deleter, as well as the object's destructor. Other than this, any
operation is safe from within a retired object's destructor, including retiring
other objects, or even retiring the same object as long as the custom deleter
did not free it.

When using the default domain or the default executor, it is not legal to hold a
lock across an `rcu_retire()` that is acquired by the deleter.  This is normally
not a problem when using the default deleter `delete`, which does not acquire
any user locks.  However, even when using the default deleter, an object having
a user-defined destructor that acquires locks held across the corresponding call
to `rcu_retire()` can still deadlock.

Note as well that there is no guarantee of the order in which retire callbacks
are invoked. A retire callback is guaranteed to be invoked only after all
readers that were present when the callback was scheduled have exited.
Otherwise, any ordering of callback invocation may occur.

### Note on fork()

`fork()` may not be invoked in a multithreaded program where any thread other
than the calling thread is in an RCU read region. Doing so will result in
undefined behavior, and will likely lead to deadlock. If the forking thread is
inside of an RCU read region, it must invoke `exec()` before exiting the read
region.

## Performance
***

`std::scoped_lock<folly::rcu_domain>` creation/destruction is on the order of
~5ns.  By comparison, `folly::SharedMutex::lock_shared()` followed by
`folly::SharedMutex::unlock_shared()` is ~26ns.
