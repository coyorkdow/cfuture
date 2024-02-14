// MIT License
//
// Copyright (c) 2024 youtao guo
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace cfuture {

using std::future_status;

template <class R>
class promise;

template <class R>
class future;

namespace internal {

#if __cplusplus < 201703L
template <class R, class... Args>
struct invoke_result {
  using type = typename std::result_of<R(Args...)>::type;
};

template <class R, class... Args>
using invoke_result_t = typename invoke_result<R, Args...>::type;
#else
using std::invoke_result;
using std::invoke_result_t;
#endif

static_assert(std::is_same<int, invoke_result_t<std::function<int()>>>::value, "");
static_assert(std::is_same<float, invoke_result_t<std::function<float(double, int)>, double, int>>::value, "");

template <class Tp, class Mem, typename std::enable_if<std::is_trivially_destructible<Tp>::value, int>::type = 0>
void destroy_at(Mem* addr) noexcept {}

template <class Tp, class Mem, typename std::enable_if<!std::is_trivially_destructible<Tp>::value, int>::type = 0>
void destroy_at(Mem* addr) noexcept(noexcept(reinterpret_cast<Tp*>(addr)->~Tp())) {
  reinterpret_cast<Tp*>(addr)->~Tp();
}

template <class>
struct is_future : std::false_type {};

template <class R>
struct is_future<future<R>> : std::true_type {};

template <class>
struct unwrap_future {};

template <class R>
struct unwrap_future<future<R>> {
  using type = R;
};

template <class Tp>
using unwrap_future_t = typename unwrap_future<Tp>::type;

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
    // ready = 4, // unused
    deferred = 8
  };

  std::aligned_storage_t<sizeof(R), alignof(R)> mem_{0};

  std::function<void(shared_state*)> on_value_set_;

  static std::shared_ptr<shared_state> make_new_state() { return std::make_shared<shared_state>(); }

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
  bool emplace_value(Args&&... args) {
    {
      std::unique_lock<std::mutex> lk(mu_);
      if (has_value()) {
        return false;
      }
      new (&mem_) R(std::forward<Args>(args)...);
      state_ |= constructed;
      if (on_value_set_) {
        on_value_set_(this);
      }
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

  R& get_value_unsafe() { return *reinterpret_cast<R*>(&mem_); }

  void set_on_value_set(std::function<void(shared_state*)> callback) {
    // not thread safe
    if (has_value()) {
      callback(this);
    } else {
      on_value_set_ = std::move(callback);
    }
  }

  template <class Fn, typename std::enable_if<is_future<invoke_result_t<Fn, R>>::value, int>::type = 0>
  auto make_continuation_shared_state(Fn&& attached_fn)
      -> std::shared_ptr<shared_state<unwrap_future_t<invoke_result_t<Fn, R>>>>;

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
class promise {
  using shared_state = internal::shared_state<R>;

 public:
  promise() : shared_s_(shared_state::make_new_state()) {}
  promise(promise&&) noexcept = default;
  promise& operator=(promise&&) noexcept = default;

  promise(const promise&) = delete;
  promise& operator=(const promise&) = delete;

  template <class Tp, typename std::enable_if<std::is_constructible<R, Tp>::value, int>::type = 0>
  bool set_value(Tp&& val) {
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

  template <class>
  friend struct internal::shared_state;

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

  R get_or(R alter_value) {
    // must do not sleep
    if (wait_until(std::chrono::system_clock::now() - std::chrono::seconds(1)) == future_status::ready) {
      return get();
    }
    return alter_value;
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

namespace internal {

template <class R>
template <class Fn, typename std::enable_if<is_future<invoke_result_t<Fn, R>>::value, int>::type>
auto shared_state<R>::make_continuation_shared_state(Fn&& attached_fn)
    -> std::shared_ptr<shared_state<unwrap_future_t<invoke_result_t<Fn, R>>>> {
  using result_t = unwrap_future_t<invoke_result_t<Fn, R>>;
  std::shared_ptr<shared_state<result_t>> new_state = shared_state<result_t>::make_new_state();

  std::unique_lock<std::mutex> lk(mu_);
  set_on_value_set([new_state, attached_fn = std::forward<Fn>(attached_fn)](shared_state<R>* self) {
    using future_t = invoke_result_t<Fn, R>;
    static_assert(std::is_same<future_t, future<result_t>>::value, "you have made something wrong!");
    assert(self->has_value());
    future_t wrapped_future = attached_fn(std::move(self->get_value_unsafe()));
    std::unique_lock<std::mutex> lk(wrapped_future.shared_s_->mu_);
    wrapped_future.shared_s_->set_on_value_set([=](shared_state<result_t>* wrapped_state_self) {
      new_state->emplace_value(std::move(wrapped_state_self->get_value_unsafe()));
    });
  });

  return new_state;
}

}  // namespace internal

}  // namespace cfuture
