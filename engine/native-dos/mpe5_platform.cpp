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
  if (port != 0x42 && port != 0x43 && port != 0x61) return false;
  const bool wasActive = active();
  const uint32_t wasCount = effectiveReload();
  if (port == 0x61) {
    gate = value;
  } else if (port == 0x43) {
    // Latch commands and other channels must not disturb a partial write.
    const uint8_t nextAccess = uint8_t((value >> 4) & 3u);
    if ((value >> 6) == 2u && nextAccess) {
      access = nextAccess;
      mode = uint8_t((value >> 1) & 7u);
      if (mode >= 6u) mode -= 4u; // 6/7 alias periodic modes 2/3.
      bcd = (value & 1u) != 0;
      phase = 0;
      loaded = false;
    }
  } else if (port == 0x42) {
    if (access == 1u) { divisor = value; loaded = true; }
    else if (access == 2u) { divisor = uint16_t(value) << 8; loaded = true; }
    else if (access == 3u) {
      // Keep the old running count until both bytes of a reload arrive.
      if (!phase) low = value;
      else { divisor = uint16_t(low | (uint16_t(value) << 8)); loaded = true; }
      phase ^= 1u;
    }
  }
  const bool nowActive = active();
  const bool changed = wasActive != nowActive ||
      (nowActive && wasCount != effectiveReload());
  if (!wasActive && nowActive) ++starts;
  if (changed) ++changes;
  return changed;
}

uint32_t PcSpeaker::effectiveReload() const {
  if (!bcd) return divisor ? divisor : 65536u;
  if (!divisor) return 10000u;
  uint32_t result = 0, scale = 1;
  for (uint8_t shift = 0; shift < 16u; shift += 4u) {
    const uint8_t digit = uint8_t((divisor >> shift) & 15u);
    if (digit > 9u) return 0; // Invalid packed BCD cannot define a tone.
    result += digit * scale;
    scale *= 10u;
  }
  return result;
}

bool PcSpeaker::active() const {
  return loaded && (gate & 3u) == 3u &&
      (mode == 2u || mode == 3u) && effectiveReload() > 1u;
}

uint32_t PcSpeaker::frequencyHz() const {
  return active() ? ClockHz / effectiveReload() : 0u;
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
