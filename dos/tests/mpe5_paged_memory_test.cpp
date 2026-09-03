#include "../../engine/native-dos/mpe5_paged_memory.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <vector>

using mpe5::PagedMemory;
static void require(bool ok, const char *message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

struct Store {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(PagedMemory::PageCount * 512u, 0xcc);
  uint32_t reads = 0, writes = 0;
  bool failRead = false, failWrite = false;
  static bool read(void *context, uint32_t page, uint8_t *out) {
    auto &self = *static_cast<Store *>(context);
    ++self.reads;
    require(page < PagedMemory::PageCount, "store read outside virtual page range");
    if (self.failRead) return false;
    memcpy(out, self.bytes.data() + page * 512u, 512u);
    return true;
  }
  static bool write(void *context, uint32_t page, const uint8_t *data) {
    auto &self = *static_cast<Store *>(context);
    ++self.writes;
    require(page < PagedMemory::PageCount, "store write outside virtual page range");
    if (self.failWrite) return false;
    memcpy(self.bytes.data() + page * 512u, data, 512u);
    return true;
  }
  mpe5::PageStore callbacks() { return {this, read, write}; }
};

static uint8_t pattern(uint32_t address) {
  return uint8_t((address * 37u) ^ (address >> 8u) ^ (address >> 16u));
}

static uint32_t pageSpan(uint32_t address, uint32_t length) {
  return length ? ((address % 512u) + length + 511u) / 512u : 0u;
}

static void checkIoBound(const Store &store, uint32_t before, uint32_t address, uint32_t length) {
  // A miss may perform one dirty write and one page read. A single request
  // must never trigger a full-cache flush or more I/O than its touched pages.
  require(store.reads + store.writes - before <= 2u * pageSpan(address, length),
          "operation exceeded two callbacks per touched page");
}

int main() {
  static_assert(PagedMemory::WorkspaceBytes == 136762u, "Update the documented paging budget");
  Store store;
  // Deliberately unaligned workspace with guards on both sides.
  std::vector<uint8_t> workspace(PagedMemory::WorkspaceBytes + 2u, 0xa5);
  auto *memory = workspace.data() + 1;
  PagedMemory pager;
  require(!pager.reset(), "unstarted pager reset must fail");
  require(!pager.start(memory, PagedMemory::WorkspaceBytes - 1, store.callbacks()), "undersized workspace accepted");
  require(!pager.start(nullptr, PagedMemory::WorkspaceBytes, store.callbacks()), "null workspace accepted");
  require(!pager.start(memory, PagedMemory::WorkspaceBytes, {}), "missing store accepted");
  require(pager.start(memory, PagedMemory::WorkspaceBytes, store.callbacks()), "pager start");

  uint8_t page[512];
  memset(page, 0xff, sizeof(page));
  require(pager.read(512u * 1000u, page, sizeof(page)), "lazy-zero read");
  require(std::all_of(page, page + 512, [](uint8_t c) { return c == 0; }), "old store data leaked into new launch");
  require(store.reads == 0 && store.writes == 0, "untouched page performed store I/O");

  // Touch the full address range, much larger than the resident cache, then
  // read it in reverse. Every evicted dirty byte must survive SD round trips.
  for (uint32_t address = 0; address < PagedMemory::VirtualBytes; address += 512u) {
    const uint32_t length = std::min(512u, PagedMemory::VirtualBytes - address);
    for (uint32_t i = 0; i < length; ++i) page[i] = pattern(address + i);
    const uint32_t before = store.reads + store.writes;
    require(pager.write(address, page, length), "full-range write");
    checkIoBound(store, before, address, length);
  }
  require(store.writes > 256u, "dirty eviction was not exercised");
  for (uint32_t index = PagedMemory::PageCount; index-- > 0;) {
    const uint32_t address = index * 512u;
    const uint32_t length = std::min(512u, PagedMemory::VirtualBytes - address);
    const uint32_t before = store.reads + store.writes;
    require(pager.read(address, page, length), "evicted-page read");
    checkIoBound(store, before, address, length);
    for (uint32_t i = 0; i < length; ++i) require(page[i] == pattern(address + i), "dirty eviction lost a byte");
  }
  require(pager.stats().pageReads > 0 && pager.stats().pageWrites > 0, "missing pager I/O statistics");

  // Make every resident frame dirty, then request a persisted nonresident
  // page. This proves the worst single-page callback count is exactly two.
  for (uint32_t index = 0; index < 256u; ++index) {
    page[0] = pattern(index * 512u);
    require(pager.write(index * 512u, page, 1u), "dirty resident frame for I/O bound");
  }
  const uint32_t beforeWorst = store.reads + store.writes;
  const auto statsBeforeWorst = pager.stats();
  require(pager.read(1000u * 512u, page, 1u), "worst-case page fetch");
  require(store.reads + store.writes - beforeWorst == 2u, "worst-case page fetch did not exercise write plus read");
  const auto statsAfterWorst = pager.stats();
  require(statsAfterWorst.pageReads == statsBeforeWorst.pageReads + 1u &&
          statsAfterWorst.pageWrites == statsBeforeWorst.pageWrites + 1u &&
          statsAfterWorst.misses == statsBeforeWorst.misses + 1u &&
          statsAfterWorst.evictions == statsBeforeWorst.evictions + 1u,
          "worst-case callback counters are inconsistent");
  require(pager.read(1000u * 512u, page, 1u), "resident hit");
  require(store.reads + store.writes == beforeWorst + 2u &&
          pager.stats().hits == statsAfterWorst.hits + 1u, "resident hit performed I/O or missed its counter");

  std::vector<uint8_t> crossing(1600u), received(crossing.size());
  for (size_t i = 0; i < crossing.size(); ++i) crossing[i] = uint8_t(i * 19u + 7u);
  require(pager.write(509u, crossing.data(), uint32_t(crossing.size())), "cross-page write");
  require(pager.read(509u, received.data(), uint32_t(received.size())) && received == crossing, "cross-page span mismatch");

  // One span itself exceeds the complete resident cache, forcing eviction
  // while the same read/write call advances across its constituent pages.
  crossing.resize((PagedMemory::ResidentFrames + 3u) * 512u + 14u);
  received.resize(crossing.size());
  for (size_t i = 0; i < crossing.size(); ++i) crossing[i] = uint8_t(i * 23u + 9u);
  uint32_t beforeSpan = store.reads + store.writes;
  require(pager.write(499u, crossing.data(), uint32_t(crossing.size())), "span larger than cache write");
  checkIoBound(store, beforeSpan, 499u, uint32_t(crossing.size()));
  beforeSpan = store.reads + store.writes;
  require(pager.read(499u, received.data(), uint32_t(received.size())) && received == crossing,
          "span larger than cache read lost evicted data");
  checkIoBound(store, beforeSpan, 499u, uint32_t(received.size()));
  const uint32_t ioBeforeBounds = store.reads + store.writes;
  require(!pager.read(PagedMemory::VirtualBytes - 1u, page, 2u), "read accepted past guest end");
  require(!pager.write(0xffffffffu, page, 2u), "write accepted wrapping address");
  require(!pager.read(0u, nullptr, 1u) && !pager.write(0u, nullptr, 1u), "null span accepted");
  require(pager.read(PagedMemory::VirtualBytes, nullptr, 0u), "empty end-of-map read");
  require(pager.write(PagedMemory::VirtualBytes, nullptr, 0u), "empty end-of-map write");
  require(store.reads + store.writes == ioBeforeBounds, "invalid span reached backing store");

  require(pager.reset(), "pager reset");
  const uint32_t oldReads = store.reads, oldWrites = store.writes;
  for (uint32_t address = 0; address < PagedMemory::VirtualBytes; address += 512u) {
    const uint32_t length = std::min(512u, PagedMemory::VirtualBytes - address);
    require(pager.read(address, page, length), "reset lazy-zero read");
    require(std::all_of(page, page + length, [](uint8_t c) { return c == 0; }), "reset exposed old SD data");
  }
  require(store.reads == oldReads && store.writes == oldWrites, "reset should not clear or read old SD pages");

  // Persist page zero by evicting it, then fail its reload. Even resident
  // accesses must stop after the failed callback until an explicit reset.
  require(pager.reset(), "read-failure reset");
  page[0] = 0x71;
  require(pager.write(0u, page, 1u), "seed persisted page");
  for (uint32_t p = 1; p <= 256u; ++p) require(pager.read(p * 512u, page, 1u), "fill read-failure cache");
  store.failRead = true;
  require(!pager.read(0u, page, 1u) && pager.failed(), "store read failure not sticky");
  const uint32_t failedIo = store.reads + store.writes;
  require(!pager.read(512u, page, 1u) && !pager.write(512u, page, 1u), "sticky read failure allowed later access");
  require(store.reads + store.writes == failedIo && pager.stats().ioFailures == 1u, "sticky read failure retried I/O");
  require(pager.reset() && !pager.failed(), "reset failed to clear I/O failure");
  require(pager.read(0u, page, 1u) && page[0] == 0, "reset retried stale page after I/O failure");
  store.failRead = false;

  require(pager.reset(), "write-failure reset");
  page[0] = 0x19;
  require(pager.write(0u, page, 1u), "seed write-failure page");
  for (uint32_t p = 1; p < 256u; ++p) require(pager.read(p * 512u, page, 1u), "fill write-failure cache");
  store.failWrite = true;
  require(!pager.read(256u * 512u, page, 1u) && pager.failed(), "dirty-store failure not sticky");
  const uint32_t failedWrites = store.writes;
  require(!pager.read(0u, page, 1u) && store.writes == failedWrites, "dirty-store failure retried or continued");
  require(workspace.front() == 0xa5 && workspace.back() == 0xa5, "pager wrote outside supplied workspace");

  std::cout << "MPE5 paged memory passed: " << PagedMemory::WorkspaceBytes
            << " workspace bytes, dirty eviction, lazy-zero reset, cross-page spans, bounds, sticky I/O failures,"
               " and at most two store callbacks per touched page.\n";
}
