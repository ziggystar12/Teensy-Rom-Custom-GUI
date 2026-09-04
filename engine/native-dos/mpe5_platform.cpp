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

bool Keyboard::peek(Key &key) const {
  if (!used) return false;
  key = entries[head];
  return true;
}

bool Keyboard::pop(Key &key) {
  if (!used) return false;
  key = entries[head];
  head = uint8_t((head + 1u) % Capacity);
  --used;
  return true;
}

void Keyboard::clear() { head = tail = used = 0; memset(held, 0, sizeof(held)); }

bool Keyboard::acceptSnapshot(uint8_t ascii, uint8_t scan, uint8_t modifiers,
                              uint8_t joystick, bool repeat) {
  uint8_t next[16]{};
  const auto has = [](const uint8_t *bits, uint8_t code) {
    return (bits[code >> 3] & (1u << (code & 7u))) != 0;
  };
  const auto add = [&](uint8_t code) { next[code >> 3] |= uint8_t(1u << (code & 7u)); };
  modifiers &= 7u;
  if (joystick & 16u) modifiers |= 1u;
  if (scan && scan < 128u) add(scan); else scan = 0;
  if (modifiers & 1u) add(0x36);
  if (modifiers & 2u) add(0x1d);
  if (modifiers & 4u) add(0x38);
  // Opposite directions cancel, including a noisy joystick switch pair.
  if ((joystick & 3u) == 1u) add(0x48);
  if ((joystick & 3u) == 2u) add(0x50);
  if ((joystick & 12u) == 4u) add(0x4b);
  if ((joystick & 12u) == 8u) add(0x4d);
  const bool repeated = repeat && scan && has(held, scan) && has(next, scan);
  unsigned needed = repeated ? 1u : 0u;
  for (uint8_t code = 1; code < 128; ++code) needed += has(held,code) != has(next,code);
  if (needed > unsigned(Capacity - used)) return false; // Retry the whole snapshot unchanged.
  const uint8_t flags = uint8_t(0x80u | modifiers);
  for (uint8_t code = 1; code < 128; ++code)
    if (has(held,code) && !has(next,code)) push({0,uint8_t(code | 0x80u),flags});
  // Modifier makes precede ordinary keys so raw IRQ handlers can combine them.
  const uint8_t modifierScans[] = {0x36,0x1d,0x38};
  for (uint8_t code : modifierScans)
    if (!has(held,code) && has(next,code)) push({0,code,flags});
  for (uint8_t code = 1; code < 128; ++code)
    if (code != 0x36 && code != 0x1d && code != 0x38 && !has(held,code) && has(next,code))
      push({uint8_t(code == scan && ascii < 128u ? ascii : 0),code,flags});
  if (repeated) push({uint8_t(ascii < 128u ? ascii : 0),scan,flags});
  memcpy(held,next,sizeof(held));
  return true;
}

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

void CgaText80::reset() {
  memset(shown, 0, sizeof(shown));
  memset(shownCursor, 0, sizeof(shownCursor));
  initialCell = scanCell = 0;
}

uint16_t CgaText80::changes(const uint8_t *source, uint8_t *records,
                            uint16_t maximum, uint16_t cursor,
                            bool cursorVisible) {
  if (!source || !records || !maximum) return 0;
  uint16_t count = 0;
  const bool initial = !initialComplete();
  const uint16_t cursorCell = uint16_t(cursor / 2u);
  for (uint16_t checked = 0; checked < CgaTextCells && count < maximum; ++checked) {
    if (initial && initialComplete()) break;
    const uint16_t cell = initial ? initialCell++ : scanCell;
    if (!initial) scanCell = uint16_t((scanCell + 1u) % CgaTextCells);
    const uint16_t offset = uint16_t(cell * 4u);
    uint8_t cursorBits = 0;
    if (cursorVisible && cursorCell == cell) cursorBits = cursor & 1u ? 2u : 1u;
    if (!initial && !memcmp(shown + offset, source + offset, 4u) &&
        shownCursor[cell] == cursorBits) continue;
    const uint16_t record = uint16_t(count * sizeof(TextPair));
    records[record] = uint8_t(cell);
    records[record + 1u] = uint8_t(cell >> 8);
    records[record + 2u] = source[offset];
    records[record + 3u] = source[offset + 2u];
    records[record + 4u] = cursorBits;
    memcpy(shown + offset, source + offset, 4u);
    shownCursor[cell] = cursorBits;
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
