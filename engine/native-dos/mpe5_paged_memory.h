#ifndef MPE5_PAGED_MEMORY_H
#define MPE5_PAGED_MEMORY_H

#include "mpe5_platform.h"

namespace mpe5 {

struct PageStore {
  static constexpr uint16_t PageBytes = 512;
  void *context = nullptr;
  // Each successful callback transfers exactly one complete page. The store
  // must contain PageCount pages, including padding after the last guest byte.
  bool (*readPage)(void *, uint32_t page, uint8_t out[PageBytes]) = nullptr;
  bool (*writePage)(void *, uint32_t page, const uint8_t data[PageBytes]) = nullptr;
};

// Foreground-only memory cache. It owns no allocation and exposes no resident
// pointers: a span may safely cross pages and evict a previously visited page.
// The store is disposable per-launch scratch; reset deliberately ignores all
// its old contents instead of clearing a megabyte on SD at every launch.
class PagedMemory {
 public:
  static constexpr uint16_t PageBytes = PageStore::PageBytes;
  static constexpr uint16_t ResidentFrames = 256;
  static constexpr uint32_t VirtualBytes = NativeBackingBytes;
  static constexpr uint32_t PageCount = (VirtualBytes + PageBytes - 1) / PageBytes;
  static constexpr size_t CacheBytes = size_t(ResidentFrames) * PageBytes;
  static constexpr size_t WorkspaceBytes = CacheBytes + PageCount * 2u +
      ResidentFrames * 2u + ResidentFrames + (PageCount + 7u) / 8u;

  struct Stats {
    uint32_t hits = 0, misses = 0, evictions = 0;
    uint32_t pageReads = 0, pageWrites = 0, zeroPages = 0;
    uint32_t ioFailures = 0;
  };

  // Definitions live outside the class to avoid inline COMDAT section flags
  // conflicting with other functions in Teensy's shared FLASHMEM section.
  MPE5_CODE bool start(void *workspace, size_t bytes, const PageStore &store);
  MPE5_CODE bool reset();
  MPE5_CODE bool read(uint32_t address, uint8_t *out, uint32_t length);
  MPE5_CODE bool write(uint32_t address, const uint8_t *data, uint32_t length);
  MPE5_CODE bool failed() const;
  MPE5_CODE Stats stats() const;

 private:
  static constexpr uint16_t Invalid = 0xffff;
  static constexpr uint8_t Referenced = 1, Dirty = 2;
  static constexpr size_t LookupOffset = CacheBytes;
  static constexpr size_t TagsOffset = LookupOffset + PageCount * 2u;
  static constexpr size_t FlagsOffset = TagsOffset + ResidentFrames * 2u;
  static constexpr size_t PresenceOffset = FlagsOffset + ResidentFrames;
  static constexpr size_t PresenceBytes = (PageCount + 7u) / 8u;
  static_assert(PageCount < Invalid, "Page identifiers must fit the cache tags");
  static_assert(WorkspaceBytes <= 136u * 1024u, "DOS page cache exceeds its RAM budget");

  uint8_t *memory_ = nullptr;
  PageStore store_{};
  Stats counts_{};
  uint16_t hand_ = 0;
  bool failed_ = false;

  MPE5_CODE bool validSpan(uint32_t address, uint32_t length) const;
  MPE5_CODE uint16_t get16(size_t offset) const;
  MPE5_CODE void put16(size_t offset, uint16_t value);
  MPE5_CODE bool present(uint32_t page) const;
  MPE5_CODE uint16_t ioFailure();
  MPE5_CODE uint16_t acquire(uint32_t page);
};

}  // namespace mpe5
#endif
