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
#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace cfuture {

enum class future_errc {
  no_err = 255,
  future_already_retrieved = static_cast<int>(std::future_errc::future_already_retrieved),
  promise_already_satisfied = static_cast<int>(std::future_errc::promise_already_satisfied),
  no_state = static_cast<int>(std::future_errc::no_state),
  broken_promise = static_cast<int>(std::future_errc::broken_promise)
};

enum class future_status {
  ready = static_cast<int>(std::future_status::ready),
  timeout = static_cast<int>(std::future_status::timeout),
  deferred = static_cast<int>(std::future_status::deferred)
};

class future_error : public std::logic_error {
 public:
  explicit future_error(future_errc errc)
      : future_error(std::error_code(static_cast<int>(errc), std::future_category())) {}

  virtual ~future_error() noexcept {}

  virtual const char* what() const noexcept { return std::logic_error::what(); }

  const std::error_code& code() const noexcept { return code_; }

 private:
  explicit future_error(std::error_code ec) : std::logic_error("std::future_error: " + ec.message()), code_(ec) {}

  std::error_code code_;
};

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
  std::exception_ptr exception_;
  std::mutex mu_;
  std::condition_variable con_;

  uint8_t state_{0};
  future_errc errc_{future_errc::no_err};

  enum : uint8_t {  // set the internal state
    constructed = 1,
    future_attached = 2,
    // ready = 4, // unused
    deferred = 8
  };

  std::aligned_storage_t<sizeof(R), alignof(R)> mem_{0};

  std::function<void(shared_state*)> on_satisfied_;

  static std::shared_ptr<shared_state> make_new_state() { return std::make_shared<shared_state>(); }

  ~shared_state() {
    if (has_value()) {
      destroy_at<R>(&mem_);
    }
  }

  bool has_value() const noexcept { return state_ & constructed; }

  bool ready() const noexcept { return has_value() && !exception_ && errc_ == future_errc::no_err; }

  bool satisfied() const noexcept { return has_value() || exception_ || errc_ != future_errc::no_err; }

  void maybe_throw_exception() {
    if (errc_ != future_errc::no_err) {
      throw future_error{errc_};
    }
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

  std::shared_ptr<shared_state> attach_future() noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    bool has_future_attached = (state_ & future_attached) != 0;
    state_ |= future_attached;
    if (has_future_attached) {
      return {};
    }
    return this->shared_from_this();
  }

  bool set_exception(std::exception_ptr p) {
    std::unique_lock<std::mutex> lk(mu_);
    if (satisfied()) {
      return false;
    }
    exception_ = p;
    if (on_satisfied_) {
      on_satisfied_(this);
    }
    return true;
  }

  bool set_exception(future_errc errc) {
    if (errc == future_errc::no_state) {
      std::abort();
    }
    std::unique_lock<std::mutex> lk(mu_);
    if (satisfied()) {
      return false;
    }
    errc_ = errc;
    if (on_satisfied_) {
      on_satisfied_(this);
    }
    return true;
  }

  template <class... Args>
  bool emplace_value(Args&&... args) {
    {
      std::unique_lock<std::mutex> lk(mu_);
      if (satisfied()) {
        return false;
      }
      new (&mem_) R(std::forward<Args>(args)...);
      state_ |= constructed;
      if (on_satisfied_) {
        on_satisfied_(this);
      }
    }
    con_.notify_all();
    return true;
  }

  R& get_value() {
    std::unique_lock<std::mutex> lk(mu_);
    while (!satisfied()) {
      con_.wait(lk);
    }
    maybe_throw_exception();
    return *reinterpret_cast<R*>(&mem_);
  }

  R& get_value_unsafe() {
    maybe_throw_exception();
    return *reinterpret_cast<R*>(&mem_);
  }

  void set_on_satisfied_(std::function<void(shared_state*)> callback) noexcept {
    // not thread safe
    if (satisfied()) {
      callback(this);
    } else {
      on_satisfied_ = std::move(callback);
    }
  }

  template <class Fn, typename std::enable_if<is_future<invoke_result_t<Fn, R>>::value, int>::type = 0>
  auto make_continuation_shared_state(Fn&& attached_fn)
      -> std::shared_ptr<shared_state<unwrap_future_t<invoke_result_t<Fn, R>>>>;

  void wait() {
    std::unique_lock<std::mutex> lk(mu_);
    while (!ready()) {
      con_.wait(lk);
    }
    maybe_throw_exception();
  }

  template <class Clock, class Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>& abs_time) {
    std::unique_lock<std::mutex> lk(mu_);
    if (state_ & deferred) {
      return future_status::deferred;
    }
    while (!(ready()) && Clock::now() < abs_time) {
      con_.wait_until(lk, abs_time);
    }
    maybe_throw_exception();
    if (ready()) {
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

#define THROW_IF_NO_STATE_()                     \
  do {                                           \
    if (!shared_s_) {                            \
      throw future_error{future_errc::no_state}; \
    }                                            \
  } while (0)

template <class R>
class promise {
  using shared_state = internal::shared_state<R>;

 public:
  promise() : shared_s_(shared_state::make_new_state()) {}
  promise(promise&&) noexcept = default;
  promise& operator=(promise&&) noexcept = default;

  promise(const promise&) = delete;
  promise& operator=(const promise&) = delete;

  ~promise() noexcept {
    if (shared_s_) {
      shared_s_->set_exception(future_errc::broken_promise);
    }
  }

  template <class Tp, typename std::enable_if<std::is_constructible<R, Tp>::value, int>::type = 0>
  void set_value(Tp&& val) {
    THROW_IF_NO_STATE_();
    if (!shared_s_->emplace_value(std::forward<Tp>(val))) {
      throw future_error{future_errc::promise_already_satisfied};
    }
  }

  void set_exception(std::exception_ptr p) {
    THROW_IF_NO_STATE_();
    if (!shared_s_->set_exception(p)) {
      throw future_error{future_errc::promise_already_satisfied};
    }
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
    THROW_IF_NO_STATE_();
    R value = std::move(shared_s_->get_value());
    shared_s_.reset();
    return value;
  }

  R get_or(R alter_value) noexcept {
    // must do not sleep
    try {
      if (wait_until(std::chrono::system_clock::now() - std::chrono::seconds(1)) == future_status::ready) {
        return get();
      }
    } catch (...) {
    }
    return alter_value;
  }

  void wait() const {
    THROW_IF_NO_STATE_();
    shared_s_->wait();
  }

  template <class Clock, class Duration>
  future_status wait_until(const std::chrono::time_point<Clock, Duration>& abs_time) const {
    THROW_IF_NO_STATE_();
    return shared_s_->wait_until(abs_time);
  }

  template <class Rep, class Period>
  future_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) const {
    THROW_IF_NO_STATE_();
    return shared_s_->wait_for(rel_time);
  }

 private:
  explicit future(std::shared_ptr<shared_state> s) noexcept : shared_s_(std::move(s)) {}

  std::shared_ptr<shared_state> shared_s_;
};

template <class R>
future<R> promise<R>::get_future() {
  THROW_IF_NO_STATE_();
  auto s = shared_s_->attach_future();
  if (s) {
    return future<R>{s};
  }
  throw future_error{future_errc::future_already_retrieved};
}

#undef THROW_IF_NO_STATE_

namespace internal {

// The promise object might be no longer exist once the promise is satisfied
// (a.k.a, shared state state satisfied).

// The on_satisfied_ callback will run immediately if the shared state is already satisfied.

// The future object might be no longer exist once the continuation attached. But when
// attaching the continuation, the future object is always alive.

template <class R>
template <class Fn, typename std::enable_if<is_future<invoke_result_t<Fn, R>>::value, int>::type>
auto shared_state<R>::make_continuation_shared_state(Fn&& attached_fn)
    -> std::shared_ptr<shared_state<unwrap_future_t<invoke_result_t<Fn, R>>>> {
  using result_t = unwrap_future_t<invoke_result_t<Fn, R>>;
  std::shared_ptr<shared_state<result_t>> new_state = shared_state<result_t>::make_new_state();

  std::unique_lock<std::mutex> lk(mu_);
  set_on_satisfied_([new_state, attached_fn = std::forward<Fn>(attached_fn)](shared_state<R>* self) {
    using future_t = invoke_result_t<Fn, R>;
    static_assert(std::is_same<future_t, future<result_t>>::value, "you have made something wrong!");
    assert(self->satisfied());
    if (!self->ready()) {
      if (self->errc_ != future_errc::no_err) {
        new_state->set_exception(self->errc_);
      } else {
        new_state->set_exception(self->exception_);
      }
      return;
    }

    future_t wrapped_future = attached_fn(std::move(self->get_value_unsafe()));
    std::unique_lock<std::mutex> lk(wrapped_future.shared_s_->mu_);
    wrapped_future.shared_s_->set_on_satisfied_(
        [new_state = std::move(new_state)](shared_state<result_t>* wrapped_state_self) {
          assert(wrapped_state_self->satisfied());
          if (!wrapped_state_self->ready()) {
            if (wrapped_state_self->errc_ != future_errc::no_err) {
              new_state->set_exception(wrapped_state_self->errc_);
            } else {
              new_state->set_exception(wrapped_state_self->exception_);
            }
            return;
          }
          new_state->emplace_value(std::move(wrapped_state_self->get_value_unsafe()));
        });
  });

  return new_state;
}

}  // namespace internal

}  // namespace cfuture
