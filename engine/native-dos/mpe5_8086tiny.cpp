#include "mpe5_8086tiny.h"

#include <string.h>
#include <type_traits>

// The inherited CPU casts alias register bytes and low words of scalar
// scratch values. Keep GCC from assuming those differently typed accesses
// are independent. Scope this to the core, including its inline proxies;
// restoring the options below leaves the surrounding firmware unchanged.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("O3,no-strict-aliasing")
#endif

namespace mpe5_detail {

MPE5_HOT_CODE uint32_t readBits(uint32_t address, uint8_t width);
MPE5_HOT_CODE void writeBits(uint32_t address, uint8_t width, uint32_t value);
MPE5_CODE bool readBytes(uint32_t address, uint8_t *out, uint32_t length);
MPE5_CODE bool writeBytes(uint32_t address, const uint8_t *data, uint32_t length);
MPE5_CODE bool zeroBytes(uint32_t address, uint32_t length);
MPE5_CODE void recordFailure(mpe5::CoreStop reason, uint32_t address);
MPE5_CODE void observeWrite(uint32_t address, const uint8_t *data, uint32_t length);
MPE5_CODE bool writeRtc(uint32_t address);

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

MPE5_CODE void recordFailure(mpe5::CoreStop reason, uint32_t address) {
  if (MPE5Diagnostic.reason != mpe5::CoreStop::None) return;
  MPE5Diagnostic.address = address;
  MPE5Diagnostic.cs = regs16 ? regs16[REG_CS] : 0;
  MPE5Diagnostic.ip = reg_ip;
  MPE5Diagnostic.reason = reason;
  MPE5Diagnostic.opcode = MPE5OpcodeBytes[0];
}

MPE5_CODE void observeWrite(uint32_t address, const uint8_t *data, uint32_t length) {
  if (!length) return;
  const auto contains = [address, length](uint32_t target) {
    return address <= target && length > target - address;
  };
  if (contains(0x449u)) {
    MPE5Video.mode = data[0x449u - address];
    // The bundled 8086tiny BIOS uses Hercules-compatible mode registers for
    // CGA and omits3D9 initialization. Match its existing bright default CGA
    // palette, while subsequent real CGA port writes remain authoritative.
    MPE5Video.control = MPE5Video.mode == 6 ? 0x1a : MPE5Video.mode == 5 ? 0x0e : 0x0a;
    MPE5Video.colorSelect = MPE5Video.mode == 6 ? 15 : 0x30;
    MPE5Video.enabled = true;
    MPE5Video.startAddress = 0;
  }
  if (contains(0x4adu)) MPE5Video.startAddress = uint16_t((MPE5Video.startAddress & 0xff00u) | data[0x4adu - address]);
  if (contains(0x4aeu)) MPE5Video.startAddress = uint16_t((MPE5Video.startAddress & 0x00ffu) | uint16_t(data[0x4aeu - address]) << 8);
  constexpr uint32_t ports = mpe5::AddressMapBytes;
  if (contains(ports + 0x3b8u)) MPE5Video.enabled = (data[ports + 0x3b8u - address] & 8u) != 0;
  if (contains(ports + 0x3d8u)) {
    const uint8_t value = data[ports + 0x3d8u - address];
    MPE5Video.control = value;
    MPE5Video.enabled = (value & 8u) != 0;
    if (value & 2u) MPE5Video.mode = value & 16u ? 6 : value & 4u ? 5 : 4;
    else if (MPE5Video.mode >= 4) MPE5Video.mode = value & 1u ? 3 : 1;
  }
  if (contains(ports + 0x3d9u)) MPE5Video.colorSelect = data[ports + 0x3d9u - address];
  if (contains(ports + 0x3d4u)) MPE5VideoCrtcIndex = data[ports + 0x3d4u - address];
  if (contains(ports + 0x3d5u)) {
    const uint8_t value = data[ports + 0x3d5u - address];
    if (MPE5VideoCrtcIndex == 12) MPE5Video.startAddress = uint16_t((MPE5Video.startAddress & 255u) | uint16_t(value) << 8);
    else if (MPE5VideoCrtcIndex == 13) MPE5Video.startAddress = uint16_t((MPE5Video.startAddress & 0xff00u) | value);
  }
  constexpr uint32_t begin = 0xb8000u, end = begin + mpe5::CgaVideo::VramBytes;
  if (MPE5Host.video.write && address < end && address + length > begin) {
    const uint32_t first = address > begin ? address : begin;
    const uint32_t last = address + length < end ? address + length : end;
    MPE5Host.video.write(MPE5Host.video.context, uint16_t(first - begin),
                        data + first - address, uint16_t(last - first));
  }
}

MPE5_CODE bool writeRtc(uint32_t address) {
  // 8086tiny's BIOS consumes nine little-endian32-bit struct-tm fields, then
  // a16-bit millisecond-of-second at+36. A frozen millisecond field prevents
  // BIOS INT0A from issuing INT8/INT1C and hangs games in countdown loops.
  MPE5ClockInstructions += uint32_t(inst_counter - MPE5ClockLastInstruction);
  MPE5ClockLastInstruction = inst_counter;
  const uint32_t elapsed = MPE5Host.milliseconds ?
      uint32_t(MPE5Host.milliseconds() - MPE5ClockStart) : uint32_t(MPE5ClockInstructions / 1000u);
  const uint32_t seconds = elapsed / 1000u, days = seconds / 86400u;
  // A32-bit elapsed-millisecond interval is at most49days. Use a valid
  // deterministic DOS-era date, including the January/February transition.
  const uint32_t fields[9] = {seconds % 60u, (seconds / 60u) % 60u,
      (seconds / 3600u) % 24u, (days < 31u ? days : days - 31u) + 1u,
      days < 31u ? 0u : 1u, 80u, (days + 2u) % 7u, days, 0u};
  uint8_t bytes[38]{};
  for (uint8_t field = 0; field < 9; ++field)
    for (uint8_t byte = 0; byte < 4; ++byte)
      bytes[field * 4u + byte] = uint8_t(fields[field] >> (byte * 8u));
  bytes[36] = uint8_t(elapsed % 1000u);
  bytes[37] = uint8_t((elapsed % 1000u) >> 8);
  return writeBytes(address, bytes, sizeof(bytes));
}

MPE5_CODE bool readBytes(uint32_t address, uint8_t *out, uint32_t length) {
  if (MPE5MemoryFailed) return false;
  if ((!out && length) || address > mpe5::NativeBackingBytes ||
      length > mpe5::NativeBackingBytes - address) {
    recordFailure(mpe5::CoreStop::InvalidRead, address);
    MPE5MemoryFailed = true; return false;
  }
  if (!MPE5Host.memory.read) {
    if (length) memcpy(out, MPE5Host.addressMap + address, length);
    return true;
  }
  while (length) {
    uint32_t chunk = length;
    if (MPE5Host.conventionalRam &&
        address < MPE5Host.conventionalRamBytes) {
      if (chunk > MPE5Host.conventionalRamBytes - address)
        chunk = MPE5Host.conventionalRamBytes - address;
      memcpy(out, MPE5Host.conventionalRam + address, chunk);
    } else if (address >= 0xf0000u && address < 0x100000u) {
      if (chunk > 0x100000u - address) chunk = 0x100000u - address;
      memcpy(out, MPE5Host.fixedF000 + address - 0xf0000u, chunk);
    } else {
      if (address < 0xf0000u && chunk > 0xf0000u - address) chunk = 0xf0000u - address;
      if (!MPE5Host.memory.read(MPE5Host.memory.context, address, out, chunk)) {
        recordFailure(mpe5::CoreStop::ReadFailure, address);
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
    recordFailure(mpe5::CoreStop::InvalidWrite, address);
    MPE5MemoryFailed = true; return false;
  }
  if (!MPE5Host.memory.write) {
    if (length) memcpy(MPE5Host.addressMap + address, data, length);
    observeWrite(address, data, length);
    return true;
  }
  while (length) {
    uint32_t chunk = length;
    if (MPE5Host.conventionalRam &&
        address < MPE5Host.conventionalRamBytes) {
      if (chunk > MPE5Host.conventionalRamBytes - address)
        chunk = MPE5Host.conventionalRamBytes - address;
      memcpy(MPE5Host.conventionalRam + address, data, chunk);
    } else if (address >= 0xf0000u && address < 0x100000u) {
      if (chunk > 0x100000u - address) chunk = 0x100000u - address;
      memcpy(MPE5Host.fixedF000 + address - 0xf0000u, data, chunk);
    } else {
      if (address < 0xf0000u && chunk > 0xf0000u - address) chunk = 0xf0000u - address;
      if (!MPE5Host.memory.write(MPE5Host.memory.context, address, data, chunk)) {
        recordFailure(mpe5::CoreStop::WriteFailure, address);
        MPE5MemoryFailed = true; return false;
      }
    }
    observeWrite(address, data, chunk);
    address += chunk; data += chunk; length -= chunk;
  }
  return true;
}

MPE5_HOT_CODE uint32_t readBits(uint32_t address, uint8_t width) {
  // Almost every operand fetched by DOS and Boulder lives in the contiguous
  // RAM2 conventional-memory span (or the permanently pinned F000 segment).
  // Avoid routing those byte/word accesses through the generic span callback;
  // boundary crossings and all other regions still use the checked path.
  if (MPE5MemoryFailed) return 0;
  const uint8_t *direct = nullptr;
  if (address <= mpe5::NativeBackingBytes &&
      width <= mpe5::NativeBackingBytes - address) {
    if (MPE5Host.conventionalRam &&
        address <= MPE5Host.conventionalRamBytes &&
        width <= MPE5Host.conventionalRamBytes - address) {
      direct = MPE5Host.conventionalRam + address;
    } else if (MPE5Host.fixedF000 && address >= 0xf0000u &&
               address <= 0x100000u && width <= 0x100000u - address) {
      direct = MPE5Host.fixedF000 + address - 0xf0000u;
    }
  }
  if (direct) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < width; ++index)
      value |= uint32_t(direct[index]) << (8u * index);
    return value;
  }
  uint8_t bytes[4]{};
  if (!readBytes(address, bytes, width)) return 0;
  uint32_t value = 0;
  for (uint8_t index = 0; index < width; ++index) value |= uint32_t(bytes[index]) << (8u * index);
  return value;
}

MPE5_HOT_CODE void writeBits(uint32_t address, uint8_t width, uint32_t value) {
  uint8_t bytes[4];
  for (uint8_t index = 0; index < width; ++index) bytes[index] = uint8_t(value >> (8u * index));
  if (MPE5MemoryFailed) return;
  uint8_t *direct = nullptr;
  if (address <= mpe5::NativeBackingBytes &&
      width <= mpe5::NativeBackingBytes - address) {
    if (MPE5Host.conventionalRam &&
        address <= MPE5Host.conventionalRamBytes &&
        width <= MPE5Host.conventionalRamBytes - address) {
      direct = MPE5Host.conventionalRam + address;
    } else if (MPE5Host.fixedF000 && address >= 0xf0000u &&
               address <= 0x100000u && width <= 0x100000u - address) {
      direct = MPE5Host.fixedF000 + address - 0xf0000u;
    }
  }
  if (direct) {
    for (uint8_t index = 0; index < width; ++index) direct[index] = bytes[index];
    observeWrite(address, bytes, width);
    return;
  }
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

MPE5_CODE bool patchBiosConventionalMemory(uint8_t *bios, uint32_t bytes) {
  static_assert(ConventionalRamBytes % 1024u == 0u &&
                ConventionalRamBytes / 1024u <= 0xffffu,
                "INT12 result must fit in AX");
  if (!bios || bytes < 4u) return false;
  const uint16_t sourceKiB = 640u;
  const uint16_t targetKiB = uint16_t(ConventionalRamBytes / 1024u);
  uint8_t *match = nullptr;
  for (uint32_t offset = 0; offset + 4u <= bytes; ++offset) {
    if (bios[offset] != 0xb8u || bios[offset + 3u] != 0xcfu) continue;
    const uint16_t immediate = uint16_t(bios[offset + 1u] |
        uint16_t(bios[offset + 2u]) << 8u);
    if (immediate != sourceKiB && immediate != targetKiB) continue;
    if (match) return false;
    match = bios + offset + 1u;
  }
  if (!match) return false;
  match[0] = uint8_t(targetKiB);
  match[1] = uint8_t(targetKiB >> 8u);
  return true;
}

MPE5_CODE bool coreStart(const CoreHost &host) {
  if (!MPE5VendorStart(host)) return false;
  // Verify the RAM reset before claiming it is ready. This is a complete
  // zero readback of conventional memory, not a simulated POST counter or
  // a destructive hardware stress test. No guest instructions have run yet.
  uint8_t checked[256];
  for (uint32_t address = 0; address < ConventionalRamBytes; address += sizeof checked) {
    if (!mpe5_detail::readBytes(address, checked, sizeof checked)) {
      MPE5Ready = false; return false;
    }
    for (uint16_t i = 0; i < sizeof checked; ++i) if (checked[i]) {
      mpe5_detail::recordFailure(CoreStop::ReadFailure, address + i);
      MPE5Ready = false; return false;
    }
  }
  static_assert(ConventionalRamBytes == 512u * 1024u, "Update the BIOS RAM label when memory changes");
  const char *banner = "Mean Hamster BIOS (C) 2026\r\n512K OK\r\nBooting drive C:\r\n";
  while (*banner) MPE5VendorPutChar(uint8_t(*banner++));
  return true;
}

MPE5_CODE bool coreRun(uint32_t instructionBudget) {
  return MPE5VendorRun(instructionBudget);
}

MPE5_CODE void coreReset() { MPE5VendorReset(); }
MPE5_CODE CoreDiagnostic coreDiagnostic() { return MPE5Diagnostic; }
MPE5_CODE void coreSetVideoObserver(const VideoObserver &observer) { MPE5Host.video = observer; }
MPE5_CODE VideoState coreVideoState() { return MPE5Video; }

static MPE5_CODE bool redirectorRead(void *, uint32_t address, uint8_t *out, uint32_t length) {
  return mpe5_detail::readBytes(address, out, length);
}
static MPE5_CODE bool redirectorWrite(void *, uint32_t address, const uint8_t *data, uint32_t length) {
  return mpe5_detail::writeBytes(address, data, length);
}
MPE5_CODE RedirectorMemory coreRedirectorMemory() {
  return {nullptr, redirectorRead, redirectorWrite};
}

}  // namespace mpe5

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
