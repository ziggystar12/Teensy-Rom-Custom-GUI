// Exercise the actual text batcher with firmware-sized packets. The hardware
// stubs also let MPE5Start prove that an unreadable cartridge header is
// rejected before opening SD files or accessing guest memory.
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#define FLASHMEM
#define DMAMEM
#define PROGMEM
#include "../../engine/native-runtime/mhs_native_arena.h"

static unsigned cartridgeReads, sdOpens, publications;
static uint8_t publishedType, publishedFlags, publishedLength;
static constexpr uint32_t RAM_ImageSize = 240u * 1024u;
static constexpr uint16_t MAX_CRT_CHIPS = 128;
struct StructCrtChip {
  uint8_t *ChipROM;
  uint16_t LoadAddress, ROMSize, BankNum;
};
uint8_t RAM_Image[RAM_ImageSize];
uint8_t NumCrtChips;
StructCrtChip CrtChips[MAX_CRT_CHIPS];
static uint8_t *BankDecode[64][2];
static constexpr uint8_t NumDecodeBanks = 64;
static constexpr uint8_t Num8kSwapBuffers = 16;
static struct { uint8_t Image[8192]; uint32_t Offset; }
  SwapBuffers[Num8kSwapBuffers];
static struct { uint32_t pages[512]; bool native; } MPE4CrtDirectory;
static constexpr uint8_t DMA_S_DisableReady = 0;
static uint8_t DMA_State, AGIPicPendingCommand;
static bool MPE3TitleOwned = true;
static bool AGIPicActive, AGIPicResetPending, AGIPicAbortRequested, MPEThinUpgradePending;
static bool MPE3TitleSelected() { return true; }
struct File {
  explicit operator bool() const { return false; }
  bool isOpen() const { return false; }
  uint32_t size() const { return 0; }
  uint64_t fileSize() const { return 0; }
  bool seek(uint32_t) { return false; }
  bool seekSet(uint64_t) { return false; }
  int read(uint8_t *, uint32_t) { return 0; }
  size_t write(const uint8_t *, size_t) { return 0; }
  void flush() {}
  void close() {}
};
using FsFile = File;
struct SdfsStub {
  FsFile open(const char *, int) { ++sdOpens; return {}; }
};
struct SdStub {
  SdfsStub sdfs;
  File open(const char *, int) { ++sdOpens; return {}; }
} SD;
static constexpr int FILE_READ = 0, FILE_WRITE_BEGIN = 2, O_RDONLY = FILE_READ;
static File myFile;
static uint8_t *BigBuf = nullptr;
static uint32_t BigBufCount = 0;
alignas(32) static uint8_t MPE5HostRam2[512u * 1024u];
#define MPE5_RAM2_BASE MPE5HostRam2
static constexpr uint8_t MPE3TitlePacketHeaderBytes = 8;
static constexpr uint8_t MPE3TitleCellBytes = 12;
static constexpr uint8_t MPE3TitleCellsPerPacket = 19;
static constexpr uint8_t MPE3TitleCellHires = 4;
static constexpr uint8_t MPE3TitleCellModeValid = 8;
static constexpr uint8_t MPE3TitleCellReplace = 16;
static constexpr uint32_t MPE3TitleInternalAssetBytes = 65536;
static constexpr uint8_t MPE3TitleErrorHeader = 2;
static constexpr uint8_t MPE3TitleErrorMemory = 4;
static constexpr uint8_t MPE3TitleErrorRead = 5;
static constexpr uint8_t MPE3TitleFinished = 5;
static constexpr uint8_t MPE3TitleCELL = 1;
static constexpr uint8_t MPE3TitleSID = 2;
static constexpr uint8_t MPE3TitleRegACK = 0xf6;
static uint8_t MPE3TitleInternalAssets[MPE3TitleInternalAssetBytes];
static uint8_t MPE3TitlePacket[240], MPE3TitleMailbox[256];
static uint32_t millis() { return 0; }
static struct { bool Loaded, Pending; uint8_t Phase, Sequence; } MPE3Title;
static void MPE3TitleMemoryBarrier() {}
static void MPE3TitlePublish(uint8_t type, uint8_t flags, uint8_t length) {
  ++publications;
  publishedType = type; publishedFlags = flags; publishedLength = length;
}
static void MPE3TitleFail(uint8_t) {}
static bool MPE4Read(void *, uint32_t, uint8_t *, uint16_t) {
  ++cartridgeReads;
  return false;
}
static uint32_t MHSNativeCRC32(const uint8_t *, uint32_t) { return 0; }

#include "../../engine/native-dos/mpe5_firmware.h"

namespace {

using Screen = std::array<uint8_t, mpe5::CgaTextCells * 2u>;
using Coverage = std::array<uint16_t, mpe5::CgaTextCells>;
using Records = std::array<uint8_t,
    MPE3TitleCellsPerPacket * sizeof(mpe5::TextCell)>;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

uint16_t recordCell(const Records &records, uint16_t record) {
  const unsigned offset = record * sizeof(mpe5::TextCell);
  return uint16_t(records[offset] | uint16_t(records[offset + 1u]) << 8u);
}

void initialFrame(bool editEarlyCells) {
  mpe5::CgaText text;
  Screen source{};
  Coverage seen{};
  Records records{};
  uint16_t total = 0, packets = 0;
  require(!text.initialComplete(), "fresh text cache is already complete");
  require(text.changes(nullptr, records.data(), 19) == 0 &&
          text.changes(source.data(), nullptr, 19) == 0 &&
          text.changes(source.data(), records.data(), 0) == 0 &&
          !text.initialComplete(), "empty request advanced initial coverage");
  while (!text.initialComplete()) {
    if (editEarlyCells && packets) source[0] = uint8_t('A' + packets % 26u);
    const uint16_t count = text.changes(source.data(), records.data(), 19);
    require(count > 0 && count <= 19, "initial packet is empty or oversized");
    ++packets;
    for (uint16_t index = 0; index < count; ++index) {
      const uint16_t cell = recordCell(records, index);
      require(cell < seen.size(), "initial cell outside screen");
      require(++seen[cell] == 1, "initial frame repeated a cell");
      const unsigned record = index * sizeof(mpe5::TextCell);
      require(records[record + 2u] == source[cell * 2u],
              "initial glyph differs from current source");
    }
    total += count;
    require(text.initialComplete() == (total == mpe5::CgaTextCells),
            "initial completion does not match unique cell coverage");
    require(packets <= 53, "initial full frame did not make bounded progress");
  }
  require(total == 1000 && packets == 53,
          "initial full frame must contain 1,000 cells in 53 packets");
  for (uint16_t count : seen) require(count == 1, "initial frame missed a cell");

  const uint16_t dirty = text.changes(source.data(), records.data(), 19);
  if (editEarlyCells) {
    require(dirty == 1 && recordCell(records, 0) == 0 &&
            records[2] == source[0],
            "dirty sweep lost early-cell edits made during initialization");
  } else require(dirty == 0, "unchanged all-zero screen emitted dirty cells");

  // Starting a new session must cover blank cells again, regardless of the
  // old screen contents or the dirty scan's last position.
  text.reset();
  source.fill(0);
  require(!text.initialComplete() &&
          text.changes(source.data(), records.data(), 1) == 1 &&
          recordCell(records, 0) == 0 && !text.initialComplete(),
          "reset did not restart complete initial coverage");
}

void dirtySweepFairness() {
  mpe5::CgaText text;
  Screen source{};
  Records records{};
  while (!text.initialComplete()) text.changes(source.data(), records.data(), 19);
  // Every cell changes, while the earliest packet's cells keep changing.
  // The last cell must still arrive within one 53-packet dirty sweep.
  for (uint16_t cell = 0; cell < mpe5::CgaTextCells; ++cell) source[cell * 2u] = 'X';
  bool lastSeen = false;
  for (uint16_t packet = 0; packet < 53 && !lastSeen; ++packet) {
    for (uint16_t cell = 0; cell < 19; ++cell)
      source[cell * 2u] = uint8_t('A' + packet % 26u);
    const uint16_t count = text.changes(source.data(), records.data(), 19);
    require(count <= 19, "dirty packet exceeds transport cell limit");
    for (uint16_t index = 0; index < count; ++index)
      if (recordCell(records, index) == mpe5::CgaTextCells - 1u) lastSeen = true;
  }
  require(lastSeen, "frequent early edits starved the last text cell");
}

void rejectedHeader() {
  std::fill(std::begin(RAM_Image), std::end(RAM_Image), uint8_t(0xa5));
  memset(&MPE5DisplayVideo, 0xa5, sizeof(MPE5DisplayVideo));
  require(!MPE5Start(0), "VM started without a readable cartridge header");
  require(MPE5Error == MPE3TitleErrorHeader && !MPE5Active,
          "unreadable header did not report the header error");
  require(cartridgeReads == 1 && sdOpens == 0,
          "rejected header opened SD or touched guest memory");
  for (uint8_t byte : RAM_Image)
    require(byte == 0xa5, "rejected header changed cartridge RAM");
}

void frameEndMode() {
  MPE5InputActivationPending = true;
  MPE5NextPacket();
  require(publications == 1 && publishedType == MPE3TitleSID &&
          publishedFlags == 0x25 && publishedLength == 27 &&
          !MPE5InputActivationPending,
          "input activation did not publish a hires gameplay frame end");
  for (uint8_t index = 0; index < 27; ++index)
    require(MPE3TitlePacket[MPE3TitlePacketHeaderBytes + index] == 0,
            "frame end contains non-silent SID registers");
}

void blankGlyph() {
  mpe5::CgaText text;
  Screen source{};
  Records records{};
  source[1] = 7; // Cleared CGA cell with a visible foreground attribute.
  require(text.changes(source.data(), records.data(), 1) == 1 &&
          records[2] == 0 && records[3] == 1,
          "NUL source cell did not retain its visible CGA attribute");
  uint8_t bitmap[8];
  MPE5Glyph(records[2], bitmap);
  for (uint8_t row : bitmap)
    require(row == 0, "NUL text cell rendered as a visible question mark");
  // Independent samples from the source font, oriented with the left pixel
  // in bit 7 as required by the VIC-II hires bitmap.
  const uint8_t promptArrow[8] = {0x60,0x30,0x18,0x0c,0x18,0x30,0x60,0};
  MPE5Glyph('>', bitmap);
  require(memcmp(bitmap, promptArrow, sizeof(bitmap)) == 0,
          "DOS prompt arrow points left instead of right");
}

void printableFont(const char *outputPath) {
  std::array<uint8_t, 256u * 8u> font{};
  for (unsigned character = 0; character < 256; ++character)
    MPE5Glyph(uint8_t(character), font.data() + character * 8u);
  const uint8_t *question = font.data() + '?' * 8u;
  for (unsigned character = 33; character <= 126; ++character) {
    const uint8_t *glyph = font.data() + character * 8u;
    bool visible = false;
    for (unsigned row = 0; row < 8; ++row) visible |= glyph[row] != 0;
    require(visible, "printable ASCII character rendered blank");
    if (character != '?')
      require(memcmp(glyph, question, 8) != 0,
              "printable ASCII character fell back to a question mark");
  }
  for (unsigned character = 'a'; character <= 'z'; ++character)
    require(memcmp(font.data() + character * 8u,
                   font.data() + (character - 'a' + 'A') * 8u, 8) != 0,
            "lowercase character was replaced with uppercase");
  const uint8_t comma[8] = {0,0,0,0,0,0x30,0x30,0x60};
  require(memcmp(font.data() + ',' * 8u, comma, 8) == 0,
          "comma does not match the expected descending comma pixels");
  if (outputPath) {
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    require(output.good(), "cannot create verified font atlas");
    output.write(reinterpret_cast<const char *>(font.data()), font.size());
    output.close();
    require(output.good(), "cannot finish verified font atlas");
  }
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc <= 2, "usage: mpe5_text_publication_test [font-output.bin]");
    initialFrame(false);
    initialFrame(true);
    dirtySweepFairness();
    rejectedHeader();
    frameEndMode();
    blankGlyph();
    printableFont(argc == 2 ? argv[1] : nullptr);
    std::cout << "MPE5 publication regression passed: 1,000 unique initial cells "
                 "in 53 packets, deferred edits, fair dirty sweep, reset, "
                 "safe bad-header rejection, hires frame end, blank NUL, "
                 "all printable ASCII, lowercase, and comma pixels.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "MPE5 publication regression failed: " << error.what() << '\n';
    return 1;
  }
}
