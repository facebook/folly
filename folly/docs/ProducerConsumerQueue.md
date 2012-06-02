`folly/ProducerConsumerQueue.h`
-------------------------------

The `folly::ProducerConsumerQueue` class is a one-producer
one-consumer queue with very low synchronization overhead.

The queue must be created with a fixed maximum size (and allocates
that many cells of sizeof(T)), and it provides just three simple
operations: read, write, and isFull.  All of these operations are
wait-free.  The read and write operations must only be called by the
reader and writer thread, respectively, but isFull is accessible to
both.

Both read and write may fail if the queue is full, so in many
situations it is important to choose the queue size such that the
queue filling up for long is unlikely.

### Example
***

A toy example that doesn't really do anything useful:

``` Cpp
    folly::ProducerConsumerQueue<folly::fbstring> queue;

    std::thread reader([&queue] {
      for (;;) {
        folly::fbstring str;
        while (!queue.read(str)) continue;

        sink(str);
      }
    });

    // producer thread:
    for (;;) {
      folly::fbstring str = source();
      while (!queue.write(str)) continue;
    }
```
