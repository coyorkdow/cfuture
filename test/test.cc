#include <memory>
#include <string>
#include <thread>
#include <type_traits>

#include "cfuture.hpp"
#include "gtest/gtest.h"

TEST(BasicFut, Get) {
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

TEST(ContinuationTest, MakeState) {
  using namespace cfuture;
  auto state = internal::shared_state<int>::make_new_state();
  auto new_state = state->make_continuation_shared_state([](int v) {
    cfuture::promise<std::string> p;
    auto future = p.get_future();
    std::thread th([v, p = std::move(p)]() mutable {
      ////
      p.set_value(std::to_string(v));
    });
    th.detach();
    return future;
  });
  static_assert(std::is_same<decltype(new_state), std::shared_ptr<internal::shared_state<std::string>>>::value, "");
  state->emplace_value(12345);
  EXPECT_EQ("12345", new_state->get_value());
}