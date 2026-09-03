#include "mpe5_8086tiny.h"

#include <string.h>
#include <type_traits>

namespace mpe5_detail {

MPE5_CODE uint32_t readBits(uint32_t address, uint8_t width);
MPE5_CODE void writeBits(uint32_t address, uint8_t width, uint32_t value);
MPE5_CODE bool readBytes(uint32_t address, uint8_t *out, uint32_t length);
MPE5_CODE bool writeBytes(uint32_t address, const uint8_t *data, uint32_t length);
MPE5_CODE bool zeroBytes(uint32_t address, uint32_t length);

// Templates must not emit weak/COMDAT bodies in the shared FLASHMEM section.
// These wrappers only carry an address; the non-template memory I/O below
// retains FLASHMEM while the wrappers always fold into their caller.
#if defined(__GNUC__)
#define MPE5_PROXY_INLINE inline __attribute__((always_inline))
#else
#define MPE5_PROXY_INLINE inline
#endif

// Only the logical address survives another memory operation. In particular,
// the source operand may fault/evict pages before the destination is stored.
template <typename T> class MemoryRef {
 public:
  MPE5_PROXY_INLINE explicit MemoryRef(uint32_t address) : location(address) {}
  MemoryRef(const MemoryRef &) = default;
  MPE5_PROXY_INLINE uint32_t address() const { return location; }
  MPE5_PROXY_INLINE operator T() const { return static_cast<T>(readBits(location, sizeof(T))); }
  MPE5_PROXY_INLINE MemoryRef &operator=(T value) {
    writeBits(location, sizeof(T), static_cast<uint32_t>(value)); return *this;
  }
  MPE5_PROXY_INLINE MemoryRef &operator=(const MemoryRef &value) { return *this = T(value); }
  template <typename U> MPE5_PROXY_INLINE MemoryRef &operator=(const MemoryRef<U> &value) {
    return *this = static_cast<T>(static_cast<U>(value));
  }
  MPE5_PROXY_INLINE MemoryRef &operator+=(uint32_t value) { return *this = T(T(*this) + value); }
  MPE5_PROXY_INLINE MemoryRef &operator-=(uint32_t value) { return *this = T(T(*this) - value); }
  MPE5_PROXY_INLINE MemoryRef &operator^=(uint32_t value) { return *this = T(T(*this) ^ value); }
  MPE5_PROXY_INLINE MemoryRef &operator&=(uint32_t value) { return *this = T(T(*this) & value); }
  MPE5_PROXY_INLINE MemoryRef &operator|=(uint32_t value) { return *this = T(T(*this) | value); }
  MPE5_PROXY_INLINE MemoryRef &operator<<=(uint32_t value) { return *this = T(T(*this) << value); }
  MPE5_PROXY_INLINE MemoryRef &operator>>=(uint32_t value) { return *this = T(T(*this) >> value); }
  MPE5_PROXY_INLINE MemoryRef &operator++() { return *this += 1; }
  MPE5_PROXY_INLINE MemoryRef &operator--() { return *this -= 1; }
  MPE5_PROXY_INLINE T operator++(int) { const T old = *this; ++*this; return old; }
  MPE5_PROXY_INLINE T operator--(int) { const T old = *this; --*this; return old; }
 private:
  uint32_t location;
};

template <typename T> struct IsMemoryRef : std::false_type {};
template <typename T> struct IsMemoryRef<MemoryRef<T>> : std::true_type {};
template <typename T> struct Caster {};
template <typename T, typename U>
MPE5_PROXY_INLINE MemoryRef<T> operator*(Caster<T>, MemoryRef<U> source) {
  return MemoryRef<T>(source.address());
}
// Preserve 8086tiny's low-byte/word aliases for its permanent registers and
// scalar scratch values. Guest accesses use the overload above instead.
template <typename T, typename U>
MPE5_PROXY_INLINE typename std::enable_if<!IsMemoryRef<U>::value, T &>::type
operator*(Caster<T>, U &source) { return *reinterpret_cast<T *>(&source); }

#undef MPE5_PROXY_INLINE

}  // namespace mpe5_detail

// The upstream source is compiled in this translation unit after its host
// main loop has been replaced with the bounded MPE5 adapter hooks.
#ifndef DMAMEM
#define DMAMEM
#endif
#define MPE5_NATIVE 1
#define NO_GRAPHICS 1
#include "vendor/8086tiny/8086tiny.c"

namespace mpe5_detail {

MPE5_CODE bool readBytes(uint32_t address, uint8_t *out, uint32_t length) {
  if (MPE5MemoryFailed) return false;
  if ((!out && length) || address > mpe5::NativeBackingBytes ||
      length > mpe5::NativeBackingBytes - address) {
    MPE5MemoryFailed = true; return false;
  }
  if (!MPE5Host.memory.read) {
    if (length) memcpy(out, MPE5Host.addressMap + address, length);
    return true;
  }
  while (length) {
    uint32_t chunk = length;
    if (address >= 0xf0000u && address < 0x100000u) {
      if (chunk > 0x100000u - address) chunk = 0x100000u - address;
      memcpy(out, MPE5Host.fixedF000 + address - 0xf0000u, chunk);
    } else {
      if (address < 0xf0000u && chunk > 0xf0000u - address) chunk = 0xf0000u - address;
      if (!MPE5Host.memory.read(MPE5Host.memory.context, address, out, chunk)) {
        MPE5MemoryFailed = true; return false;
      }
    }
    address += chunk; out += chunk; length -= chunk;
  }
  return true;
}

MPE5_CODE bool writeBytes(uint32_t address, const uint8_t *data, uint32_t length) {
  if (MPE5MemoryFailed) return false;
  if ((!data && length) || address > mpe5::NativeBackingBytes ||
      length > mpe5::NativeBackingBytes - address) {
    MPE5MemoryFailed = true; return false;
  }
  if (!MPE5Host.memory.write) {
    if (length) memcpy(MPE5Host.addressMap + address, data, length);
    return true;
  }
  while (length) {
    uint32_t chunk = length;
    if (address >= 0xf0000u && address < 0x100000u) {
      if (chunk > 0x100000u - address) chunk = 0x100000u - address;
      memcpy(MPE5Host.fixedF000 + address - 0xf0000u, data, chunk);
    } else {
      if (address < 0xf0000u && chunk > 0xf0000u - address) chunk = 0xf0000u - address;
      if (!MPE5Host.memory.write(MPE5Host.memory.context, address, data, chunk)) {
        MPE5MemoryFailed = true; return false;
      }
    }
    address += chunk; data += chunk; length -= chunk;
  }
  return true;
}

MPE5_CODE uint32_t readBits(uint32_t address, uint8_t width) {
  uint8_t bytes[4]{};
  if (!readBytes(address, bytes, width)) return 0;
  uint32_t value = 0;
  for (uint8_t index = 0; index < width; ++index) value |= uint32_t(bytes[index]) << (8u * index);
  return value;
}

MPE5_CODE void writeBits(uint32_t address, uint8_t width, uint32_t value) {
  uint8_t bytes[4];
  for (uint8_t index = 0; index < width; ++index) bytes[index] = uint8_t(value >> (8u * index));
  writeBytes(address, bytes, width);
}

MPE5_CODE bool zeroBytes(uint32_t address, uint32_t length) {
  const uint8_t zeros[64]{};
  while (length) {
    const uint32_t chunk = length < sizeof(zeros) ? length : sizeof(zeros);
    if (!writeBytes(address, zeros, chunk)) return false;
    address += chunk; length -= chunk;
  }
  return true;
}

}  // namespace mpe5_detail

namespace mpe5 {

MPE5_CODE bool coreStart(const CoreHost &host) { return MPE5VendorStart(host); }

MPE5_CODE bool coreRun(uint32_t instructionBudget) {
  return MPE5VendorRun(instructionBudget);
}

MPE5_CODE void coreReset() { MPE5VendorReset(); }

}  // namespace mpe5
