#include "mpe5_paged_memory.h"
#include <string.h>

namespace mpe5 {

MPE5_CODE bool PagedMemory::start(void *workspace, size_t bytes, const PageStore &store) {
  memory_ = nullptr;
  store_ = store;
  failed_ = false;
  counts_ = {};
  hand_ = 0;
  if (!workspace || bytes < WorkspaceBytes || !store.readPage || !store.writePage)
    return false;
  memory_ = static_cast<uint8_t *>(workspace);
  return reset();
}

MPE5_CODE bool PagedMemory::reset() {
  if (!memory_) return false;
  // Metadata is byte-addressed, so the caller's workspace need not be
  // aligned for C++ objects. Cache contents are zeroed only when acquired.
  memset(memory_ + LookupOffset, 0xff, PageCount * 2u + ResidentFrames * 2u);
  memset(memory_ + FlagsOffset, 0, ResidentFrames + PresenceBytes);
  hand_ = 0;
  failed_ = false;
  counts_ = {};
  return true;
}

MPE5_CODE bool PagedMemory::read(uint32_t address, uint8_t *out, uint32_t length) {
  if (!validSpan(address, length) || (length && !out)) return false;
  while (length) {
    const uint16_t frame = acquire(address / PageBytes);
    if (frame == Invalid) return false;
    const uint16_t offset = address % PageBytes;
    uint32_t take = PageBytes - offset;
    if (take > length) take = length;
    memcpy(out, memory_ + size_t(frame) * PageBytes + offset, take);
    address += take; out += take; length -= take;
  }
  return true;
}

MPE5_CODE bool PagedMemory::write(uint32_t address, const uint8_t *data, uint32_t length) {
  if (!validSpan(address, length) || (length && !data)) return false;
  while (length) {
    const uint16_t frame = acquire(address / PageBytes);
    if (frame == Invalid) return false;
    const uint16_t offset = address % PageBytes;
    uint32_t take = PageBytes - offset;
    if (take > length) take = length;
    memcpy(memory_ + size_t(frame) * PageBytes + offset, data, take);
    memory_[FlagsOffset + frame] |= Dirty;
    address += take; data += take; length -= take;
  }
  return true;
}

MPE5_CODE bool PagedMemory::failed() const { return failed_; }
MPE5_CODE PagedMemory::Stats PagedMemory::stats() const { return counts_; }

MPE5_CODE bool PagedMemory::validSpan(uint32_t address, uint32_t length) const {
  return memory_ && !failed_ && address <= VirtualBytes && length <= VirtualBytes - address;
}

MPE5_CODE uint16_t PagedMemory::get16(size_t offset) const {
  return uint16_t(memory_[offset]) | (uint16_t(memory_[offset + 1]) << 8);
}

MPE5_CODE void PagedMemory::put16(size_t offset, uint16_t value) {
  memory_[offset] = uint8_t(value);
  memory_[offset + 1] = uint8_t(value >> 8);
}

MPE5_CODE bool PagedMemory::present(uint32_t page) const {
  return (memory_[PresenceOffset + page / 8u] & uint8_t(1u << (page % 8u))) != 0;
}

MPE5_CODE uint16_t PagedMemory::ioFailure() {
  failed_ = true;
  ++counts_.ioFailures;
  return Invalid;
}

MPE5_CODE uint16_t PagedMemory::acquire(uint32_t page) {
  uint16_t frame = get16(LookupOffset + page * 2u);
  if (frame != Invalid) {
    ++counts_.hits;
    memory_[FlagsOffset + frame] |= Referenced;
    return frame;
  }
  ++counts_.misses;
  // Second-chance clock: direct lookup serves hits; a full cache scans at
  // most twice around its bounded frame array to choose an eviction.
  for (;;) {
    frame = hand_;
    hand_ = uint16_t((hand_ + 1u) % ResidentFrames);
    const uint16_t previous = get16(TagsOffset + frame * 2u);
    if (previous == Invalid) break;
    uint8_t &flags = memory_[FlagsOffset + frame];
    if (flags & Referenced) { flags &= uint8_t(~Referenced); continue; }
    if (flags & Dirty) {
      if (!store_.writePage(store_.context, previous, memory_ + size_t(frame) * PageBytes))
        return ioFailure();
      ++counts_.pageWrites;
      memory_[PresenceOffset + previous / 8u] |= uint8_t(1u << (previous % 8u));
    }
    put16(LookupOffset + previous * 2u, Invalid);
    ++counts_.evictions;
    break;
  }

  put16(TagsOffset + frame * 2u, Invalid);
  memory_[FlagsOffset + frame] = 0;
  uint8_t *data = memory_ + size_t(frame) * PageBytes;
  if (present(page)) {
    if (!store_.readPage(store_.context, page, data)) return ioFailure();
    ++counts_.pageReads;
  } else {
    memset(data, 0, PageBytes);
    ++counts_.zeroPages;
  }
  put16(TagsOffset + frame * 2u, uint16_t(page));
  put16(LookupOffset + page * 2u, frame);
  memory_[FlagsOffset + frame] = Referenced;
  return frame;
}

}  // namespace mpe5
