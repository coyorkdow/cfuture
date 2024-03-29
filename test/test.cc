#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

#include "cfuture.hpp"
#include "gtest/gtest.h"

TEST(BasicTest, Get) {
  using namespace std::chrono_literals;
  cfuture::promise<std::string> p;
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
  cfuture::promise<void> p;
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
  cfuture::promise<int> p;
  auto start = std::chrono::steady_clock::now();
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    p.set_value(123);
  });
  cfuture::future<int> future = p.get_future();
  future.wait();
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_GE(dur.count(), 1000);
  EXPECT_LE(dur.count(), 1100);
  EXPECT_EQ(123, future.get_or(0));
  th.join();
}

TEST(BasicTest, WaitFor) {
  using namespace std::chrono_literals;
  cfuture::promise<int> p;
  auto start = std::chrono::steady_clock::now();
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    p.set_value(123);
  });
  cfuture::future<int> future = p.get_future();
  ASSERT_EQ(cfuture::future_status::timeout, future.wait_for(1ms));
  ASSERT_EQ(0, future.get_or(0));
  ASSERT_EQ(cfuture::future_status::ready, future.wait_for(5s));
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_GE(dur.count(), 1000);
  EXPECT_LE(dur.count(), 1100);
  EXPECT_EQ(123, future.get_or(0));
  th.join();
}

TEST(BasicTest, WaitUntil) {
  using namespace std::chrono_literals;
  cfuture::promise<int> p;
  auto start = std::chrono::steady_clock::now();
  std::thread th([&] {
    std::this_thread::sleep_for(1s);
    p.set_value(123);
  });
  cfuture::future<int> future = p.get_future();
  ASSERT_EQ(cfuture::future_status::timeout, future.wait_until(std::chrono::system_clock::now() + 100ms));
  ASSERT_EQ(0, future.get_or(0));
  ASSERT_EQ(cfuture::future_status::ready, future.wait_until(std::chrono::system_clock::now() + 5s));
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_GE(dur.count(), 1000);
  EXPECT_LE(dur.count(), 1100);
  EXPECT_EQ(123, future.get_or(0));
  th.join();
}

TEST(BasicExceptionTest, SetException) {
  cfuture::promise<int> p;
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
  cfuture::promise<int> p;
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
  cfuture::promise<int> p;
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
  cfuture::promise<int> p;
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
  cfuture::promise<int> p;
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
  cfuture::future<int> f;
  {
    cfuture::promise<int> p;
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
    new_state = state->make_continuation_shared_state([](int v) {
      cfuture::promise<std::string> p;
      auto future = p.get_future();
      std::thread th([v, p = std::move(p)]() mutable {
        std::this_thread::sleep_for(100ms);
        p.set_value(std::to_string(v));
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
    new_state = state->make_continuation_shared_state([](int v) { return std::to_string(v); });
    state->try_emplace_value(12345);
  }
  EXPECT_EQ("12345", new_state->get_value());
}

TEST(ContinuationTest, FutureThen) {
  using namespace std::chrono_literals;
  using namespace cfuture;
  promise<int> p;
  std::cout << "the main thread is " << std::this_thread::get_id() << '\n';
  std::thread th([&] {
    std::cout << "run in thread " << std::this_thread::get_id() << '\n';
    std::this_thread::sleep_for(100ms);
    p.set_value(1);
  });
  future<std::string> f;
  f = p.get_future()
          .then([](int v) {
            promise<int> p;
            auto future = p.get_future();
            std::thread th([v, p = std::move(p)]() mutable {
              std::cout << "run in thread " << std::this_thread::get_id() << '\n';
              std::this_thread::sleep_for(100ms);
              p.set_value(v + 2);
            });
            th.detach();
            return future;
          })
          .then([](int v) {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return v + 3;
          })
          .then([](int v) {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return std::to_string(v + 4);
          });
  EXPECT_EQ("10", f.get());
  th.join();
}

TEST(ContinuationTest, FutureThen_FirstStepDelayed) {
  using namespace std::chrono_literals;
  using namespace cfuture;
  promise<int> p;
  future<std::string> f;
  f = p.get_future()
          .then([](int v) {
            promise<int> p;
            auto future = p.get_future();
            std::thread th([v, p = std::move(p)]() mutable {
              std::cout << "run in thread " << std::this_thread::get_id() << '\n';
              std::this_thread::sleep_for(100ms);
              p.set_value(v + 2);
            });
            th.detach();
            return future;
          })
          .then([](int v) {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return v + 3;
          })
          .then([](int v) {
            std::cout << "run in thread " << std::this_thread::get_id() << '\n';
            std::this_thread::sleep_for(1ms);
            return std::to_string(v + 4);
          });
  ASSERT_EQ("", f.get_or(""));
  ASSERT_EQ(future_status::timeout, f.wait_for(1s));
  p.set_value(1);
  EXPECT_EQ("10", f.get());
}