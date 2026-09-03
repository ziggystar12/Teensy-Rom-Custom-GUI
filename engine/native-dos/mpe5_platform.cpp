#include "mpe5_platform.h"

#include <string.h>

namespace mpe5 {

bool Keyboard::push(Key key) {
  if (used == Capacity) return false;
  entries[tail] = key;
  tail = uint8_t((tail + 1u) % Capacity);
  ++used;
  return true;
}

bool Keyboard::pop(Key &key) {
  if (!used) return false;
  key = entries[head];
  head = uint8_t((head + 1u) % Capacity);
  --used;
  return true;
}

void Keyboard::clear() { head = tail = used = 0; }

bool PcSpeaker::write(uint16_t port, uint8_t value) {
  const bool wasActive = active();
  const uint16_t wasDivisor = divisor;
  if (port == 0x61) {
    gate = value;
  } else if (port == 0x43) {
    // Channel 2, lobyte/hibyte access. Other PIT channels do not drive the
    // PC speaker and remain the 8086 core's timer concern.
    if ((value >> 6) == 2u) {
      access = uint8_t((value >> 4) & 3u);
      phase = 0;
    }
  } else if (port == 0x42) {
    if (access == 1u) divisor = uint16_t((divisor & 0xff00u) | value);
    else if (access == 2u) divisor = uint16_t((divisor & 0x00ffu) | (uint16_t(value) << 8));
    else if (access == 3u) {
      if (!phase) divisor = uint16_t((divisor & 0xff00u) | value);
      else divisor = uint16_t((divisor & 0x00ffu) | (uint16_t(value) << 8));
      phase ^= 1u;
    }
  }
  return wasActive != active() || wasDivisor != divisor;
}

uint32_t PcSpeaker::frequencyHz() const {
  return active() ? 1193182u / divisor : 0u;
}

void CgaText::reset() {
  memset(shown, 0, sizeof(shown));
  initialCell = scanCell = 0;
}

uint8_t CgaText::colour(uint8_t attribute) {
  // The low nibble is the CGA foreground. Map PC's standard 16 colours to
  // the nearest VIC-II colour. DOS's black-background text remains legible.
  static const uint8_t map[16] = {
      0, 6, 5, 3, 2, 4, 8, 1, 11, 14, 13, 3, 10, 4, 7, 1};
  return map[attribute & 15u];
}

uint16_t CgaText::changes(const uint8_t *source, uint8_t *records, uint16_t maximum) {
  if (!source || !records || !maximum) return 0;
  uint16_t count = 0;
  const bool initial = !initialComplete();
  // Keep initial coverage separate from dirty detection: changes to earlier
  // cells cannot count twice toward a complete frame or starve later cells.
  // A subsequent dirty sweep catches edits made after a cell was first sent.
  for (uint16_t checked = 0; checked < CgaTextCells && count < maximum; ++checked) {
    if (initial && initialComplete()) break;
    const uint16_t cell = initial ? initialCell++ : scanCell;
    if (!initial) scanCell = uint16_t((scanCell + 1u) % CgaTextCells);
    const uint16_t offset = uint16_t(cell * 2u);
    if (!initial && shown[offset] == source[offset] && shown[offset + 1u] == source[offset + 1u]) continue;
    const uint16_t record = uint16_t(count * sizeof(TextCell));
    records[record] = uint8_t(cell);
    records[record + 1u] = uint8_t(cell >> 8);
    records[record + 2u] = source[offset];
    records[record + 3u] = colour(source[offset + 1u]);
    shown[offset] = source[offset];
    shown[offset + 1u] = source[offset + 1u];
    ++count;
  }
  return count;
}

bool BootDrive::open(const BlockDevice &candidate) {
  device = {};
  if (!candidate.readSector || !candidate.sectors) return false;
  uint8_t sector[SectorBytes];
  if (!candidate.readSector(candidate.context, 0, sector) ||
      sector[510] != 0x55 || sector[511] != 0xaa) return false;
  device = candidate;
  return true;
}

bool BootDrive::bootSector(uint8_t out[SectorBytes]) const {
  return valid() && device.readSector(device.context, 0, out);
}

}  // namespace mpe5
