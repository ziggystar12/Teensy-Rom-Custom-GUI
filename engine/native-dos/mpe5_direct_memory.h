#ifndef MPE5_DIRECT_MEMORY_H
#define MPE5_DIRECT_MEMORY_H

#include "mpe5_platform.h"

namespace mpe5 {

// Reset-only PC memory map for the Teensy 4.1. Conventional memory owns all
// 512 KiB of RAM2. The PC video/ROM aperture and I/O-port latches live in
// caller-owned RAM1 because no firmware object may remain in RAM2 after the
// handoff.
class DirectMemory {
 public:
  static constexpr uint32_t ConventionalBytes = 512u * 1024u;
  static constexpr uint32_t HighBase = 0xb0000u;
  static constexpr uint32_t HighBytes = 0x20000u;
  static constexpr uint32_t HighChunkBytes = 0x2000u;
  static constexpr uint32_t HighChunks = HighBytes / HighChunkBytes;
  static constexpr uint32_t PortBase = AddressMapBytes;
  static constexpr uint32_t PortBytes = NativeIoPortBytes;

  MPE5_CODE bool start(void *conventional, size_t conventionalBytes,
                       void *high, size_t highStorageBytes,
                       size_t highStride,
                       void *ports, size_t portBytes);
  MPE5_CODE bool reset();
  MPE5_CODE bool read(uint32_t address, uint8_t *out, uint32_t length);
  MPE5_CODE bool write(uint32_t address, const uint8_t *data, uint32_t length);

  uint8_t *conventional() const { return conventional_; }

 private:
  uint8_t *conventional_ = nullptr;
  uint8_t *high_ = nullptr;
  uint8_t *ports_ = nullptr;
  size_t highStride_ = 0;

  MPE5_CODE bool validSpan(uint32_t address, uint32_t length) const;
  MPE5_CODE void readHigh(uint32_t offset, uint8_t *out,
                          uint32_t length) const;
  MPE5_CODE void writeHigh(uint32_t offset, const uint8_t *data,
                           uint32_t length);
};

static_assert(ConventionalRamBytes == DirectMemory::ConventionalBytes,
              "DOS conventional-memory declaration must match RAM2");

}  // namespace mpe5

#endif
