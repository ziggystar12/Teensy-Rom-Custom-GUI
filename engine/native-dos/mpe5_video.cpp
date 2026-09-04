#include "mpe5_video.h"

#include <string.h>

namespace mpe5 {

MPE5_CODE bool CgaVideo::start(void *workspace, size_t bytes) {
  memory_ = nullptr;
  state_ = {};
  sharp_ = false;
  initialCell_ = scanCell_ = 0;
  if (!workspace || bytes < WorkspaceBytes) return false;
  memory_ = static_cast<uint8_t *>(workspace);
  reset();
  return true;
}

MPE5_CODE void CgaVideo::reset() {
  state_ = {};
  sharp_ = false;
  initialCell_ = scanCell_ = 0;
  if (memory_) memset(memory_, 0, WorkspaceBytes);
}

MPE5_CODE void CgaVideo::write(uint16_t offset, const uint8_t *data, uint16_t length) {
  if (!memory_ || !data || offset >= VramBytes) return;
  if (length > VramBytes - offset) length = VramBytes - offset;
  const uint16_t start = uint16_t(state_.startAddress * 2u) & 8191u;
  for (uint16_t index = 0; index < length; ++index) {
    const uint16_t address = offset + index;
    if (memory_[address] == data[index]) continue;
    memory_[address] = data[index];
    const uint16_t local = uint16_t((address & 8191u) + 8192u - start) & 8191u;
    if (local >= 8000u) continue; // Unshown padding at the end of each bank.
    const uint16_t row = uint16_t((local / 80u) * 2u + (address >> 13));
    const uint16_t cell = uint16_t((row / 8u) * 40u + (local % 80u) / 2u);
    memory_[DirtyOffset + cell / 8u] |= uint8_t(1u << (cell & 7u));
  }
}

MPE5_CODE bool CgaVideo::setState(const VideoState &next) {
  if (state_.mode == next.mode && state_.control == next.control &&
      state_.colorSelect == next.colorSelect && state_.startAddress == next.startAddress &&
      state_.enabled == next.enabled) return false;
  state_ = next;
  initialCell_ = scanCell_ = 0;
  if (memory_) memset(memory_ + DirtyOffset, 0xff, (Cells + 7u) / 8u);
  return true;
}

MPE5_CODE bool CgaVideo::initialComplete() const { return initialCell_ == Cells; }
MPE5_CODE bool CgaVideo::graphics() const { return state_.mode >= 4 && state_.mode <= 6; }
MPE5_CODE bool CgaVideo::hires() const { return state_.mode == 6 || (sharp_ && graphics()); }
MPE5_CODE bool CgaVideo::sharp() const { return sharp_; }

MPE5_CODE bool CgaVideo::setSharp(bool enabled) {
  if (sharp_ == enabled) return false;
  sharp_ = enabled;
  initialCell_ = scanCell_ = 0;
  if (memory_) memset(memory_ + DirtyOffset, 0xff, (Cells + 7u) / 8u);
  return true;
}

MPE5_CODE void CgaVideo::palette(uint8_t colors[4]) const {
  // Closest available VIC-II RGBI colors. CGA palette selection/intensity is
  // defined by IBM's Color/Graphics Adapter mode/color registers (3D8/3D9).
  static const uint8_t c64[16] = {0,6,5,3,2,4,8,15,11,14,13,3,10,4,7,1};
  if (!state_.enabled) { memset(colors, 0, 4); return; }
  if (state_.mode == 6) {
    colors[0] = 0;
    colors[1] = c64[state_.colorSelect & 15u];
    colors[2] = colors[3] = 0;
    return;
  }
  const uint8_t intense = (state_.colorSelect & 0x10u) ? 8u : 0u;
  const bool alternate = (state_.colorSelect & 0x20u) != 0;
  const bool monochrome = state_.mode == 5 || (state_.control & 4u);
  colors[0] = c64[state_.colorSelect & 15u];
  colors[1] = c64[(monochrome || alternate ? 3u : 2u) + intense];
  colors[2] = c64[(monochrome ? 4u : alternate ? 5u : 4u) + intense];
  colors[3] = c64[(monochrome || alternate ? 7u : 6u) + intense];
}

MPE5_CODE uint8_t CgaVideo::background() const {
  uint8_t colors[4]; palette(colors); return colors[0];
}

MPE5_CODE void CgaVideo::render(uint16_t cell, uint8_t out[10]) const {
  uint8_t colors[4]; palette(colors);
  if (sharp_ && state_.mode != 6) { renderSharp(cell, out, colors); return; }
  for (uint16_t line = 0; line < 8; ++line) {
    const uint16_t y = uint16_t((cell / 40u) * 8u + line);
    const uint16_t local = uint16_t(state_.startAddress * 2u + (y / 2u) * 80u + (cell % 40u) * 2u) & 8191u;
    const uint16_t bank = uint16_t((y & 1u) * 8192u);
    const uint8_t left = memory_[bank + local];
    const uint8_t right = memory_[bank + ((local + 1u) & 8191u)];
    uint8_t pixels = 0;
    if (state_.enabled) {
      if (hires()) {
        // 640 -> 320 monochrome: retain either pixel in each horizontal pair,
        // so one-pixel strokes are not discarded by nearest-neighbor sampling.
        for (uint8_t pair = 0; pair < 4; ++pair) {
          if (left & (0xc0u >> (pair * 2u))) pixels |= uint8_t(0x80u >> pair);
          if (right & (0xc0u >> (pair * 2u))) pixels |= uint8_t(0x08u >> pair);
        }
      } else {
        // 320 -> 160 color: choose the higher CGA index in each pair. This
        // retains foreground against index-zero background deterministically.
        const uint16_t source = uint16_t(uint16_t(left) << 8) | right;
        for (uint8_t pair = 0; pair < 4; ++pair) {
          const uint8_t a = uint8_t((source >> (14u - pair * 4u)) & 3u);
          const uint8_t b = uint8_t((source >> (12u - pair * 4u)) & 3u);
          pixels |= uint8_t((a > b ? a : b) << (6u - pair * 2u));
        }
      }
    }
    out[line] = pixels;
  }
  out[8] = hires() ? uint8_t(colors[1] << 4) : uint8_t((colors[1] << 4) | colors[2]);
  out[9] = hires() ? colors[1] : colors[3];
}

MPE5_CODE void CgaVideo::renderSharp(uint16_t cell, uint8_t out[10], const uint8_t colors[4]) const {
  uint16_t source[8];
  uint8_t counts[4] = {}, canonical[4];
  // Some CGA entries map to the same VIC-II color. Count those together so
  // every cell with at most two distinct mapped colors remains exact.
  for (uint8_t index = 0; index < 4; ++index) {
    canonical[index] = index;
    for (uint8_t previous = 0; previous < index; ++previous)
      if (colors[previous] == colors[index]) { canonical[index] = previous; break; }
  }
  for (uint16_t line = 0; line < 8; ++line) {
    const uint16_t y = uint16_t((cell / 40u) * 8u + line);
    const uint16_t local = uint16_t(state_.startAddress * 2u + (y / 2u) * 80u + (cell % 40u) * 2u) & 8191u;
    const uint16_t bank = uint16_t((y & 1u) * 8192u);
    source[line] = uint16_t(uint16_t(memory_[bank + local]) << 8) |
                   memory_[bank + ((local + 1u) & 8191u)];
    for (uint8_t pixel = 0; pixel < 8; ++pixel)
      ++counts[canonical[(source[line] >> (14u - pixel * 2u)) & 3u]];
  }
  uint8_t primary = 0;
  for (uint8_t index = 1; index < 4; ++index)
    if (counts[index] > counts[primary]) primary = index;
  uint8_t secondary = primary;
  for (uint8_t index = 0; index < 4; ++index)
    if (index != primary && counts[index] &&
        (secondary == primary || counts[index] > counts[secondary])) secondary = index;

  // A fixed, compact VIC-II RGB palette gives deterministic nearest-color
  // reduction when a cell uses three or four colors. Ties favor the most
  // frequent color, then the lowest original CGA index; no temporal dithering.
  static const uint8_t rgb[16][3] = {
    {0,0,0},{255,255,255},{136,57,50},{103,182,189},
    {139,63,150},{85,160,73},{64,49,141},{191,206,114},
    {139,84,41},{87,66,0},{184,105,98},{80,80,80},
    {120,120,120},{148,224,137},{120,105,196},{159,159,159}
  };
  uint8_t foreground[4];
  for (uint8_t index = 0; index < 4; ++index) {
    if (!counts[canonical[index]]) { foreground[index] = 0; continue; }
    if (colors[index] == colors[primary]) { foreground[index] = 0; continue; }
    if (colors[index] == colors[secondary]) { foreground[index] = 1; continue; }
    uint32_t primaryDistance = 0, secondaryDistance = 0;
    for (uint8_t channel = 0; channel < 3; ++channel) {
      const int32_t a = int32_t(rgb[colors[index]][channel]) - rgb[colors[primary]][channel];
      const int32_t b = int32_t(rgb[colors[index]][channel]) - rgb[colors[secondary]][channel];
      primaryDistance += uint32_t(a * a); secondaryDistance += uint32_t(b * b);
    }
    foreground[index] = secondaryDistance < primaryDistance;
  }
  for (uint8_t line = 0; line < 8; ++line) {
    uint8_t pixels = 0;
    for (uint8_t pixel = 0; pixel < 8; ++pixel)
      pixels |= uint8_t(foreground[(source[line] >> (14u - pixel * 2u)) & 3u] << (7u - pixel));
    out[line] = pixels;
  }
  out[8] = uint8_t((colors[secondary] << 4) | colors[primary]);
  out[9] = colors[secondary];
}

MPE5_CODE uint16_t CgaVideo::changes(uint8_t *records, uint16_t maximum) {
  if (!memory_ || !records || !maximum || !graphics()) return 0;
  uint16_t count = 0;
  const bool initial = !initialComplete();
  for (uint16_t checked = 0; checked < Cells && count < maximum; ++checked) {
    if (initial && initialComplete()) break;
    const uint16_t cell = initial ? initialCell_++ : scanCell_;
    if (!initial) scanCell_ = uint16_t((scanCell_ + 1u) % Cells);
    uint8_t &dirty = memory_[DirtyOffset + cell / 8u];
    const uint8_t mask = uint8_t(1u << (cell & 7u));
    if (!initial && !(dirty & mask)) continue;
    dirty &= uint8_t(~mask);
    uint8_t rendered[10]; render(cell, rendered);
    uint8_t *shown = memory_ + ShownOffset + cell * 10u;
    if (!initial && !memcmp(shown, rendered, sizeof(rendered))) continue;
    uint8_t *record = records + count * RecordBytes;
    record[0] = uint8_t(cell); record[1] = uint8_t(cell >> 8);
    memcpy(record + 2, rendered, sizeof(rendered));
    memcpy(shown, rendered, sizeof(rendered));
    ++count;
  }
  return count;
}

}  // namespace mpe5
