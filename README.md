# cfuture
A promise future implementation with continuation.

cfuture is **header-only**, and it only has a **single file** `cfuture.hpp`. It requires cpp14.

# Motivation

cfuture is aimed to provide a promise/future class which supports `future::then` to attach the continuations like a chain. I admit that async/await is a better practice than promise/future when writing the asynchronous codes. But there are still a lot of projects cannot use c++20 or higher standard. Or, sometimes, the promise/future is easier than coroutine.

The `boost::future` and `std::experimental::future` (in concurrency TS) both support `then`. But I hope to provide a light and easy-to-rely implementation. It isn't the cfuture's goal to be completely complied with the design of `std::future` or `boost::future`. But still, it will very close to them. It could be an alternate in most cases.

# Quick Start

```cpp
  using namespace std::chrono_literals;
  using namespace cfuture;
  promise<int> p;
  std::cout << "the main thread is " << std::this_thread::get_id() << '\n';
  std::thread th([&] {
    std::cout << "run in thread id " << std::this_thread::get_id() << '\n';
    std::this_thread::sleep_for(100ms);
    p.set_value(1);
  });
  future<std::string> f;
  f = p.get_future()
          .then([](int v) {
            promise<int> p;
            auto future = p.get_future();
            std::thread th([v, p = std::move(p)]() mutable {
              std::cout << "run in thread id " << std::this_thread::get_id() << '\n';
              std::this_thread::sleep_for(100ms);
              p.set_value(v + 2);
            });
            th.detach();
            return future;
          })
          .then([](int v) {
            std::cout << "run in thread id " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return v + 3;
          })
          .then([](int v) {
            std::cout << "run in thread id " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return std::to_string(v + 4);
          });
  std::cout << f.get() << '\n';
```

The possible output of the above codes is
```
the main thread is 0x7ff857913340
run in thread 0x70000da84000
run in thread 0x70000db07000
run in thread 0x70000db07000
run in thread 0x70000db07000
10
```
