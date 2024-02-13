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
