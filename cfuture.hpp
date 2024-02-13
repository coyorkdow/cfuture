#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace cfuture {

using std::future_status;

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

  std::shared_ptr<shared_state> attach_future() noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    bool has_future_attached = (state_ & future_attached) != 0;
    state_ |= future_attached;
    if (has_future_attached) {
      return {};
    }
    return this->shared_from_this();
  }

  template <class... Args>
  bool emplace_value(Args&&... args) noexcept(noexcept(R(std::forward<Args>(args)...))) {
    {
      std::unique_lock<std::mutex> lk(mu_);
      if (has_value()) {
        return false;
      }
      new (&mem_) R(std::forward<Args>(args)...);
      state_ |= constructed;
    }
    con_.notify_all();
    return true;
  }

  R& get_value() {
    std::unique_lock<std::mutex> lk(mu_);
    while (!has_value()) {
      con_.wait(lk);
    }
    return *reinterpret_cast<R*>(&mem_);
  }

  void wait() {
    std::unique_lock<std::mutex> lk(mu_);
    while (!has_value()) {
      con_.wait(lk);
    }
  }

  template <class Clock, class Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>& abs_time) {
    std::unique_lock<std::mutex> lk(mu_);
    if (state_ & deferred) {
      return future_status::deferred;
    }
    while (!(has_value()) && Clock::now() < abs_time) {
      con_.wait_until(lk, abs_time);
    }
    if (has_value()) {
      return future_status::ready;
    }
    return future_status::timeout;
  }

  template <class Rep, class Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) {
    return wait_until(std::chrono::steady_clock::now() + rel_time);
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
  promise(promise&&) noexcept = default;
  promise& operator=(promise&&) noexcept = default;

  promise(const promise&) = delete;
  promise& operator=(const promise&) = delete;

  template <class Tp, typename std::enable_if<std::is_constructible<R, Tp>::value, int>::type = 0>
  bool set_value(Tp&& val) noexcept(noexcept(std::declval<shared_state>().emplace_value(std::forward<Tp>(val)))) {
    return shared_s_->emplace_value(std::forward<Tp>(val));
  }

  future<R> get_future();

 private:
  std::shared_ptr<shared_state> shared_s_;
};

template <class R>
class future {
  using shared_state = internal::shared_state<R>;

  template <class>
  friend class promise;

 public:
  future() noexcept = default;
  future(future&&) noexcept = default;
  future& operator=(future&&) noexcept = default;

  future(const future&) = delete;
  future& operator=(const future&) = delete;

  bool valid() const noexcept { return static_cast<bool>(shared_s_); }

  R get() {
    R value = std::move(shared_s_->get_value());
    shared_s_.reset();
    return value;
  }

  void wait() const { shared_s_->wait(); }

  template <class Clock, class Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>& abs_time) const {
    return shared_s_->wait_until(abs_time);
  }

  template <class Rep, class Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) const {
    return shared_s_->wait_for(rel_time);
  }

 private:
  explicit future(std::shared_ptr<shared_state> s) noexcept : shared_s_(std::move(s)) {}

  std::shared_ptr<shared_state> shared_s_;
};

template <class R>
future<R> promise<R>::get_future() {
  auto s = shared_s_->attach_future();
  if (s) {
    return future<R>{s};
  }
  return {};
}

}  // namespace cfuture
