#include "mpe_doom_video.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using mpe_doom::StageResult;
using mpe_doom::Video;

void check(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

std::array<uint8_t, Video::PaletteBytes> vicPalette() {
  std::array<uint8_t, Video::PaletteBytes> result{};
  std::copy_n(mpe_doom::vicPaletteRgb(), 16u * 3u, result.begin());
  return result;
}

struct Fixture {
  static constexpr size_t GuardBytes = 32;
  std::vector<uint8_t> workspace =
      std::vector<uint8_t>(Video::WorkspaceBytes + GuardBytes * 2u, 0xa5);
  Video video;

  Fixture() {
    check(video.start(workspace.data() + GuardBytes, Video::WorkspaceBytes),
          "Doom video workspace rejected");
  }

  void guards() const {
    check(std::all_of(workspace.begin(), workspace.begin() + GuardBytes,
                      [](uint8_t byte) { return byte == 0xa5; }) &&
              std::all_of(workspace.end() - GuardBytes, workspace.end(),
                          [](uint8_t byte) { return byte == 0xa5; }),
          "Doom video touched workspace guards");
  }
};

uint16_t recordCell(const uint8_t *record) {
  return uint16_t(record[0] | uint16_t(record[1]) << 8u);
}

void apply(const uint8_t *records, uint16_t count,
           std::vector<uint8_t> &cells) {
  for (uint16_t index = 0; index != count; ++index) {
    const uint8_t *record = records + size_t(index) * Video::RecordBytes;
    const uint16_t cell = recordCell(record);
    check(cell < Video::Cells, "Doom video emitted an out-of-range cell");
    std::copy_n(record + 2, Video::CellBytes,
                cells.begin() + size_t(cell) * Video::CellBytes);
  }
}

uint16_t guardedChanges(Video &video, uint16_t maximum,
                        std::vector<uint8_t> &cells,
                        std::array<bool, Video::Cells> *seen = nullptr) {
  constexpr size_t GuardBytes = 31;
  std::vector<uint8_t> records(size_t(maximum) * Video::RecordBytes + GuardBytes,
                               0xa5);
  const uint16_t count = video.changes(records.data(), maximum);
  check(count <= maximum, "Doom video exceeded the caller record maximum");
  check(std::all_of(records.begin() + size_t(maximum) * Video::RecordBytes,
                    records.end(), [](uint8_t byte) { return byte == 0xa5; }),
        "Doom video wrote beyond the caller record buffer");
  if (seen) {
    for (uint16_t index = 0; index != count; ++index) {
      const uint16_t cell =
          recordCell(records.data() + size_t(index) * Video::RecordBytes);
      check(cell < Video::Cells && !(*seen)[cell],
            "Doom initial traversal repeated a cell");
      (*seen)[cell] = true;
    }
  }
  apply(records.data(), count, cells);
  return count;
}

unsigned drain(Video &video, std::vector<uint8_t> &cells,
               std::array<bool, Video::Cells> *seen = nullptr) {
  static const uint16_t limits[] = {1, 19, 7, 3, 31};
  unsigned total = 0;
  unsigned batch = 0;
  while (video.busy()) {
    const uint16_t count =
        guardedChanges(video, limits[batch++ % std::size(limits)], cells, seen);
    check(count != 0, "Doom dirty traversal stalled");
    total += count;
    check(total <= Video::Cells, "Doom traversal exceeded one frame");
  }
  return total;
}

void setLogicalPixel(std::vector<uint8_t> &frame, uint16_t logicalX,
                     uint16_t y, uint8_t color) {
  const size_t offset = size_t(y) * Video::SourceWidth + logicalX * 2u;
  frame[offset] = frame[offset + 1u] = color;
}

uint8_t pixelSlot(const std::vector<uint8_t> &cells, uint16_t logicalX,
                  uint16_t y) {
  const uint16_t cell = uint16_t((y / 8u) * 40u + logicalX / 4u);
  const uint8_t bitmap = cells[size_t(cell) * Video::CellBytes + y % 8u];
  return uint8_t((bitmap >> (6u - (logicalX % 4u) * 2u)) & 3u);
}

uint8_t pixelColor(const std::vector<uint8_t> &cells, uint16_t logicalX,
                   uint16_t y) {
  const uint16_t cell = uint16_t((y / 8u) * 40u + logicalX / 4u);
  const uint8_t *payload = cells.data() + size_t(cell) * Video::CellBytes;
  const uint8_t slot = pixelSlot(cells, logicalX, y);
  if (!slot) return Video::BackgroundColor;
  if (slot == 1u) return uint8_t(payload[8] >> 4u);
  if (slot == 2u) return uint8_t(payload[8] & 15u);
  return uint8_t(payload[9] & 15u);
}

void verifyBoundsAndInitialFrame() {
  std::vector<uint8_t> shortWorkspace(Video::WorkspaceBytes + 64u, 0xa5);
  Video unbound;
  check(!unbound.start(nullptr, Video::WorkspaceBytes),
        "null Doom workspace was accepted");
  check(!unbound.start(shortWorkspace.data() + 32u, Video::WorkspaceBytes - 1u),
        "undersized Doom workspace was accepted");
  check(std::all_of(shortWorkspace.begin(), shortWorkspace.end(),
                    [](uint8_t byte) { return byte == 0xa5; }),
        "rejected Doom workspace was modified");

  // WorkspaceBytes includes enough alignment slop for the palette-search
  // scratch even when an arena hands us a deliberately unaligned byte span.
  constexpr size_t AlignmentGuard = 32;
  std::vector<uint8_t> unalignedStorage(
      Video::WorkspaceBytes + AlignmentGuard * 2u + alignof(uint32_t), 0xa5);
  size_t unalignedOffset = AlignmentGuard;
  while ((reinterpret_cast<uintptr_t>(unalignedStorage.data() +
                                      unalignedOffset) &
          (alignof(uint32_t) - 1u)) == 0)
    ++unalignedOffset;
  Video unaligned;
  check(unaligned.start(unalignedStorage.data() + unalignedOffset,
                        Video::WorkspaceBytes),
        "unaligned Doom workspace was rejected");
  std::vector<uint8_t> unalignedFrame(Video::SourceBytes, 0);
  auto unalignedPalette = vicPalette();
  check(unaligned.stageFrame(unalignedFrame.data(), unalignedFrame.size(),
                             unalignedPalette.data(),
                             unalignedPalette.size()) == StageResult::Accepted,
        "unaligned Doom workspace could not stage a frame");
  check(std::all_of(unalignedStorage.begin(),
                    unalignedStorage.begin() + unalignedOffset,
                    [](uint8_t byte) { return byte == 0xa5; }) &&
            std::all_of(unalignedStorage.begin() + unalignedOffset +
                            Video::WorkspaceBytes,
                        unalignedStorage.end(),
                        [](uint8_t byte) { return byte == 0xa5; }),
        "Doom video touched unaligned workspace guards");

  Fixture detached;
  std::vector<uint8_t> detachFrame(Video::SourceBytes, 1);
  auto detachPalette = vicPalette();
  check(detached.video.stageFrame(detachFrame.data(), detachFrame.size(),
                                  detachPalette.data(), detachPalette.size()) ==
            StageResult::Accepted,
        "Doom detach fixture initial stage failed");
  const std::vector<uint8_t> releasedWorkspace = detached.workspace;
  check(!detached.video.start(nullptr, Video::WorkspaceBytes) &&
            !detached.video.busy() && !detached.video.initialComplete() &&
            detached.video.acceptedFrames() == 0 &&
            detached.video.droppedFrames() == 0,
        "failed Doom rebind retained publication state");
  detached.video.reset();
  check(detached.video.stageFrame(detachFrame.data(), detachFrame.size(),
                                  detachPalette.data(), detachPalette.size()) ==
            StageResult::InvalidArgument,
        "failed Doom rebind retained its old workspace");
  check(detached.workspace == releasedWorkspace,
        "detached Doom video touched its released workspace");
  detached.guards();

  Fixture fixture;
  std::vector<uint8_t> frame(Video::SourceBytes, 0);
  auto palette = vicPalette();
  check(fixture.video.stageFrame(nullptr, frame.size(), palette.data(),
                                 palette.size()) ==
            StageResult::InvalidArgument &&
            fixture.video.stageFrame(frame.data(), frame.size() - 1u,
                                     palette.data(), palette.size()) ==
                StageResult::InvalidArgument &&
            fixture.video.stageFrame(frame.data(), frame.size(), nullptr,
                                     palette.size()) ==
                StageResult::InvalidArgument &&
            fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                     palette.size() - 1u) ==
                StageResult::InvalidArgument,
        "invalid Doom source bounds were accepted");
  check(fixture.video.acceptedFrames() == 0 &&
            fixture.video.droppedFrames() == 0 && !fixture.video.busy(),
        "invalid Doom source changed publication state");

  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted,
        "initial Doom frame was rejected");
  check(fixture.video.pendingCells() == Video::Cells &&
            !fixture.video.initialComplete(),
        "initial Doom frame did not require all 1000 unique cells");
  const uint16_t pending = fixture.video.pendingCells();
  uint8_t ignored[Video::RecordBytes]{};
  check(fixture.video.changes(nullptr, 19) == 0 &&
            fixture.video.changes(ignored, 0) == 0 &&
            fixture.video.pendingCells() == pending,
        "invalid record request consumed the staged target");

  // The source can be reused as soon as staging returns. Published cells must
  // still describe the original all-black target.
  std::fill(frame.begin(), frame.end(), 1);
  std::fill(palette.begin(), palette.end(), 0xff);
  std::vector<uint8_t> cells(Video::FrameBytes, 0xcc);
  std::array<bool, Video::Cells> seen{};
  check(drain(fixture.video, cells, &seen) == Video::Cells &&
            std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }),
        "initial Doom frame did not visit every cell exactly once");
  for (uint16_t cell = 0; cell != Video::Cells; ++cell) {
    const uint8_t *payload = cells.data() + size_t(cell) * Video::CellBytes;
    check(std::all_of(payload, payload + 8,
                      [](uint8_t byte) { return byte == 0; }) &&
              payload[8] == 0x12 && payload[9] == 3,
          "staged Doom target changed with the reused source buffer");
  }
  check(fixture.video.initialComplete() &&
            fixture.video.acceptedFrames() == 1 &&
            fixture.video.droppedFrames() == 0,
        "initial Doom publication counters are wrong");
  fixture.guards();
  std::cout << "Initial frame PASS: guarded bounds, immutable staging, 1000 "
               "unique bounded records.\n";
}

void verifyDirtyAndDroppedFrames() {
  Fixture fixture;
  std::vector<uint8_t> frame(Video::SourceBytes, 0);
  auto palette = vicPalette();
  std::vector<uint8_t> cells(Video::FrameBytes, 0);
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted,
        "dirty fixture initial stage failed");
  check(drain(fixture.video, cells) == Video::Cells,
        "dirty fixture initial drain failed");

  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted &&
            !fixture.video.busy() && fixture.video.changes(nullptr, 19) == 0,
        "identical Doom frame produced dirty cells");

  // One cell uses black plus three exact VIC colors. The globally optimal
  // zero-error palette is therefore exactly 2,3,7 and packs as 00/01/10/11.
  for (uint16_t y = 0; y != 8; ++y) {
    setLogicalPixel(frame, 0, y, 0);
    setLogicalPixel(frame, 1, y, 2);
    setLogicalPixel(frame, 2, y, 3);
    setLogicalPixel(frame, 3, y, 7);
  }
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted &&
            fixture.video.pendingCells() == 1,
        "single changed Doom cell was not isolated");

  std::vector<uint8_t> newer = frame;
  for (uint16_t y = 0; y != 8; ++y)
    for (uint16_t x = 0; x != 4; ++x) setLogicalPixel(newer, x, y, 1);
  check(fixture.video.stageFrame(newer.data(), newer.size(), palette.data(),
                                 palette.size()) == StageResult::DroppedBusy &&
            fixture.video.droppedFrames() == 1 &&
            fixture.video.pendingCells() == 1,
        "busy Doom target was not dropped atomically");
  frame = newer;

  check(guardedChanges(fixture.video, 1, cells) == 1 && !fixture.video.busy(),
        "single Doom dirty record did not drain");
  const uint8_t *first = cells.data();
  check(std::all_of(first, first + 8,
                    [](uint8_t byte) { return byte == 0x1b; }) &&
            first[8] == 0x23 && first[9] == 7,
        "dropped frame replaced or corrupted the immutable staged cell");

  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted &&
            fixture.video.pendingCells() == 1 &&
            guardedChanges(fixture.video, 1, cells) == 1,
        "newest Doom frame was not accepted after drain");
  check(std::all_of(cells.begin(), cells.begin() + 8,
                    [](uint8_t byte) { return byte == 0x55; }) &&
            cells[8] == 0x12 && cells[9] == 3,
        "post-drop white Doom target was encoded incorrectly");
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted &&
            !fixture.video.busy() && fixture.video.acceptedFrames() == 5 &&
            fixture.video.droppedFrames() == 1,
        "Doom dirty/drop accounting is wrong");
  fixture.guards();
  std::cout << "Dirty frames PASS: unchanged suppression, one-cell delta, "
               "busy drop, immutable target and latest retry.\n";
}

void verifyHorizontalSquish() {
  Fixture fixture;
  std::vector<uint8_t> frame(Video::SourceBytes, 0);
  auto palette = vicPalette();
  std::vector<uint8_t> cells(Video::FrameBytes, 0);
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted,
        "squish fixture initial stage failed");
  drain(fixture.video, cells);

  // Every source pair is black on the left and white on the right. Their RGB
  // midpoint is 128 gray, whose nearest VIC color is medium gray (12). A
  // left- or right-only sampler would instead produce black or white.
  for (uint16_t y = 0; y != 8; ++y) {
    for (uint16_t logicalX = 4; logicalX != 8; ++logicalX) {
      const size_t offset = size_t(y) * Video::SourceWidth + logicalX * 2u;
      frame[offset] = 0;
      frame[offset + 1u] = 1;
    }
  }
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted &&
            fixture.video.pendingCells() == 1,
        "paired-pixel squish did not isolate its cell");
  check(guardedChanges(fixture.video, 19, cells) == 1,
        "paired-pixel squish did not emit one cell");
  for (uint16_t y = 0; y != 8; ++y)
    for (uint16_t x = 4; x != 8; ++x)
      check(pixelColor(cells, x, y) == 12,
            "horizontal squish discarded one source pixel");
  fixture.guards();
  std::cout << "Horizontal squish PASS: all 320 columns contribute to the "
               "160-pixel multicolor frame.\n";
}

uint32_t vicDistance(uint8_t left, uint8_t right) {
  const uint8_t *palette = mpe_doom::vicPaletteRgb();
  uint32_t total = 0;
  for (uint8_t channel = 0; channel != 3; ++channel) {
    const int32_t delta = int32_t(palette[size_t(left) * 3u + channel]) -
                          palette[size_t(right) * 3u + channel];
    total += uint32_t(delta * delta);
  }
  return total;
}

std::array<uint8_t, 3> optimalColors(const std::array<uint8_t, 32> &source) {
  uint32_t bestCost = UINT32_MAX;
  std::array<uint8_t, 3> best{1, 2, 3};
  for (uint8_t first = 1; first <= 13; ++first) {
    for (uint8_t second = uint8_t(first + 1u); second <= 14; ++second) {
      for (uint8_t third = uint8_t(second + 1u); third <= 15; ++third) {
        uint32_t cost = 0;
        for (uint8_t color : source) {
          uint32_t closest = vicDistance(color, Video::BackgroundColor);
          closest = std::min(closest, vicDistance(color, first));
          closest = std::min(closest, vicDistance(color, second));
          closest = std::min(closest, vicDistance(color, third));
          cost += closest;
        }
        if (cost < bestCost) {
          bestCost = cost;
          best = {first, second, third};
        }
      }
    }
  }
  return best;
}

void verifyOptimalReduction() {
  Fixture fixture;
  std::vector<uint8_t> frame(Video::SourceBytes, 0);
  auto palette = vicPalette();
  std::vector<uint8_t> cells(Video::FrameBytes, 0);
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted,
        "optimal-palette fixture initial stage failed");
  drain(fixture.video, cells);

  constexpr uint16_t cell = 2;
  constexpr uint16_t logicalBase = (cell % 40u) * 4u;
  const uint8_t choices[4] = {2, 3, 7, 14};
  std::array<uint8_t, 32> source{};
  uint8_t at = 0;
  for (uint16_t y = 0; y != 8; ++y) {
    for (uint16_t x = 0; x != 4; ++x, ++at) {
      source[at] = choices[at % std::size(choices)];
      setLogicalPixel(frame, uint16_t(logicalBase + x), y, source[at]);
    }
  }
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted &&
            fixture.video.pendingCells() == 1 &&
            guardedChanges(fixture.video, 19, cells) == 1,
        "four-color reduction did not emit its one changed cell");

  const uint8_t *payload = cells.data() + size_t(cell) * Video::CellBytes;
  const std::array<uint8_t, 3> selected{
      uint8_t(payload[8] >> 4u), uint8_t(payload[8] & 15u),
      uint8_t(payload[9] & 15u)};
  check(selected == optimalColors(source),
        "Doom cell palette is not the deterministic global minimum");
  check(std::all_of(choices, choices + std::size(choices),
                    [&](uint8_t color) {
                      return std::find(selected.begin(), selected.end(), color) !=
                             selected.end();
                    }) == false,
        "four source colors unexpectedly fit three VIC color slots");

  at = 0;
  for (uint16_t y = 0; y != 8; ++y) {
    for (uint16_t x = 0; x != 4; ++x, ++at) {
      uint8_t expectedSlot = 0;
      uint32_t closest = vicDistance(source[at], Video::BackgroundColor);
      for (uint8_t candidate = 0; candidate != selected.size(); ++candidate) {
        const uint32_t distance = vicDistance(source[at], selected[candidate]);
        if (distance < closest) {
          closest = distance;
          expectedSlot = uint8_t(candidate + 1u);
        }
      }
      check(pixelSlot(cells, uint16_t(logicalBase + x), y) == expectedSlot,
            "Doom pixel did not choose its deterministic nearest cell color");
    }
  }
  fixture.guards();
  std::cout << "Palette reduction PASS: exhaustive best-three selection and "
               "deterministic nearest-color packing.\n";
}

std::array<uint8_t, Video::PaletteBytes> proofPalette() {
  std::array<uint8_t, Video::PaletteBytes> palette{};
  for (uint16_t index = 0; index != 256; ++index) {
    palette[index * 3u] = uint8_t(((index >> 5u) & 7u) * 255u / 7u);
    palette[index * 3u + 1u] = uint8_t(((index >> 2u) & 7u) * 255u / 7u);
    palette[index * 3u + 2u] = uint8_t((index & 3u) * 255u / 3u);
  }
  return palette;
}

void saveProof(const std::string &path) {
  Fixture fixture;
  auto palette = proofPalette();
  std::vector<uint8_t> frame(Video::SourceBytes);
  for (uint16_t y = 0; y != Video::SourceHeight; ++y) {
    for (uint16_t x = 0; x != Video::SourceWidth; ++x) {
      const uint8_t red = uint8_t(x * 7u / (Video::SourceWidth - 1u));
      const uint8_t green = uint8_t(y * 7u / (Video::SourceHeight - 1u));
      const uint8_t blue = uint8_t(((x / 20u) ^ (y / 20u)) & 3u);
      frame[size_t(y) * Video::SourceWidth + x] =
          uint8_t((red << 5u) | (green << 2u) | blue);
    }
  }
  std::vector<uint8_t> cells(Video::FrameBytes, 0);
  check(fixture.video.stageFrame(frame.data(), frame.size(), palette.data(),
                                 palette.size()) == StageResult::Accepted &&
            drain(fixture.video, cells) == Video::Cells,
        "proof frame did not complete");
#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
  constexpr uint32_t exhaustiveSamples =
      uint32_t(Video::Cells) * 455u * uint32_t(Video::CellSamples);
  const uint32_t evaluated = fixture.video.searchSampleEvaluations();
  check(evaluated < exhaustiveSamples,
        "Doom palette search did not prune any candidate samples");
  std::cout << "Palette search PASS: " << evaluated << " of "
            << exhaustiveSamples << " exhaustive candidate-sample "
               "evaluations ("
            << (100u * evaluated / exhaustiveSamples) << "%).\n";
#endif

  std::ofstream ppm(path, std::ios::binary);
  check(bool(ppm), "unable to create Doom video PPM proof");
  ppm << "P6\n320 200\n255\n";
  const uint8_t *vic = mpe_doom::vicPaletteRgb();
  for (uint16_t y = 0; y != Video::SourceHeight; ++y) {
    for (uint16_t x = 0; x != Video::SourceWidth; ++x) {
      const uint8_t color = pixelColor(cells, uint16_t(x / 2u), y);
      ppm.write(reinterpret_cast<const char *>(vic + size_t(color) * 3u), 3);
    }
  }
  check(bool(ppm), "unable to finish Doom video PPM proof");
  fixture.guards();
}

}  // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: mpe_doom_video_test OUTPUT.ppm");
    verifyBoundsAndInitialFrame();
    verifyDirtyAndDroppedFrames();
    verifyHorizontalSquish();
    verifyOptimalReduction();
    saveProof(argv[1]);
    std::cout << "Doom video PASS: fixed-black VIC-II multicolor conversion, "
                 "deterministic per-cell palettes and PPM proof "
              << argv[1] << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Doom video FAILED: " << error.what() << '\n';
    return 1;
  }
}
