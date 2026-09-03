// Unit acceptance for the reset-only Teensy 4.1 DOS memory map. The firmware
// gives guest addresses 00000h-7FFFFh the complete 512 KiB RAM2 device; every
// other writable region is supplied independently from RAM1.

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../../engine/native-dos/mpe5_direct_memory.h"

namespace {

constexpr uint8_t kGuard = 0xd3;
constexpr size_t kGuardBytes = 37;
constexpr size_t kHighStride = mpe5::DirectMemory::HighChunkBytes + 4u;
constexpr size_t kMinimumHighStorageBytes =
    (mpe5::DirectMemory::HighChunks - 1u) * kHighStride +
    mpe5::DirectMemory::HighChunkBytes;
// Production passes the complete stcSwapBuffers[16] allocation, including
// the final slot's Offset word. It must survive reset just like the 15
// metadata words between logical 8 KiB chunks.
constexpr size_t kHighStorageBytes =
    mpe5::DirectMemory::HighChunks * kHighStride;

struct Guarded {
  explicit Guarded(size_t bytes)
      : bytes(bytes + 2u * kGuardBytes, kGuard) {}

  uint8_t *data() { return bytes.data() + kGuardBytes; }
  size_t size() const { return bytes.size() - 2u * kGuardBytes; }
  void fill(uint8_t value) {
    std::fill(bytes.begin() + kGuardBytes, bytes.end() - kGuardBytes, value);
  }
  void guards() const {
    if (!std::all_of(bytes.begin(), bytes.begin() + kGuardBytes,
                     [](uint8_t value) { return value == kGuard; }) ||
        !std::all_of(bytes.end() - kGuardBytes, bytes.end(),
                     [](uint8_t value) { return value == kGuard; }))
      throw std::runtime_error("direct-memory operation crossed a RAM arena boundary");
  }

  std::vector<uint8_t> bytes;
};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void expectBytes(mpe5::DirectMemory &memory, uint32_t address,
                 const std::vector<uint8_t> &expected) {
  std::vector<uint8_t> actual(expected.size(), 0x69);
  require(memory.read(address, actual.data(), static_cast<uint32_t>(actual.size())),
          "mapped read was rejected");
  require(actual == expected, "mapped read returned the wrong bytes");
}

}  // namespace

int main() {
  try {
    static_assert(mpe5::DirectMemory::ConventionalBytes == 0x80000u,
                  "guest conventional RAM must be exactly 512 KiB");
    static_assert(mpe5::DirectMemory::HighBase == 0xb0000u,
                  "PC video aperture moved");
    static_assert(mpe5::DirectMemory::HighBytes == 0x20000u,
                  "PC video aperture must cover B0000h-CFFFFh");
    static_assert(mpe5::DirectMemory::PortBase == mpe5::AddressMapBytes,
                  "8086tiny I/O latches must follow the address map");
    static_assert(mpe5::DirectMemory::PortBytes == 0x10000u,
                  "all 65536 I/O-port latches must remain addressable");

    Guarded conventional(mpe5::DirectMemory::ConventionalBytes);
    // Match MinimalBoot's stcSwapBuffers layout. Image is 8192 bytes and the
    // next chunk starts four bytes later so every slot's Offset field survives.
    Guarded high(kHighStorageBytes);
    Guarded ports(mpe5::DirectMemory::PortBytes);
    mpe5::DirectMemory memory;

    require(!memory.start(nullptr, conventional.size(), high.data(), high.size(), kHighStride,
                          ports.data(), ports.size()),
            "null conventional RAM was accepted");
    require(!memory.start(conventional.data(), conventional.size() - 1u,
                          high.data(), high.size(), kHighStride, ports.data(), ports.size()),
            "a conventional arena smaller than 512 KiB was accepted");
    require(!memory.start(conventional.data(), conventional.size() + 1u,
                          high.data(), high.size(), kHighStride,
                          ports.data(), ports.size()),
            "a non-exact conventional arena was accepted");
    require(!memory.start(conventional.data(), conventional.size(), high.data(),
                          kMinimumHighStorageBytes - 1u, kHighStride,
                          ports.data(), ports.size()),
            "a short PC high-memory arena was accepted");
    require(!memory.start(conventional.data(), conventional.size(), high.data(),
                          high.size(), kHighStride, ports.data(), ports.size() - 1u),
            "a short I/O-port arena was accepted");
    require(!memory.start(conventional.data(), conventional.size(), high.data(),
                          high.size(), mpe5::DirectMemory::HighChunkBytes - 1u,
                          ports.data(), ports.size()),
            "an overlapping high-memory chunk stride was accepted");
    require(memory.start(conventional.data(), conventional.size(), high.data(),
                         high.size(), kHighStride, ports.data(), ports.size()),
            "complete direct-memory arenas were rejected");
    require(memory.conventional() == conventional.data(),
            "guest address zero is not the supplied RAM2 base");

    conventional.fill(0xa5); high.fill(0xb6); ports.fill(0xc7);
    require(memory.reset(), "direct-memory reset failed");
    require(std::all_of(conventional.data(), conventional.data() + conventional.size(),
                        [](uint8_t value) { return value == 0; }),
            "reset did not clear all 512 KiB of conventional RAM");
    for (uint32_t chunk = 0; chunk < mpe5::DirectMemory::HighChunks; ++chunk) {
      const uint8_t *image = high.data() + chunk * kHighStride;
      require(std::all_of(image,
                          image + mpe5::DirectMemory::HighChunkBytes,
                          [](uint8_t value) { return value == 0; }),
              "reset did not clear a PC high-memory image chunk");
      require(std::all_of(image + mpe5::DirectMemory::HighChunkBytes,
                          image + kHighStride,
                          [](uint8_t value) { return value == 0xb6; }),
              "reset overwrote a SwapBuffers Offset field");
    }
    require(std::all_of(ports.data(), ports.data() + ports.size(),
                        [](uint8_t value) { return value == 0; }),
            "reset did not clear all I/O-port latches");

    // Exact first/last conventional addresses and a span ending at 80000h.
    const std::vector<uint8_t> lowPattern{0x11, 0x22, 0x33, 0x44};
    require(memory.write(0, lowPattern.data(), uint32_t(lowPattern.size())),
            "write at guest address zero failed");
    expectBytes(memory, 0, lowPattern);
    const std::vector<uint8_t> lowEnd{0x51, 0x52, 0x53, 0x54};
    require(memory.write(0x7fffcu, lowEnd.data(), uint32_t(lowEnd.size())),
            "write at the end of 512 KiB conventional RAM failed");
    expectBytes(memory, 0x7fffcu, lowEnd);

    // Holes read as zero and discard writes, including a transfer crossing
    // directly from RAM2 into the 80000h-AFFFFh hole.
    const std::vector<uint8_t> crossing{0x71, 0x72, 0x73, 0x74};
    require(memory.write(0x7fffeu, crossing.data(), uint32_t(crossing.size())),
            "conventional-to-hole write failed");
    expectBytes(memory, 0x7fffeu, {0x71, 0x72, 0x00, 0x00});
    const std::vector<uint8_t> holeWrite{0xe1, 0xe2};
    require(memory.write(0x80000u, holeWrite.data(), 2), "hole write was rejected");
    expectBytes(memory, 0x80000u, {0, 0});

    // B0000h-CFFFFh is a separate RAM1 aperture. Both boundaries and spans
    // crossing into/out of it must select the correct backing array.
    const std::vector<uint8_t> highPattern{0x81, 0x82, 0x83, 0x84};
    require(memory.write(0xafffeu, highPattern.data(), 4),
            "hole-to-high-aperture write failed");
    expectBytes(memory, 0xafffeu, {0, 0, 0x83, 0x84});
    require(high.data()[0] == 0x83 && high.data()[1] == 0x84,
            "B0000h did not map to the first high-aperture byte");
    const std::vector<uint8_t> chunkCrossing{0xc1, 0xc2, 0xc3, 0xc4};
    require(memory.write(mpe5::DirectMemory::HighBase +
                         mpe5::DirectMemory::HighChunkBytes - 2u,
                         chunkCrossing.data(), 4),
            "write across high-memory image chunks failed");
    expectBytes(memory, mpe5::DirectMemory::HighBase +
                mpe5::DirectMemory::HighChunkBytes - 2u, chunkCrossing);
    require(high.data()[mpe5::DirectMemory::HighChunkBytes - 2u] == 0xc1 &&
            high.data()[mpe5::DirectMemory::HighChunkBytes - 1u] == 0xc2 &&
            high.data()[kHighStride] == 0xc3 &&
            high.data()[kHighStride + 1u] == 0xc4 &&
            std::all_of(high.data() + mpe5::DirectMemory::HighChunkBytes,
                        high.data() + kHighStride,
                        [](uint8_t value) { return value == 0xb6; }),
            "high-memory chunk crossing overwrote a SwapBuffers Offset field");
    require(memory.write(0xcfffeu, highPattern.data(), 4),
            "high-aperture-to-hole write failed");
    expectBytes(memory, 0xcfffeu, {0x81, 0x82, 0, 0});
    const size_t highLogicalEnd =
        (mpe5::DirectMemory::HighChunks - 1u) * kHighStride +
        mpe5::DirectMemory::HighChunkBytes;
    require(high.data()[highLogicalEnd - 2u] == 0x81 &&
            high.data()[highLogicalEnd - 1u] == 0x82,
            "CFFFFh did not map to the last high-aperture byte");

    // The core owns F0000h-FFFFFh as its pinned BIOS/register buffer. A call
    // into DirectMemory is an adapter error and must fail instead of aliasing.
    std::array<uint8_t, 2> pinned{{0x4a, 0x5b}};
    require(!memory.read(0xf0000u, pinned.data(), 1),
            "F0000h read incorrectly bypassed the pinned BIOS buffer");
    require(!memory.write(0xfffffu, pinned.data(), 1),
            "FFFFFh write incorrectly bypassed the pinned BIOS buffer");

    // 8086 wraparound from 100000h through 10FFEFh aliases the first 65520
    // conventional bytes. The next byte begins the independent port array.
    conventional.data()[0] = 0x91;
    conventional.data()[0xffef] = 0x92;
    expectBytes(memory, 0x100000u, {0x91});
    expectBytes(memory, mpe5::AddressMapBytes - 1u, {0x92});
    const std::vector<uint8_t> wrapToPort{0xa1, 0xa2, 0xa3, 0xa4};
    require(memory.write(mpe5::AddressMapBytes - 2u, wrapToPort.data(), 4),
            "wraparound-to-port write failed");
    require(conventional.data()[0xffee] == 0xa1 &&
            conventional.data()[0xffef] == 0xa2 &&
            ports.data()[0] == 0xa3 && ports.data()[1] == 0xa4,
            "wraparound/port boundary selected the wrong arena");
    expectBytes(memory, mpe5::AddressMapBytes - 2u, wrapToPort);

    const std::vector<uint8_t> portEnd{0xb1, 0xb2};
    require(memory.write(mpe5::DirectMemory::PortBase +
                         mpe5::DirectMemory::PortBytes - 2u,
                         portEnd.data(), 2),
            "write at the end of the I/O-port array failed");
    expectBytes(memory, mpe5::DirectMemory::PortBase +
                mpe5::DirectMemory::PortBytes - 2u, portEnd);
    std::array<uint8_t, 2> rejected{{0x61, 0x62}};
    require(!memory.read(mpe5::DirectMemory::PortBase +
                         mpe5::DirectMemory::PortBytes,
                         rejected.data(), 1),
            "native console storage incorrectly entered DirectMemory");
    require(rejected[0] == 0x61,
            "rejected native-console read modified its output");
    require(!memory.read(mpe5::NativeBackingBytes - 1u,
                         rejected.data(), 2),
            "out-of-range span was accepted");
    require(!memory.write(mpe5::NativeBackingBytes + 1u,
                          rejected.data(), 1),
            "out-of-range write was accepted");

    conventional.guards(); high.guards(); ports.guards();
    std::cout << "MPE5 direct-memory acceptance passed: exact 512 KiB low RAM, "
                 "RAM1 high/port arenas, holes, F000 pin, 20-bit wrap and guards.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "MPE5 direct-memory acceptance failed: " << error.what() << '\n';
    return 1;
  }
}
