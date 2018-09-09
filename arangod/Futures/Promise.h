////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2018 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_FUTURES_PROMISE_H
#define ARANGOD_FUTURES_PROMISE_H 1

#include <future>

#include "Futures/SharedState.h"

namespace arangodb {
namespace futures {
  
template <typename T>
class Future;
  
/// producer side of future-promise pair
/// accesses on Promise have to be synchronized externally to
/// be thread-safe
template<typename T>
class Promise {
public:
  
  /// make invalid promise
  static Promise<T> makeEmpty() {
    return Promise(nullptr);
  }
  
  /// @brief Constructs a Promise with no shared state.
  /// After construction, valid() == true.
  Promise() : _state(detail::SharedState<T>::make()), _retrieved(false) {}
  
  Promise(Promise const& o) = delete;
  Promise(Promise<T>&& o) noexcept
    : _state(std::move(o._state)), _retrieved(o._retrieved) {
      o._state = nullptr;
    }
  
  ~Promise() {
    this->detach();
  }
  
  Promise& operator=(Promise const&) = delete;
  Promise& operator=(Promise<T>&& o) noexcept {
    detach();
    _state = std::move(o._state);
    _retrieved = o._retrieved;
    o._state = nullptr;
    return *this;
  }
  
  bool valid() const noexcept {
    return _state != nullptr;
  }
  
  bool isFulfilled() const noexcept {
    if (_state) {
      return _state->hasResult();
    }
    return true;
  }
  
  /// Fulfill the Promise with an exception_ptr.
  void set_exception(std::exception_ptr ep) {
    setTry(Try<T>(ep));
  }
  
  /// Fulfill the Promise with exception `e` *as if* by
  ///   `set_exception(std::make_exception_ptr<E>(e))`.
  template <class E>
  typename std::enable_if<std::is_base_of<std::exception, E>::value>::type
  set_exception(E const& e) {
    set_exception(std::make_exception_ptr<E>(e));
  }
  
  /// Fulfill the Promise with the specified value using perfect forwarding.
  /// Functionally equivalent to `setTry(Try<T>(std::forward<M>(value)))`
  template <class M>
  void set_value(M&& value) {
    static_assert(!std::is_same<T, void>::value, "Use set_value() instead");
    setTry(Try<T>(std::forward<M>(value)));
  }
  
  /// set void value
  template <class B = T>
  typename std::enable_if<std::is_same<void, B>::value>::type set_value() {
    setTry(Try<void>());
  }
  
  /// Fulfill the Promise with the specified Try (value or exception).
  void setTry(Try<T>&& t) {
    throwIfFulfilled();
    getState().setResult(std::move(t));
  }
  
  /// Fulfill this Promise with the result of a function that takes no
  ///   arguments and returns something implicitly convertible to T.
  template <class F>
  void setWith(F&& func) {
    throwIfFulfilled();
    getState().setResult(makeTryWith(std::forward<F>(func)));
  }
  
  arangodb::futures::Future<T> get_future();
  
private:
  Promise(detail::SharedState<T>* state) : _state(state), _retrieved(false) {}
  
  // convenience method that checks if _state is set
  inline detail::SharedState<T>& getState() {
    if (!_state) {
      throw std::future_error(std::future_errc::no_state);
    }
    return *_state;
  }
  
  inline void throwIfFulfilled() const {
    if (isFulfilled()) {
      throw std::future_error(std::future_errc::promise_already_satisfied);
    }
  }
  
  void detach() {
    if (_state) {
      if (!_retrieved) {
        _state->detachFuture();
      }
      if (!_state->hasResult()) {
        auto ptr = std::make_exception_ptr(std::future_error(std::future_errc::broken_promise));
        _state->setResult(Try<T>(std::move(ptr)));
      }
      _state->detachPromise();
      _state = nullptr;
    }
  }
  
private:
  detail::SharedState<T>* _state;
  /// Whether the Future has been retrieved (a one-time operation)
  bool _retrieved;
};
}}
#endif // ARANGOD_FUTURES_PROMISE_H

#include "Future.h"
#include "Promise-inl.h"
