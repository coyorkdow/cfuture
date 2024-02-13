#include <atomic>
#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace cfuture {

namespace internal {

template <class Tp, class Mem, typename std::enable_if<std::is_trivially_destructible<Tp>::value, int>::type = 0>
void destroy_at(Mem* addr) noexcept {}

template <class Tp, class Mem, typename std::enable_if<!std::is_trivially_destructible<Tp>::value, int>::type = 0>
void destroy_at(Mem* addr) noexcept(noexcept(reinterpret_cast<Tp*>(addr)->~Tp())) {
  reinterpret_cast<Tp*>(addr)->~Tp();
}

template <class R>
struct shared_state : std::enable_shared_from_this<shared_state<R>> {
  shared_state() = default;

 public:
  std::mutex mu_;
  std::condition_variable con_;

  uint8_t state_{0};

  enum : uint8_t {  // set the internal state
    constructed = 1,
    future_attached = 2,
    ready = 4,
    deferred = 8
  };

  std::aligned_storage_t<sizeof(R), alignof(R)> mem_{0};

  static std::shared_ptr<shared_state> create_instance() { return std::make_shared<shared_state>(); }

  ~shared_state() {
    if (has_value()) {
      destroy_at<R>(&mem_);
    }
  }

  bool has_value() const noexcept { return state_ & constructed; }

  bool attach_future() noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    bool has_future_attached = (state_ & future_attached) != 0;
    state_ |= future_attached;
    return has_future_attached;
  }

  template <class... Args>
  bool emplace_value(Args&&... args) noexcept(noexcept(new(&mem_) R(std::forward<Args>(args)...))) {
    {
      std::unique_lock<std::mutex> lk;
      if (!has_value()) {
        return false;
      }
      new (&mem_) R(std::forward<Args>(args)...);
    }
    con_.notify_all();
    return true;
  }
};

}  // namespace internal

template <class R>
class future;

template <class R>
class promise {
  using shared_state = internal::shared_state<R>;

 public:
  promise() : shared_s_(shared_state::create_instance()) {}

  template <class Tp, typename std::enable_if<std::is_constructible<R, Tp>::value, int>::type = 0>
  bool set_value(Tp&& val) noexcept(noexcept(shared_s_->emplace_value(std::forward<Tp>(val)))) {
    return shared_s_->emplace_value(std::forward<Tp>(val));
  }

 private:
  std::shared_ptr<shared_state> shared_s_;
};

}  // namespace cfuture

int main() {
  { std::promise<int> p; }

  cfuture::promise<int> p;
  p.set_value(12);
}