# cfuture
A promise future implementation with continuation.

cfuture is **header-only**, and it only has a **single file** `cfuture.hpp`. It requires cpp14.

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