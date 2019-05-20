#pragma once

#include "cista/offset_t.h"

namespace cista {

template <typename T, typename DataType, typename Enable = void>
struct offset_ptr {
  static constexpr auto const NULLPTR_OFFSET =
      std::numeric_limits<DataType>::min();

  offset_ptr() = default;
  offset_ptr(std::nullptr_t) : offset_{NULLPTR_OFFSET} {}
  offset_ptr(T const* p) : offset_{ptr_to_offset(p)} {}

  offset_ptr& operator=(T const* p) {
    offset_ = ptr_to_offset(p);
    return *this;
  }
  offset_ptr& operator=(std::nullptr_t) {
    offset_ = NULLPTR_OFFSET;
    return *this;
  }

  offset_ptr(offset_ptr const& o) : offset_ptr{o.get()} {}
  offset_ptr(offset_ptr&& o) : offset_ptr{o.get()} {}
  offset_ptr& operator=(offset_ptr const& o) {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }
  offset_ptr& operator=(offset_ptr&& o) {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }

  DataType ptr_to_offset(T const* p) const {
    if (p == nullptr) {
      return NULLPTR_OFFSET;
    }
    auto v = reinterpret_cast<intptr_t>(p) - reinterpret_cast<intptr_t>(this);
    if (v > std::numeric_limits<DataType>::max() ||
        v < std::numeric_limits<DataType>::min()) {
      printf("ERROR: this=%p -> ptr=%p -> offset=%ld\n", this, p, v);
      throw std::runtime_error("nooo");
    }
    auto const r =
        p == nullptr ? NULLPTR_OFFSET
                     : static_cast<DataType>(reinterpret_cast<uintptr_t>(p) -
                                             reinterpret_cast<uintptr_t>(this));
    printf("this=%p -> ptr=%p -> offset=%d\n", this, p, r);
    return r;
  }

  operator T*() { return get(); }
  operator T const*() const { return get(); }

  T& operator*() { return *get(); }
  T const& operator*() const { return *get(); }

  T* operator->() { return get(); }
  T const* operator->() const { return get(); }

  T const* get() const {
    auto const ptr = offset_ == NULLPTR_OFFSET
                         ? nullptr
                         : reinterpret_cast<T const*>(
                               reinterpret_cast<intptr_t>(this) + offset_);
    return ptr;
  }
  T* get() {
    auto const ptr =
        offset_ == NULLPTR_OFFSET
            ? nullptr
            : reinterpret_cast<T*>(reinterpret_cast<intptr_t>(this) + offset_);
    return ptr;
  }

  template <typename Int>
  offset_ptr operator+(Int i) const {
    offset_ptr r = *this;
    r.offset_ += i * sizeof(T);
    return r;
  }

  offset_ptr& operator++() {
    offset_ += sizeof(T);
    return *this;
  }

  offset_ptr& operator--() {
    offset_ -= sizeof(T);
    return *this;
  }

  offset_ptr operator++(int) const {
    offset_ptr r = *this;
    r.offset_ += sizeof(T);
    return r;
  }

  offset_ptr operator--(int) const {
    offset_ptr r = *this;
    r.offset_ -= sizeof(T);
    return r;
  }

  friend bool operator==(std::nullptr_t, offset_ptr const& o) {
    return o.offset_ == NULLPTR_OFFSET;
  }
  friend bool operator==(offset_ptr const& o, std::nullptr_t) {
    return o.offset_ == NULLPTR_OFFSET;
  }
  friend bool operator!=(std::nullptr_t, offset_ptr const& o) {
    return o.offset_ != NULLPTR_OFFSET;
  }
  friend bool operator!=(offset_ptr const& o, std::nullptr_t) {
    return o.offset_ != NULLPTR_OFFSET;
  }

  DataType offset_;
};

template <typename T, typename DataType>
struct offset_ptr<T, DataType, std::enable_if_t<std::is_same_v<void, T>>> {
  static constexpr auto const NULLPTR_OFFSET =
      std::numeric_limits<DataType>::min();

  offset_ptr() = default;
  offset_ptr(std::nullptr_t) : offset_{NULLPTR_OFFSET} {}
  offset_ptr(T const* p) : offset_{ptr_to_offset(p)} {}

  offset_ptr& operator=(T const* p) {
    offset_ = ptr_to_offset(p);
    return *this;
  }
  offset_ptr& operator=(std::nullptr_t) {
    offset_ = NULLPTR_OFFSET;
    return *this;
  }

  offset_ptr(offset_ptr const& o) : offset_ptr{o.get()} {}
  offset_ptr(offset_ptr&& o) : offset_ptr{o.get()} {}
  offset_ptr& operator=(offset_ptr const& o) {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }
  offset_ptr& operator=(offset_ptr&& o) {
    offset_ = ptr_to_offset(o.get());
    return *this;
  }

  DataType ptr_to_offset(T const* p) const {
    return p == nullptr
               ? NULLPTR_OFFSET
               : static_cast<DataType>(reinterpret_cast<uintptr_t>(p) -
                                       reinterpret_cast<uintptr_t>(this));
  }

  operator T*() { return get(); }
  operator T const*() const { return get(); }

  T* operator->() { return get(); }
  T const* operator->() const { return get(); }

  T const* get() const {
    auto const ptr = offset_ == NULLPTR_OFFSET
                         ? nullptr
                         : reinterpret_cast<T const*>(
                               reinterpret_cast<intptr_t>(this) + offset_);
    return ptr;
  }
  T* get() {
    auto const ptr =
        offset_ == NULLPTR_OFFSET
            ? nullptr
            : reinterpret_cast<T*>(reinterpret_cast<intptr_t>(this) + offset_);
    return ptr;
  }

  friend bool operator==(std::nullptr_t, offset_ptr const& o) {
    return o.offset_ == NULLPTR_OFFSET;
  }
  friend bool operator==(offset_ptr const& o, std::nullptr_t) {
    return o.offset_ == NULLPTR_OFFSET;
  }
  friend bool operator!=(std::nullptr_t, offset_ptr const& o) {
    return o.offset_ != NULLPTR_OFFSET;
  }
  friend bool operator!=(offset_ptr const& o, std::nullptr_t) {
    return o.offset_ != NULLPTR_OFFSET;
  }

  DataType offset_;
};

}  // namespace cista