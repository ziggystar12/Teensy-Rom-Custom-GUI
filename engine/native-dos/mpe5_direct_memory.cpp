#include "mpe5_direct_memory.h"

#include <string.h>

namespace mpe5 {

MPE5_CODE bool DirectMemory::start(void *conventional,
                                   size_t conventionalBytes,
                                   void *high, size_t highStorageBytes,
                                   size_t highStride,
                                   void *ports, size_t portBytes) {
  conventional_ = high_ = ports_ = nullptr;
  highStride_ = 0;
  if (!conventional || conventionalBytes != ConventionalBytes ||
      !high || highStride < HighChunkBytes ||
      highStorageBytes < (HighChunks - 1u) * highStride + HighChunkBytes ||
      !ports || portBytes < PortBytes)
    return false;
  conventional_ = static_cast<uint8_t *>(conventional);
  high_ = static_cast<uint8_t *>(high);
  ports_ = static_cast<uint8_t *>(ports);
  highStride_ = highStride;
  return true;
}

MPE5_CODE bool DirectMemory::reset() {
  if (!conventional_ || !high_ || !ports_) return false;
  memset(conventional_, 0, ConventionalBytes);
  for (uint32_t chunk = 0; chunk < HighChunks; ++chunk)
    memset(high_ + chunk * highStride_, 0, HighChunkBytes);
  memset(ports_, 0, PortBytes);
  return true;
}

MPE5_CODE void DirectMemory::readHigh(uint32_t offset, uint8_t *out,
                                      uint32_t length) const {
  while (length) {
    const uint32_t within = offset % HighChunkBytes;
    uint32_t take = HighChunkBytes - within;
    if (take > length) take = length;
    memcpy(out, high_ + (offset / HighChunkBytes) * highStride_ + within, take);
    offset += take;
    out += take;
    length -= take;
  }
}

MPE5_CODE void DirectMemory::writeHigh(uint32_t offset, const uint8_t *data,
                                       uint32_t length) {
  while (length) {
    const uint32_t within = offset % HighChunkBytes;
    uint32_t take = HighChunkBytes - within;
    if (take > length) take = length;
    memcpy(high_ + (offset / HighChunkBytes) * highStride_ + within, data, take);
    offset += take;
    data += take;
    length -= take;
  }
}

MPE5_CODE bool DirectMemory::validSpan(uint32_t address,
                                       uint32_t length) const {
  return conventional_ && high_ && ports_ && address <= NativeBackingBytes &&
         length <= NativeBackingBytes - address;
}

MPE5_CODE bool DirectMemory::read(uint32_t address, uint8_t *out,
                                  uint32_t length) {
  if (!validSpan(address, length) || (length && !out)) return false;
  while (length) {
    uint32_t take;
    if (address < ConventionalBytes) {
      take = ConventionalBytes - address;
      if (take > length) take = length;
      memcpy(out, conventional_ + address, take);
    } else if (address < HighBase) {
      take = HighBase - address;
      if (take > length) take = length;
      memset(out, 0, take);
    } else if (address < HighBase + HighBytes) {
      take = HighBase + HighBytes - address;
      if (take > length) take = length;
      readHigh(address - HighBase, out, take);
    } else if (address < 0xf0000u) {
      take = 0xf0000u - address;
      if (take > length) take = length;
      memset(out, 0, take);
    } else if (address < 0x100000u) {
      // F000 is permanently pinned by the CPU adapter. Reaching this callback
      // means the split in readBytes/writeBytes regressed.
      return false;
    } else if (address < AddressMapBytes) {
      take = AddressMapBytes - address;
      if (take > length) take = length;
      memcpy(out, conventional_ + address - 0x100000u, take);
    } else if (address < PortBase + PortBytes) {
      take = PortBase + PortBytes - address;
      if (take > length) take = length;
      memcpy(out, ports_ + address - PortBase, take);
    } else {
      // Native console storage is supplied as direct pointers and must never
      // be folded back into the guest memory callback.
      return false;
    }
    address += take;
    out += take;
    length -= take;
  }
  return true;
}

MPE5_CODE bool DirectMemory::write(uint32_t address, const uint8_t *data,
                                   uint32_t length) {
  if (!validSpan(address, length) || (length && !data)) return false;
  while (length) {
    uint32_t take;
    if (address < ConventionalBytes) {
      take = ConventionalBytes - address;
      if (take > length) take = length;
      memcpy(conventional_ + address, data, take);
    } else if (address < HighBase) {
      take = HighBase - address;
      if (take > length) take = length;
    } else if (address < HighBase + HighBytes) {
      take = HighBase + HighBytes - address;
      if (take > length) take = length;
      writeHigh(address - HighBase, data, take);
    } else if (address < 0xf0000u) {
      take = 0xf0000u - address;
      if (take > length) take = length;
    } else if (address < 0x100000u) {
      return false;
    } else if (address < AddressMapBytes) {
      take = AddressMapBytes - address;
      if (take > length) take = length;
      memcpy(conventional_ + address - 0x100000u, data, take);
    } else if (address < PortBase + PortBytes) {
      take = PortBase + PortBytes - address;
      if (take > length) take = length;
      memcpy(ports_ + address - PortBase, data, take);
    } else {
      return false;
    }
    address += take;
    data += take;
    length -= take;
  }
  return true;
}

}  // namespace mpe5
