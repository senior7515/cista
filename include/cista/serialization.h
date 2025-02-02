#pragma once

#include <limits>
#include <map>
#include <vector>

#include "cista/containers.h"
#include "cista/decay.h"
#include "cista/endian/conversion.h"
#include "cista/hash.h"
#include "cista/mode.h"
#include "cista/offset_t.h"
#include "cista/reflection/for_each_field.h"
#include "cista/serialized_size.h"
#include "cista/targets/buf.h"
#include "cista/targets/file.h"
#include "cista/type_hash/type_hash.h"
#include "cista/verify.h"

#ifndef cista_member_offset
#define cista_member_offset(s, m) (static_cast<cista::offset_t>(offsetof(s, m)))
#endif

namespace cista {

// =============================================================================
// SERIALIZE
// -----------------------------------------------------------------------------
struct pending_offset {
  void const* origin_ptr_;
  offset_t pos_;
};

template <typename Target, mode Mode>
struct serialization_context {
  static constexpr auto const MODE = Mode;

  explicit serialization_context(Target& t) : t_{t} {}

  offset_t write(void const* ptr, std::size_t const size,
                 std::size_t const alignment = 0) {
    return t_.write(ptr, size, alignment);
  }

  template <typename T>
  void write(offset_t const pos, T const& val) {
    t_.write(static_cast<std::size_t>(pos), val);
  }

  uint64_t checksum(offset_t const from) const { return t_.checksum(from); }

  std::map<void const*, offset_t> offsets_;
  std::vector<pending_offset> pending_;
  Target& t_;
};

template <typename Ctx, typename T>
void serialize(Ctx& c, T const* origin, offset_t const pos) {
  using Type = decay_t<T>;
  if constexpr (std::is_union_v<Type>) {
    static_assert(std::is_standard_layout_v<Type> &&
                  std::is_trivially_copyable_v<Type>);
  } else if constexpr (is_pointer_v<Type>) {
    if (*origin == nullptr) {
      c.write(pos, convert_endian<Ctx::MODE>(NULLPTR_OFFSET));
    } else if (auto const it = c.offsets_.find(*origin);
               it != end(c.offsets_)) {
      c.write(pos, convert_endian<Ctx::MODE>(it->second - pos));
    } else {
      c.pending_.emplace_back(pending_offset{*origin, pos});
    }
  } else if constexpr (!std::is_scalar_v<Type>) {
    static_assert(std::is_aggregate_v<Type> &&
                      std::is_standard_layout_v<Type> &&
                      !std::is_polymorphic_v<Type>,
                  "Please implement custom serializer.");
    for_each_ptr_field(*origin, [&](auto& member) {
      auto const member_offset =
          static_cast<offset_t>(reinterpret_cast<intptr_t>(member) -
                                reinterpret_cast<intptr_t>(origin));
      serialize(c, member, pos + member_offset);
    });
  } else if constexpr (std::numeric_limits<Type>::is_integer ||
                       std::is_floating_point_v<Type>) {
    c.write(pos, convert_endian<Ctx::MODE>(*origin));
  } else {
    (void)origin;
    (void)pos;
  }
}

template <typename Ctx, typename T, typename Ptr, typename TemplateSizeType>
void serialize(Ctx& c, basic_vector<T, Ptr, TemplateSizeType> const* origin,
               offset_t const pos) {
  auto const size = serialized_size<T>() * origin->used_size_;
  auto const start = origin->el_ == nullptr
                         ? NULLPTR_OFFSET
                         : c.write(static_cast<T const*>(origin->el_), size,
                                   std::alignment_of_v<T>);

  c.write(pos + cista_member_offset(offset::vector<T>, el_),
          convert_endian<Ctx::MODE>(
              start == NULLPTR_OFFSET
                  ? start
                  : start - cista_member_offset(offset::vector<T>, el_) - pos));
  c.write(pos + cista_member_offset(offset::vector<T>, allocated_size_),
          convert_endian<Ctx::MODE>(origin->used_size_));
  c.write(pos + cista_member_offset(offset::vector<T>, used_size_),
          convert_endian<Ctx::MODE>(origin->used_size_));
  c.write(pos + cista_member_offset(offset::vector<T>, self_allocated_), false);

  if (origin->el_ != nullptr) {
    auto i = 0u;
    for (auto it = start; it != start + static_cast<offset_t>(size);
         it += serialized_size<T>()) {
      serialize(c, static_cast<T const*>(origin->el_ + i++), it);
    }
  }
}

template <typename Ctx, typename Ptr>
void serialize(Ctx& c, basic_string<Ptr> const* origin, offset_t const pos) {
  if (origin->is_short()) {
    return;
  }

  auto const start = (origin->h_.ptr_ == nullptr)
                         ? NULLPTR_OFFSET
                         : c.write(origin->data(), origin->size());
  c.write(
      pos + cista_member_offset(offset::string, h_.ptr_),
      convert_endian<Ctx::MODE>(
          start == NULLPTR_OFFSET
              ? start
              : start - cista_member_offset(offset::string, h_.ptr_) - pos));
  c.write(pos + cista_member_offset(offset::string, h_.size_),
          convert_endian<Ctx::MODE>(origin->h_.size_));
  c.write(pos + cista_member_offset(offset::string, h_.self_allocated_), false);
}

template <typename Ctx, typename T, typename Ptr>
void serialize(Ctx& c, basic_unique_ptr<T, Ptr> const* origin,
               offset_t const pos) {
  auto const start =
      origin->el_ == nullptr
          ? NULLPTR_OFFSET
          : c.write(origin->el_, serialized_size<T>(), std::alignment_of_v<T>);

  c.write(
      pos + cista_member_offset(offset::unique_ptr<T>, el_),
      convert_endian<Ctx::MODE>(
          start == NULLPTR_OFFSET
              ? start
              : start - cista_member_offset(offset::unique_ptr<T>, el_) - pos));
  c.write(pos + cista_member_offset(offset::unique_ptr<T>, self_allocated_),
          false);

  if (origin->el_ != nullptr) {
    auto const ptr = static_cast<T const*>(origin->el_);
    c.offsets_[ptr] = start;
    serialize(c, ptr, start);
  }
}

template <typename Ctx, typename T, size_t Size>
void serialize(Ctx& c, array<T, Size> const* origin, offset_t const pos) {
  auto const size =
      static_cast<offset_t>(serialized_size<T>() * origin->size());
  auto i = 0u;
  for (auto it = pos; it != pos + size; it += serialized_size<T>()) {
    serialize(c, origin->el_ + i++, it);
  }
}

constexpr offset_t integrity_start(mode const m) {
  offset_t start = 0;
  if ((m & mode::WITH_VERSION) == mode::WITH_VERSION) {
    start += sizeof(uint64_t);
  }
  return start;
}

constexpr offset_t data_start(mode const m) {
  auto start = integrity_start(m);
  if ((m & mode::WITH_INTEGRITY) == mode::WITH_INTEGRITY) {
    start += sizeof(uint64_t);
  }
  return start;
}

template <mode const Mode = mode::NONE, typename Target, typename T>
void serialize(Target& t, T& value) {
  serialization_context<Target, Mode> c{t};

  if constexpr ((Mode & mode::WITH_VERSION) == mode::WITH_VERSION) {
    auto const h = convert_endian<Mode>(type_hash<decay_t<T>>());
    c.write(&h, sizeof(h));
  }

  auto integrity_offset = offset_t{0};
  if constexpr ((Mode & mode::WITH_INTEGRITY) == mode::WITH_INTEGRITY) {
    auto const h = hash_t{};
    integrity_offset = c.write(&h, sizeof(h));
  }

  serialize(c, &value,
            c.write(&value, serialized_size<T>(),
                    std::alignment_of_v<decay_t<decltype(value)>>));

  for (auto& p : c.pending_) {
    if (auto const it = c.offsets_.find(p.origin_ptr_); it != end(c.offsets_)) {
      c.write(p.pos_, convert_endian<Mode>(it->second - p.pos_));
    } else {
      printf("warning: dangling pointer %p serialized at offset %" PRI_O "\n",
             p.origin_ptr_, p.pos_);
    }
  }

  if constexpr ((Mode & mode::WITH_INTEGRITY) == mode::WITH_INTEGRITY) {
    auto const csum =
        c.checksum(integrity_offset + static_cast<offset_t>(sizeof(hash_t)));
    c.write(integrity_offset, convert_endian<Mode>(csum));
  }
}

template <mode const Mode = mode::NONE, typename T>
byte_buf serialize(T& el) {
  auto b = buf{};
  serialize<Mode>(b, el);
  return std::move(b.buf_);
}

// =============================================================================
// DESERIALIZE
// -----------------------------------------------------------------------------
template <typename Arg, typename... Args>
Arg checked_addition(Arg a1, Args... aN) {
  using Type = decay_t<Arg>;
  auto add_if_ok = [&](auto x) {
    if (a1 > std::numeric_limits<Type>::max() - x) {
      throw std::overflow_error("addition overflow");
    }
    a1 = a1 + x;
  };
  (add_if_ok(aN), ...);
  return a1;
}

template <typename Arg, typename... Args>
Arg checked_multiplication(Arg a1, Args... aN) {
  using Type = decay_t<Arg>;
  auto multiply_if_ok = [&](auto x) {
    if (a1 != 0 && ((std::numeric_limits<Type>::max() / a1) < x)) {
      throw std::overflow_error("addition overflow");
    }
    a1 = a1 * x;
  };
  (multiply_if_ok(aN), ...);
  return a1;
}

template <mode Mode>
struct deserialization_context {
  static constexpr auto const MODE = Mode;

  deserialization_context(uint8_t* from, uint8_t* to)
      : from_{reinterpret_cast<intptr_t>(from)},
        to_{reinterpret_cast<intptr_t>(to)} {}

  template <typename T>
  void convert_endian(T& el) const {
    if (endian_conversion_necessary<MODE>()) {
      el = ::cista::convert_endian<MODE>(el);
    }
  }

  template <typename Ptr>
  void deserialize(Ptr** ptr) const {
    auto const offset =
        reinterpret_cast<offset_t>(::cista::convert_endian<MODE>(*ptr));
    *ptr =
        offset == NULLPTR_OFFSET
            ? nullptr
            : reinterpret_cast<Ptr*>(reinterpret_cast<offset_t>(ptr) + offset);
  }

  template <typename T>
  void check(T* el, size_t size) const {
    if constexpr ((MODE & mode::UNCHECKED) == mode::UNCHECKED) {
      return;
    }

    if (el == nullptr || to_ == 0U) {
      return;
    }

    auto const pos = reinterpret_cast<intptr_t>(el);
    verify(size < static_cast<size_t>(std::numeric_limits<intptr_t>::max()),
           "size out of bounds");
    verify(pos >= from_, "underflow");
    verify(checked_addition(pos, static_cast<intptr_t>(size)) <= to_,
           "overflow");
  }

  void check(bool condition, char const* msg) const {
    if constexpr ((MODE & mode::UNCHECKED) == mode::UNCHECKED) {
      return;
    }

    if (!condition) {
      throw std::runtime_error(msg);
    }
  }

  intptr_t from_, to_;
};

template <typename T, mode const Mode = mode::NONE>
void check(uint8_t const* from, uint8_t const* to) {
  verify(to - from > data_start(Mode), "invalid range");

  if constexpr ((Mode & mode::WITH_VERSION) == mode::WITH_VERSION) {
    verify(convert_endian<Mode>(*reinterpret_cast<hash_t const*>(from)) ==
               type_hash<T>(),
           "invalid version");
  }

  if constexpr ((Mode & mode::WITH_INTEGRITY) == mode::WITH_INTEGRITY) {
    verify(convert_endian<Mode>(*reinterpret_cast<uint64_t const*>(
               from + integrity_start(Mode))) ==
               hash(std::string_view{
                   reinterpret_cast<char const*>(from + data_start(Mode)),
                   static_cast<size_t>(to - from - data_start(Mode))}),
           "invalid checksum");
  }
}

template <typename Ctx, typename T>
void deserialize(Ctx const& c, T* el) {
  using written_type_t = decay_t<T>;
  if constexpr (std::is_union_v<written_type_t>) {
    static_assert(std::is_standard_layout_v<written_type_t> &&
                  std::is_trivially_copyable_v<written_type_t>);
  } else if constexpr (std::is_pointer_v<written_type_t>) {
    c.deserialize(el);
    c.check(*el, sizeof(*std::declval<written_type_t>()));
  } else if constexpr (std::is_scalar_v<written_type_t>) {
    c.check(el, sizeof(T));
    if (std::numeric_limits<written_type_t>::is_integer ||
        std::is_floating_point_v<written_type_t>) {
      c.convert_endian(*el);
    }
  } else {
    for_each_ptr_field(*el, [&](auto& f) { deserialize(c, f); });
  }
}

template <typename Ctx, typename T>
void deserialize(Ctx const& c, offset_ptr<T>* el) {
  using written_type_t = decay_t<T>;
  c.check(el, sizeof(offset_ptr<T>));
  c.convert_endian(el->offset_);
  c.check(el->get(), sizeof(std::declval<written_type_t>()));
}

template <typename Ctx, typename T, typename Ptr, typename TemplateSizeType>
void deserialize(Ctx const& c, basic_vector<T, Ptr, TemplateSizeType>* el) {
  c.check(el, sizeof(basic_vector<T, Ptr, TemplateSizeType>));
  deserialize(c, &el->el_);
  c.convert_endian(el->allocated_size_);
  c.convert_endian(el->used_size_);
  c.check(static_cast<T*>(el->el_),
          checked_multiplication(static_cast<size_t>(el->allocated_size_),
                                 sizeof(T)));
  c.check(el->allocated_size_ == el->used_size_, "vector size mismatch");
  c.check(!el->self_allocated_, "vector self-allocated");
  for (auto& m : *el) {
    deserialize(c, &m);
  }
}

template <typename Ctx, typename Ptr>
void deserialize(Ctx const& c, basic_string<Ptr>* el) {
  c.check(el, sizeof(basic_string<Ptr>));
  if (!el->is_short()) {
    deserialize(c, &el->h_.ptr_);
    c.convert_endian(el->h_.size_);
    c.check(static_cast<char const*>(el->h_.ptr_), el->h_.size_);
    c.check(!el->h_.self_allocated_, "string self-allocated");
  }
}

template <typename Ctx, typename T, typename Ptr>
void deserialize(Ctx const& c, basic_unique_ptr<T, Ptr>* el) {
  c.check(el, sizeof(basic_unique_ptr<T, Ptr>));
  c.check(!el->self_allocated_, "unique_ptr self-allocated");
  deserialize(c, &el->el_);
  if (el->el_ != nullptr) {
    deserialize(c, static_cast<T*>(el->el_));
  }
}

template <typename Ctx, typename T, size_t Size>
void deserialize(Ctx const& c, array<T, Size>* el) {
  c.check(el, sizeof(array<T, Size>));
  for (auto& m : *el) {
    deserialize(c, &m);
  }
}

template <typename T, mode const Mode = mode::NONE>
T* deserialize(uint8_t* from, uint8_t* to = nullptr) {
  check<T, Mode>(from, to);
  deserialization_context<Mode> c{from, to};
  auto const el = reinterpret_cast<T*>(from + data_start(Mode));
  deserialize(c, el);
  return el;
}

template <typename T, mode const Mode = mode::NONE, typename Container>
T* deserialize(Container& c) {
  return deserialize<T, Mode>(&c[0], &c[0] + c.size());
}

template <typename T, mode const Mode = mode::NONE>
T* unchecked_deserialize(uint8_t* from, uint8_t* to = nullptr) {
  return deserialize<T, Mode | mode::UNCHECKED>(from, to);
}

template <typename T, mode const Mode = mode::NONE, typename Container>
T* unchecked_deserialize(Container& c) {
  return unchecked_deserialize<T, Mode>(&c[0], &c[0] + c.size());
}

namespace raw {
using cista::deserialize;
using cista::unchecked_deserialize;
}  // namespace raw

namespace offset {
using cista::deserialize;
using cista::unchecked_deserialize;
}  // namespace offset

}  // namespace cista

#undef cista_member_offset