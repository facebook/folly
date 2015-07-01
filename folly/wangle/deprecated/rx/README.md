Rx is a pattern for "functional reactive programming" that started at
Microsoft in C#, and has been reimplemented in various languages, notably
RxJava for JVM languages.

It is basically the plural of Futures (a la Wangle).

```
                    singular              |            plural
        +---------------------------------+-----------------------------------
  sync  |  Foo getData()                  |  std::vector<Foo> getData()
  async |  wangle::Future<Foo> getData()  |  wangle::Observable<Foo> getData()
```

For more on Rx, I recommend these resources:

Netflix blog post (RxJava): http://techblog.netflix.com/2013/02/rxjava-netflix-api.html
Introduction to Rx eBook (C#): http://www.introtorx.com/content/v1.0.10621.0/01_WhyRx.html
The RxJava wiki: https://github.com/Netflix/RxJava/wiki
Netflix QCon presentation: http://www.infoq.com/presentations/netflix-functional-rx
https://rx.codeplex.com/

I haven't even tried to support move-only data in this version. I'm on the
fence about the usage of shared_ptr. Subject is underdeveloped. A whole rich
set of operations is obviously missing. I haven't decided how to handle
subscriptions (and therefore cancellation), but I'm pretty sure C#'s
"Disposable" is thoroughly un-C++ (opposite of RAII). So for now subscribe
returns nothing at all and you can't cancel anything ever. The whole thing is
probably riddled with lifetime corner case bugs that will come out like a
swarm of angry bees as soon as someone tries an infinite sequence, or tries to
partially observe a long sequence. I'm pretty sure subscribeOn has a bug that
I haven't tracked down yet.

DEPRECATED:
This was an experimental exploration. There are better, more robust, and (most
importantly) supported C++ implementations, notably
[rxcpp](https://rxcpp.codeplex.com/). Use that instead. You really shouldn't
use this one. It's unsupported and incomplete. Honest.
