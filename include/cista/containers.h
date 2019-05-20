#pragma once

#include "cista/containers/array.h"
#include "cista/containers/string.h"
#include "cista/containers/unique_ptr.h"
#include "cista/containers/vector.h"

namespace cista {

enum class ptr_type { RAW, OFFSET };

template <unsigned Size, ptr_type PtrType, class Enable = void>
struct strategy {
  static_assert(PtrType == ptr_type::RAW, "only sizes 1, 2, 4, and 8 allowed");
  static_assert(PtrType != ptr_type::RAW, "only native pointer size allowed");
};

template <unsigned Size, ptr_type PtrType>
struct strategy<Size, PtrType,
                typename std::enable_if_t<PtrType == ptr_type::RAW &&
                                          Size == sizeof(void*)>> {
  template <typename T>
  using ptr = T*;
};

template <unsigned Size, ptr_type PtrType>
struct strategy<Size, PtrType,
                typename std::enable_if_t<PtrType == ptr_type::OFFSET &&
                                          Size == sizeof(int64_t)>> {
  template <typename T>
  using ptr = offset_ptr<T, int64_t>;
};

template <unsigned Size, ptr_type PtrType>
struct strategy<Size, PtrType,
                typename std::enable_if_t<PtrType == ptr_type::OFFSET &&
                                          Size == sizeof(int32_t)>> {
  template <typename T>
  using ptr = offset_ptr<T, int32_t>;
};

template <unsigned Size, ptr_type PtrType>
struct strategy<Size, PtrType,
                typename std::enable_if_t<PtrType == ptr_type::OFFSET &&
                                          Size == sizeof(int16_t)>> {
  template <typename T>
  using ptr = offset_ptr<T, int16_t>;
};

template <unsigned Size, ptr_type PtrType>
struct strategy<Size, PtrType,
                typename std::enable_if_t<PtrType == ptr_type::OFFSET &&
                                          Size == sizeof(int8_t)>> {
  template <typename T>
  using ptr = offset_ptr<T, int8_t>;
};

template <unsigned Size, ptr_type PtrType>
struct data : public strategy<Size, PtrType> {
  template <typename T>
  using ptr = typename strategy<Size, PtrType>::template ptr<T>;

  template <typename T, size_t ArraySize>
  using array = cista::array<T, ArraySize>;

  template <typename T>
  using unique_ptr = cista::basic_unique_ptr<T, ptr<T>>;

  template <typename T>
  using vector = cista::basic_vector<T, ptr<T>>;

  using string = cista::basic_string<ptr<char const>>;

  template <typename T, typename... Args>
  static inline cista::basic_unique_ptr<T, ptr<T>> make_unique(Args&&... args) {
    return cista::basic_unique_ptr<T, ptr<T>>{
        new T{std::forward<Args>(args)...}, true};
  }
};

// =============================================================================
// Offset based data structures:
// [+] can be read without any deserialization step
// [+] suitable for shared memory applications
// [-] slower at runtime (pointers needs to be resolved using on more add)
// -----------------------------------------------------------------------------
using offset = data<sizeof(void*), ptr_type::OFFSET>;

// =============================================================================
// Raw data structures:
// [-] deserialize step takes time (but still very fast also for GBs of data)
// [-] the buffer containing the serialized data needs to be modified
// [+] fast runtime access (raw access)
// -----------------------------------------------------------------------------
using raw = data<sizeof(void*), ptr_type::RAW>;

}  // namespace cista