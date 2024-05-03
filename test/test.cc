#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "cfuture.hpp"
#include "gtest/gtest.h"

TEST(BasicTest, Get) {
  using namespace std::chrono_literals;
  cfuture::Promise<std::string> p;
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    p.set_value("print some strings after the 1s delay");
  });
  auto future = p.get_future();
  EXPECT_EQ("print some strings after the 1s delay", future.get());
  EXPECT_FALSE(future.valid());
  th.join();
}

TEST(BasicTest, VoidGet) {
  using namespace std::chrono_literals;
  cfuture::Promise<void> p;
  std::string val;
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    val.assign("print some strings after the 1s delay");
    p.set_value();
  });
  auto future = p.get_future();
  future.get();
  EXPECT_EQ("print some strings after the 1s delay", val);
  EXPECT_FALSE(future.valid());
  th.join();
}

TEST(BasicTest, Wait) {
  using namespace std::chrono_literals;
  cfuture::Promise<int> p;
  auto start = std::chrono::steady_clock::now();
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    p.set_value(123);
  });
  cfuture::Future<int> future = p.get_future();
  future.wait();
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_GE(dur.count(), 1000);
  EXPECT_LE(dur.count(), 1100);
  EXPECT_EQ(cfuture::future_status::ready, future.wait_for(0s));
  EXPECT_EQ(123, future.get());
  th.join();
}

TEST(BasicTest, WaitFor) {
  using namespace std::chrono_literals;
  cfuture::Promise<int> p;
  auto start = std::chrono::steady_clock::now();
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    p.set_value(123);
  });
  cfuture::Future<int> future = p.get_future();
  ASSERT_EQ(cfuture::future_status::timeout, future.wait_for(1ms));
  ASSERT_EQ(cfuture::future_status::ready, future.wait_for(5s));
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_GE(dur.count(), 1000);
  EXPECT_LE(dur.count(), 1100);
  EXPECT_EQ(cfuture::future_status::ready, future.wait_for(0s));
  EXPECT_EQ(123, future.get());
  th.join();
}

TEST(BasicTest, WaitUntil) {
  using namespace std::chrono_literals;
  cfuture::Promise<int> p;
  auto start = std::chrono::steady_clock::now();
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    p.set_value(123);
  });
  cfuture::Future<int> future = p.get_future();
  ASSERT_EQ(cfuture::future_status::timeout, future.wait_until(std::chrono::system_clock::now() + 100ms));
  ASSERT_EQ(cfuture::future_status::ready, future.wait_until(std::chrono::system_clock::now() + 5s));
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_GE(dur.count(), 1000);
  EXPECT_LE(dur.count(), 1100);
  EXPECT_EQ(cfuture::future_status::ready, future.wait_for(0s));
  EXPECT_EQ(123, future.get());
  th.join();
}

TEST(BasicExceptionTest, SetException) {
  cfuture::Promise<int> p;
  p.set_exception(std::make_exception_ptr(std::runtime_error{"something wrong"}));

  auto future = p.get_future();
  EXPECT_THROW(
      {
        try {
          future.get();
        } catch (const std::runtime_error& e) {
          EXPECT_STREQ("something wrong", e.what());
          throw e;
        }
      },
      std::runtime_error);
}

TEST(BasicExceptionTest, SetExceptionWhenValueHasBeenSet) {
  cfuture::Promise<int> p;
  p.set_value(123);
  EXPECT_THROW(
      {
        try {
          p.set_exception(std::make_exception_ptr(std::runtime_error{"something wrong"}));
        } catch (const cfuture::future_error& e) {
          std::cout << e.code() << e.what() << '\n';
          throw e;
        }
      },
      cfuture::future_error);
}

TEST(BasicExceptionTest, SetValueWhenExceptionHasBeenSet) {
  cfuture::Promise<int> p;
  p.set_exception(std::make_exception_ptr(std::runtime_error{"something wrong"}));
  EXPECT_THROW(
      {
        try {
          p.set_value(123);
        } catch (const cfuture::future_error& e) {
          std::cout << e.code() << e.what() << '\n';
          throw e;
        }
      },
      cfuture::future_error);
}

TEST(BasicExceptionTest, SetValueTwice) {
  cfuture::Promise<int> p;
  p.set_value(123);
  EXPECT_THROW(
      {
        try {
          p.set_value(123);
        } catch (const cfuture::future_error& e) {
          std::cout << e.code() << e.what() << '\n';
          throw e;
        }
      },
      cfuture::future_error);
}

TEST(BasicExceptionTest, SetExceptionTwice) {
  cfuture::Promise<int> p;
  p.set_exception(std::make_exception_ptr(std::runtime_error{"something wrong"}));
  EXPECT_THROW(
      {
        try {
          p.set_exception(std::make_exception_ptr(std::runtime_error{"something wrong"}));
        } catch (const cfuture::future_error& e) {
          std::cout << e.code() << e.what() << '\n';
          throw e;
        }
      },
      cfuture::future_error);
}

TEST(BasicExceptionTest, BrokenPromise) {
  cfuture::Future<int> f;
  {
    cfuture::Promise<int> p;
    f = p.get_future();
  }
  EXPECT_THROW(
      {
        try {
          f.get();
        } catch (const cfuture::future_error& e) {
          std::cout << e.code() << e.what() << '\n';
          throw e;
        }
      },
      cfuture::future_error);
}

TEST(ContinuationTest, MakeStateReturnFuture) {
  using namespace std::chrono_literals;
  using namespace cfuture;
  std::shared_ptr<internal::shared_state<std::string>> new_state;
  {
    auto state = internal::shared_state<int>::make_new_state();
    new_state = state->make_continuation_shared_state([](Future<int> v) {
      cfuture::Promise<std::string> p;
      auto future = p.get_future();
      std::thread th([v = std::move(v), p = std::move(p)]() mutable {
        std::this_thread::sleep_for(100ms);
        p.set_value(std::to_string(v.get()));
      });
      th.detach();
      return future;
    });
    state->try_emplace_value(12345);
  }
  EXPECT_EQ("12345", new_state->get_value());
}

TEST(ContinuationTest, MakeStateReturnDirectly) {
  using namespace std::chrono_literals;
  using namespace cfuture;
  std::shared_ptr<internal::shared_state<std::string>> new_state;
  {
    auto state = internal::shared_state<int>::make_new_state();
    new_state = state->make_continuation_shared_state([](Future<int> v) { return std::to_string(v.get()); });
    state->try_emplace_value(12345);
  }
  EXPECT_EQ("12345", new_state->get_value());
}

TEST(ContinuationTest, FutureThen) {
  using namespace std::chrono_literals;
  using namespace cfuture;
  Promise<int> p;
  std::cout << "the main thread is " << std::this_thread::get_id() << '\n';
  std::thread th([&] {
    std::cout << "run in thread " << std::this_thread::get_id() << '\n';
    std::this_thread::sleep_for(100ms);
    p.set_value(1);
  });
  Future<std::string> f;
  f = p.get_future()
          .then([](Future<int> v) {
            Promise<int> p;
            auto future = p.get_future();
            std::thread th([v = v.get(), p = std::move(p)]() mutable {
              std::cout << "run in thread " << std::this_thread::get_id() << '\n';
              std::this_thread::sleep_for(100ms);
              p.set_value(v + 2);
            });
            th.detach();
            return future;
          })
          .then([](Future<int> v) -> long {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return v.get() + 3;
          })
          .then([](Future<long> v) {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return std::to_string(v.get() + 4);
          });
  EXPECT_EQ("10", f.get());
  th.join();
}

TEST(ContinuationTest, FutureThen_FirstStepDelayed) {
  using namespace std::chrono_literals;
  using namespace cfuture;
  Promise<int> p;
  Future<std::string> f;
  f = p.get_future()
          .then([](Future<int> v) {
            Promise<int> p;
            auto future = p.get_future();
            std::thread th([v = v.get(), p = std::move(p)]() mutable {
              std::cout << "run in thread " << std::this_thread::get_id() << '\n';
              std::this_thread::sleep_for(100ms);
              p.set_value(v + 2);
            });
            th.detach();
            return future;
          })
          .then([](Future<int> v) {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return v.get() + 3;
          })
          .then([](Future<int> v) {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return std::to_string(v.get() + 4);
          });
  ASSERT_EQ(future_status::timeout, f.wait_for(1s));
  p.set_value(1);
  EXPECT_EQ("10", f.get());
}

TEST(ContinuationTest, FutureThen_Exception) {
  auto promise = std::make_unique<cfuture::Promise<int>>();
  auto future = promise->get_future().then([](cfuture::Future<int> v) {
    // fo nothing;
  });
  delete promise.release();
  ASSERT_TRUE(future.valid());
  future.get();
}