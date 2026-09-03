#ifndef MPE5_PLATFORM_H
#define MPE5_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifndef MPE5_CODE
#define MPE5_CODE
#endif

namespace mpe5 {

// 8086tiny keeps register scratch, ROM and display memory in the same 20-bit
// backing map.  Only the first 640 KiB is conventional guest RAM.
static constexpr uint32_t ConventionalRamBytes = 640u * 1024u;
static constexpr uint32_t AddressMapBytes = 0x10fff0u;
static constexpr uint32_t SharedArenaBytes = 1310700u;
static constexpr uint32_t NativeIoPortBytes = 0x10000u;
static constexpr uint32_t CgaTextAddress = 0xb8000u;
static constexpr uint16_t CgaTextColumns = 40u;
static constexpr uint16_t CgaTextRows = 25u;
static constexpr uint16_t CgaTextCells = CgaTextColumns * CgaTextRows;
// The BIOS owns B800 (VRAM), C000 (console readback), and C800 (VRAM
// comparison shadow). Host terminal output must never overwrite those pages.
static constexpr uint16_t NativeTextColumns = 80u;
static constexpr uint32_t NativeTextShadowAddress = AddressMapBytes + NativeIoPortBytes;
static constexpr uint32_t NativeTextViewportAddress = NativeTextShadowAddress +
    NativeTextColumns * CgaTextRows * 2u;
static constexpr uint32_t NativeBackingBytes = NativeTextViewportAddress + CgaTextCells * 2u;
static constexpr uint16_t SectorBytes = 512u;

struct BlockDevice {
  void *context = nullptr;
  bool (*readSector)(void *context, uint32_t lba, uint8_t out[SectorBytes]) = nullptr;
  uint32_t sectors = 0;
};

struct Key {
  uint8_t ascii = 0;
  uint8_t scan = 0;
};

// The native glue converts every changed text cell to this compact record.
// A C64 receiver stores glyph in screen RAM and colour in colour RAM.
struct TextCell {
  uint16_t cell = 0;
  uint8_t glyph = 0;
  uint8_t colour = 0;
};

class Keyboard {
 public:
  MPE5_CODE bool push(Key key);
  MPE5_CODE bool pop(Key &key);
  MPE5_CODE void clear();
  uint8_t count() const { return used; }
 private:
  static constexpr uint8_t Capacity = 32;
  Key entries[Capacity]{};
  uint8_t head = 0, tail = 0, used = 0;
};

class PcSpeaker {
 public:
  // Feed writes to 0x42, 0x43 and 0x61. Returns true when audible state or
  // pitch changed, so a caller can publish a SID update only when necessary.
  MPE5_CODE bool write(uint16_t port, uint8_t value);
  bool active() const { return (gate & 3u) == 3u && divisor != 0; }
  uint16_t reload() const { return divisor; }
  MPE5_CODE uint32_t frequencyHz() const;
 private:
  uint16_t divisor = 0;
  uint8_t gate = 0, access = 0, phase = 0;
};

class CgaText {
 public:
  MPE5_CODE void reset();
  // Return at most maximum records, each serialized little-endian as
  // {cellLo, cellHi, CP437 glyph, C64 colour}. The source must point at the
  // native 40x25 console viewport's character/attribute pairs. After reset, send every
  // cell exactly once before returning dirty updates, including zero cells.
  // Callers publish/acknowledge each batch before requesting the next one.
  MPE5_CODE uint16_t changes(const uint8_t *source, uint8_t *records, uint16_t maximum);
  bool initialComplete() const { return initialCell == CgaTextCells; }
 private:
  uint8_t shown[CgaTextCells * 2]{};
  uint16_t initialCell = 0, scanCell = 0;
  MPE5_CODE static uint8_t colour(uint8_t cgaAttribute);
};

class BootDrive {
 public:
  MPE5_CODE bool open(const BlockDevice &candidate);
  MPE5_CODE bool bootSector(uint8_t out[SectorBytes]) const;
  bool valid() const { return device.readSector != nullptr; }
  uint32_t sectors() const { return device.sectors; }
 private:
  BlockDevice device{};
};

}  // namespace mpe5

#endif
