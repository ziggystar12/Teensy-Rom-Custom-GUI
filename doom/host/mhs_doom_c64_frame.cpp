// SPDX-License-Identifier: MIT

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "mpe_doom_video.h"

namespace {

bool readExact(const char *path, std::vector<uint8_t> *bytes,
               size_t expected) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input || input.tellg() != static_cast<std::streamoff>(expected))
    return false;
  bytes->resize(expected);
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes->data()),
             static_cast<std::streamsize>(expected));
  return input.good();
}

int fail(const char *check) {
  std::cout << "{\"status\":\"FAIL\",\"check\":\"" << check << "\"}\n";
  return 1;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: mhs-doom-c64-frame INDEXED-FRAME RGB24-PALETTE OUTPUT-PPM\n";
    return 64;
  }

  std::vector<uint8_t> framebuffer;
  std::vector<uint8_t> palette;
  if (!readExact(argv[1], &framebuffer, mpe_doom::Video::SourceBytes))
    return fail("frame-input");
  if (!readExact(argv[2], &palette, mpe_doom::Video::PaletteBytes))
    return fail("palette-input");

  std::vector<uint8_t> workspace(mpe_doom::Video::WorkspaceBytes);
  mpe_doom::Video video;
  if (!video.start(workspace.data(), workspace.size()))
    return fail("workspace");
  if (video.stageFrame(framebuffer.data(), framebuffer.size(), palette.data(),
                       palette.size()) != mpe_doom::StageResult::Accepted)
    return fail("stage");

  std::array<uint8_t,
             mpe_doom::Video::Cells * mpe_doom::Video::CellBytes>
      cells{};
  std::array<uint8_t, mpe_doom::Video::Cells> seen{};
  std::array<uint8_t, 19u * mpe_doom::Video::RecordBytes> records{};
  uint16_t recordCount = 0;
  uint16_t packetCount = 0;
  while (video.busy()) {
    const uint16_t count = video.changes(records.data(), 19);
    if (!count || count > 19) return fail("bounded-records");
    ++packetCount;
    for (uint16_t record = 0; record != count; ++record) {
      const uint8_t *source =
          records.data() + size_t(record) * mpe_doom::Video::RecordBytes;
      const uint16_t cell =
          uint16_t(source[0] | (uint16_t(source[1]) << 8u));
      if (cell >= mpe_doom::Video::Cells || seen[cell])
        return fail("unique-cells");
      seen[cell] = 1;
      std::copy(source + 2, source + mpe_doom::Video::RecordBytes,
                cells.begin() + size_t(cell) * mpe_doom::Video::CellBytes);
      ++recordCount;
    }
  }
  if (recordCount != mpe_doom::Video::Cells || packetCount != 53 ||
      !video.initialComplete())
    return fail("complete-frame");

  std::ofstream output(argv[3], std::ios::binary);
  if (!output) return fail("output-open");
  output << "P6\n320 200\n255\n";
  const uint8_t *vic = mpe_doom::vicPaletteRgb();
  for (uint16_t y = 0; y != 200; ++y) {
    for (uint16_t logicalX = 0; logicalX != 160; ++logicalX) {
      const uint16_t cell =
          uint16_t((y / 8u) * 40u + logicalX / 4u);
      const uint8_t *data =
          cells.data() + size_t(cell) * mpe_doom::Video::CellBytes;
      const uint8_t slot = uint8_t(
          (data[y & 7u] >> (6u - 2u * (logicalX & 3u))) & 3u);
      const uint8_t color =
          slot == 0u ? 0u
                     : slot == 1u ? uint8_t(data[8] >> 4u)
                                  : slot == 2u ? uint8_t(data[8] & 15u)
                                               : uint8_t(data[9] & 15u);
      output.write(reinterpret_cast<const char *>(vic + size_t(color) * 3u),
                   3);
      output.write(reinterpret_cast<const char *>(vic + size_t(color) * 3u),
                   3);
    }
  }
  output.close();
  if (!output) return fail("output-write");

  std::cout << "{\"status\":\"PASS\",\"source\":\"320x200 indexed\","
               "\"logical\":\"160x200 VIC-II multicolor\","
               "\"preview\":\"320x200 pixel-aspect corrected\","
               "\"records\":"
            << recordCount << ",\"packets\":" << packetCount
            << ",\"recordBytes\":" << unsigned(mpe_doom::Video::RecordBytes)
#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
            << ",\"searchSampleEvaluations\":"
            << video.searchSampleEvaluations()
#endif
            << "}\n";
  return 0;
}
