#ifndef MPE5_PLATFORM_H
#define MPE5_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifndef MPE5_CODE
#define MPE5_CODE
#endif
#ifndef MPE5_HOT_CODE
#define MPE5_HOT_CODE MPE5_CODE
#endif

namespace mpe5 {

// The reset-only native DOS session owns all 512 KiB of Teensy 4.1 RAM2 as
// contiguous conventional guest memory.
static constexpr uint32_t ConventionalRamBytes = 512u * 1024u;
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
  // Optional; a missing callback keeps existing hosts read-only. The host
  // persists each complete sector before reporting success.
  bool (*writeSector)(void *context, uint32_t lba, const uint8_t data[SectorBytes]) = nullptr;
};

struct Key {
  uint8_t ascii = 0;
  uint8_t scan = 0;
  // Bit7: native set-1 event (scan bit7 is break); bits0..2: Shift/Ctrl/Alt.
  // Zero retains the original ASCII tap interface used by host scripts.
  uint8_t flags = 0;
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
  MPE5_CODE bool peek(Key &key) const;
  MPE5_CODE bool pop(Key &key);
  // Atomically expand the complete C64 state into make/break events. Joystick
  // directions become cursor keys; fire becomes Shift (grab in Boulder).
  MPE5_CODE bool acceptSnapshot(uint8_t ascii, uint8_t scan, uint8_t modifiers,
                               uint8_t joystick, bool repeat = false);
  MPE5_CODE void clear();
  uint8_t count() const { return used; }
  bool nativePending() const { return used && (entries[head].flags & 0x80u); }
 private:
  static constexpr uint8_t Capacity = 32;
  Key entries[Capacity]{};
  uint8_t head = 0, tail = 0, used = 0;
  uint8_t held[16]{};
};

class PcSpeaker {
 public:
  static constexpr uint32_t ClockHz = 1193182u;
  // Feed writes to 0x42, 0x43 and 0x61. Returns true when audible state or
  // pitch changed. Periodic modes 2/3 feed the tone adapter; direct DAC and
  // one-shot modes require a sampled audio path and are not synthesized.
  MPE5_CODE bool write(uint16_t port, uint8_t value);
  MPE5_CODE bool active() const;
  uint16_t reload() const { return divisor; }
  MPE5_CODE uint32_t effectiveReload() const;
  MPE5_CODE uint32_t frequencyHz() const;
  uint32_t revision() const { return changes; }
  uint32_t restartToken() const { return starts; }
 private:
  uint16_t divisor = 0;
  uint8_t gate = 0, access = 0, phase = 0, low = 0, mode = 0;
  bool loaded = false, bcd = false;
  uint32_t changes = 0, starts = 0;
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
