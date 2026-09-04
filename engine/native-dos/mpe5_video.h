#ifndef MPE5_VIDEO_H
#define MPE5_VIDEO_H

#include "mpe5_platform.h"

namespace mpe5 {

// Mirrored from successful guest BDA/port writes, so asking for the current
// video mode never reads a swapped guest page. startAddress is a CRTC word
// address; the two CGA scanline banks wrap independently at 8 KiB.
struct VideoState {
  uint8_t mode = 0, control = 0, colorSelect = 0;
  uint16_t startAddress = 0;
  bool enabled = false;
};

struct VideoObserver {
  void *context = nullptr;
  void (*write)(void *, uint16_t offset, const uint8_t *, uint16_t length) = nullptr;
};

// The caller supplies storage, normally the BIOS load arena after coreStart
// has copied the BIOS. Successful guest VRAM writes update a private mirror;
// rendering never scans guest pages or creates SD traffic.
class CgaVideo {
 public:
  static constexpr uint16_t VramBytes = 16384;
  static constexpr uint16_t Cells = 1000;
  static constexpr uint8_t RecordBytes = 12;
  static constexpr size_t WorkspaceBytes = VramBytes + Cells * 10u + (Cells + 7u) / 8u;

  MPE5_CODE bool start(void *workspace, size_t bytes);
  MPE5_CODE void reset();
  MPE5_CODE void write(uint16_t offset, const uint8_t *data, uint16_t length);
  // Call before changes or querying initialComplete. A mode, display-start,
  // enable, or palette change starts another complete, unique-cell traversal.
  MPE5_CODE bool setState(const VideoState &next);
  // Presentation preference only: preserve all 320 CGA pixels using the two
  // most frequent colors in each VIC-II hires cell. Guest mode is unchanged.
  MPE5_CODE bool setSharp(bool enabled);
  MPE5_CODE bool sharp() const;
  MPE5_CODE uint16_t changes(uint8_t *records, uint16_t maximum);
  MPE5_CODE bool initialComplete() const;
  MPE5_CODE bool graphics() const;
  MPE5_CODE bool hires() const;
  MPE5_CODE uint8_t background() const;

 private:
  static constexpr size_t ShownOffset = VramBytes;
  static constexpr size_t DirtyOffset = ShownOffset + Cells * 10u;
  uint8_t *memory_ = nullptr;
  VideoState state_{};
  bool sharp_ = false;
  uint16_t initialCell_ = 0, scanCell_ = 0;
  MPE5_CODE void palette(uint8_t colors[4]) const;
  MPE5_CODE void render(uint16_t cell, uint8_t out[10]) const;
  MPE5_CODE void renderSharp(uint16_t cell, uint8_t out[10], const uint8_t colors[4]) const;
};

}  // namespace mpe5
#endif
