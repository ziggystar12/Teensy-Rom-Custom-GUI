#ifndef MPE_DOOM_VIDEO_H
#define MPE_DOOM_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#ifndef MPE_DOOM_CODE
#define MPE_DOOM_CODE
#endif

namespace mpe_doom {

enum class StageResult : uint8_t {
  Accepted = 0,
  DroppedBusy,
  InvalidArgument
};

// Converts one complete 320x200 indexed Doom framebuffer into the C64
// multicolor-bitmap cell format used by the existing MPE packet transport.
// stageFrame() owns the converted target when it returns, so the engine may
// immediately reuse its framebuffer and palette. A target remains immutable
// until all of its changed cells have been consumed.
class Video {
 public:
  static constexpr uint16_t SourceWidth = 320;
  static constexpr uint16_t SourceHeight = 200;
  static constexpr uint16_t LogicalWidth = 160;
  static constexpr uint16_t Cells = 40u * 25u;
  static constexpr uint8_t RecordBytes = 12;
  static constexpr uint8_t CellBytes = 10;
  static constexpr size_t SourceBytes = size_t(SourceWidth) * SourceHeight;
  static constexpr size_t PaletteBytes = 256u * 3u;
  static constexpr size_t FrameBytes = size_t(Cells) * CellBytes;
  static constexpr size_t DirtyBytes = (Cells + 7u) / 8u;
  static constexpr size_t CellSamples = 4u * 8u;
  static constexpr size_t VicColors = 16u;
  static constexpr size_t ScratchWords =
      CellSamples * VicColors + CellSamples;
  static constexpr size_t ScratchBytes = ScratchWords * sizeof(uint32_t);
  static constexpr size_t PublishedBytes = FrameBytes * 2u + DirtyBytes;
  // Three bytes cover the worst-case padding needed to align the uint32_t
  // search scratch even when the caller supplies a byte-aligned arena.
  static constexpr size_t WorkspaceBytes =
      PublishedBytes + (alignof(uint32_t) - 1u) + ScratchBytes;
  static constexpr uint8_t BackgroundColor = 0;

  MPE_DOOM_CODE bool start(void *workspace, size_t bytes);
  MPE_DOOM_CODE void reset();

  // paletteRgb contains 256 consecutive RGB triples. Every adjacent pair of
  // source pixels contributes equally to one logical output pixel. A frame
  // submitted while an earlier target is pending is deliberately dropped;
  // callers can continue simulation and submit the newest frame later.
  MPE_DOOM_CODE StageResult stageFrame(const uint8_t *framebuffer,
                                       size_t framebufferBytes,
                                       const uint8_t *paletteRgb,
                                       size_t paletteBytes);

  // Emits at most maximum 12-byte MPE cell records. Call again only after the
  // previous batch has been accepted by the transport. Records are committed
  // to the local shown-state as they are returned.
  MPE_DOOM_CODE uint16_t changes(uint8_t *records, uint16_t maximum);

  bool busy() const { return pendingCells_ != 0; }
  bool initialComplete() const { return initialComplete_; }
  uint16_t pendingCells() const { return pendingCells_; }
  uint32_t acceptedFrames() const { return acceptedFrames_; }
  uint32_t droppedFrames() const { return droppedFrames_; }
#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
  uint32_t searchSampleEvaluations() const {
    return searchSampleEvaluations_;
  }
#endif

 private:
  uint8_t *target_ = nullptr;
  uint8_t *shown_ = nullptr;
  uint8_t *dirty_ = nullptr;
  uint32_t *distanceScratch_ = nullptr;
  uint32_t *sampleRgbScratch_ = nullptr;
  uint16_t pendingCells_ = 0;
  uint16_t cursor_ = 0;
  uint32_t acceptedFrames_ = 0;
  uint32_t droppedFrames_ = 0;
  bool initialComplete_ = false;
#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
  mutable uint32_t searchSampleEvaluations_ = 0;
#endif

  MPE_DOOM_CODE void renderCell(const uint8_t *framebuffer,
                                const uint8_t *paletteRgb,
                                uint16_t cell, uint8_t out[CellBytes]) const;
};

// Sixteen RGB triples, indexed by the VIC-II color number used in records.
MPE_DOOM_CODE const uint8_t *vicPaletteRgb();

}  // namespace mpe_doom

#endif
