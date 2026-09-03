// Host acceptance test for the exact native 8086 adapter used by the MPE5
// firmware.  It boots a supplied FreeDOS image through the same BIOS, sector
// callback, 20-bit memory map, CGA text address, and keyboard queue as the
// Teensy build.  It deliberately has no C64 or EasyFlash dependency.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../engine/native-dos/mpe5_platform.h"
#include "../../engine/native-dos/mpe5_platform.cpp"
#include "../../engine/native-dos/mpe5_8086tiny.cpp"

namespace {

constexpr uint32_t kSliceInstructions = 25000u;
constexpr uint32_t kBootSliceLimit = 20000u;
constexpr uint32_t kCommandSliceLimit = 5000u;

struct Image {
  std::vector<uint8_t> bytes;
  uint32_t reads = 0;
  uint32_t lastLba = 0;
};

std::vector<uint8_t> readFile(const char *path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error(std::string("cannot open ") + path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool readSector(void *context, uint32_t lba,
                uint8_t out[mpe5::SectorBytes]) {
  const auto *image = static_cast<const Image *>(context);
  const uint64_t offset = uint64_t(lba) * mpe5::SectorBytes;
  if (!image || offset + mpe5::SectorBytes > image->bytes.size()) return false;
  auto *tracked = const_cast<Image *>(image);
  ++tracked->reads;
  tracked->lastLba = lba;
  std::copy_n(image->bytes.data() + offset, mpe5::SectorBytes, out);
  return true;
}

std::string textScreen(const std::vector<uint8_t> &memory) {
  std::string screen;
  screen.reserve(mpe5::CgaTextRows * (mpe5::CgaTextColumns + 1u));
  for (uint16_t row = 0; row < mpe5::CgaTextRows; ++row) {
    for (uint16_t column = 0; column < mpe5::CgaTextColumns; ++column) {
      const uint32_t offset = mpe5::NativeTextViewportAddress +
          2u * (uint32_t(row) * mpe5::CgaTextColumns + column);
      const uint8_t character = memory[offset];
      screen.push_back(character >= 32u && character <= 126u ?
                       static_cast<char>(character) : ' ');
    }
    screen.push_back('\n');
  }
  return screen;
}

bool hasText(const std::vector<uint8_t> &memory, const char *text) {
  const size_t length = std::char_traits<char>::length(text);
  for (uint32_t offset = mpe5::NativeTextViewportAddress;
       offset + 2u * length <= mpe5::NativeTextViewportAddress + 2u * mpe5::CgaTextCells; offset += 2u) {
    bool matched = true;
    for (size_t index = 0; index < length; ++index)
      if (memory[offset + 2u * index] != static_cast<uint8_t>(text[index])) {
        matched = false;
        break;
      }
    if (matched) return true;
  }
  return false;
}

bool atPrompt(const std::vector<uint8_t> &memory) {
  static constexpr char prompt[] = "C:\\>";
  if (MPE5TextCursor < 4 || MPE5TextCursor % mpe5::NativeTextColumns != 4) return false;
  for (unsigned index = 0; index < 4; ++index)
    if (memory[mpe5::NativeTextShadowAddress + 2u * (MPE5TextCursor - 4u + index)] != prompt[index]) return false;
  return true;
}

void runUntil(const Image &image, const std::vector<uint8_t> &memory,
              const char *text, uint32_t limit, const char *stage) {
  for (uint32_t slice = 0; slice < limit; ++slice) {
    if (!mpe5::coreRun(kSliceInstructions)) {
      std::cerr << "CGA at halt:\n" << textScreen(memory);
      throw std::runtime_error(std::string("8086 core stopped during ") + stage +
          " after " + std::to_string(slice + 1u) + " execution slices and " +
          std::to_string(image.reads) + " sector reads; last LBA " +
          std::to_string(image.lastLba) + "; CS:IP " +
          std::to_string(regs16[REG_CS]) + ":" + std::to_string(reg_ip));
    }
    if (hasText(memory, text) && (std::string(text) != "C:\\>" || atPrompt(memory))) return;
  }
  std::cerr << "CGA at timeout:\n" << textScreen(memory);
  throw std::runtime_error(std::string("did not reach '") + text + "' during " +
      stage + " after " + std::to_string(image.reads) + " sector reads; last LBA " +
      std::to_string(image.lastLba) + "; CS:IP " +
      std::to_string(regs16[REG_CS]) + ":" + std::to_string(reg_ip));
}

void queue(mpe5::Keyboard &keyboard, const char *text) {
  for (const char *cursor = text; *cursor; ++cursor)
    if (!keyboard.push({static_cast<uint8_t>(*cursor), 0}))
      throw std::runtime_error("keyboard queue unexpectedly filled");
}

void printScreen(const std::vector<uint8_t> &memory) {
  std::cout << "--- CGA 40x25 ---\n" << textScreen(memory)
            << "----------------\n";
}

void verifyConsole(std::vector<uint8_t> &memory) {
  // Exercise the real BIOS-facing renderer without allowing it to scribble
  // into guest VRAM, console readback, or BIOS dirty-video tracking pages.
  std::fill(memory.begin() + 0xb8000, memory.begin() + 0xc9000, 0xa5);
  const auto emit = [](const char *text) {
    for (; *text; ++text) MPE5VendorPutChar(static_cast<uint8_t>(*text));
  };
  emit("\x1b[2J\x1b[1;1Htop\x1b[2;1Hsecond\x1b[3;1Hthird\x1b[1;1H\x1b[M");
  if (textScreen(memory).substr(0, 6) != "second")
    throw std::runtime_error("BIOS ANSI delete-line did not scroll text");
  emit("\x1b[1;1H\x1b[2KDIRX\b\x1b[K\r\nC:\\>");
  if (textScreen(memory).substr(0, 4) != "DIR " || !atPrompt(memory))
    throw std::runtime_error("BIOS console backspace/erase/cursor did not produce a clean prompt");
  if (!std::all_of(memory.begin() + 0xb8000, memory.begin() + 0xc9000,
                   [](uint8_t value) { return value == 0xa5; }))
    throw std::runtime_error("native console overwrote guest video/BIOS shadow memory");
}

void poisonNativeStartupState(std::vector<uint8_t> &memory) {
  // Teensy places these native CPU globals in RAM2's NOLOAD DMA section.
  // Desktop BSS starts at zero and otherwise hides cold-start/relaunch bugs.
  std::fill(memory.begin(), memory.end(), 0xa5);
  mem = io_ports = opcode_stream = regs8 = vid_mem_base = memory.data();
  regs16 = reinterpret_cast<unsigned short *>(memory.data());
  i_rm = i_w = i_reg = i_mod = i_mod_size = i_d = i_reg4bit =
      raw_opcode_id = xlat_opcode_id = extra = scratch_uchar = 0xa5;
  seg_override_en = rep_override_en = 0xa5;
  seg_override = REG_ES;
  rep_mode = trap_flag = int8_asap = io_hi_lo = spkr_en = 1;
  reg_ip = file_index = wave_counter = 0xa5a5;
  op_source = op_dest = rm_addr = op_to_addr = op_from_addr =
      i_data0 = i_data1 = i_data2 = scratch_uint = scratch2_uint =
      inst_counter = set_flags_type = GRAPHICS_X = GRAPHICS_Y =
      vmem_ctr = 0xa5a5a5a5u;
  op_result = scratch_int = -1;
  std::fill_n(pixel_colors, 16, 0xa5a5a5a5u);
  std::fill_n(disk, 3, -1);
  std::fill_n(&bios_table_lookup[0][0], sizeof(bios_table_lookup), 0xa5);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: mpe5_vm_host_test <8086tiny-bios> <dosvm.img>\n";
    return 2;
  }
  try {
    const std::vector<uint8_t> bios = readFile(argv[1]);
    Image image{readFile(argv[2])};
    if (bios.empty() || bios.size() > 0xff00u ||
        image.bytes.empty() || image.bytes.size() % mpe5::SectorBytes)
      throw std::runtime_error("invalid BIOS or sector-aligned DOS image");

    std::vector<uint8_t> addressMap(mpe5::NativeBackingBytes);
    mpe5::Keyboard keyboard;
    mpe5::PcSpeaker speaker;
    const mpe5::CoreHost host{
        addressMap.data(), static_cast<uint32_t>(addressMap.size()),
        bios.data(), static_cast<uint16_t>(bios.size()),
        {&image, readSector,
         static_cast<uint32_t>(image.bytes.size() / mpe5::SectorBytes)},
        &keyboard, &speaker};
    for (unsigned boot = 0; boot < 2; ++boot) {
      if (boot) mpe5::coreReset();
      poisonNativeStartupState(addressMap);
      keyboard.clear();
      if (!mpe5::coreStart(host))
        throw std::runtime_error("native 8086 core rejected its host configuration");
      if (!boot) {
        verifyConsole(addressMap);
        if (!mpe5::coreStart(host)) throw std::runtime_error("core restart after console test failed");
      }

      runUntil(image, addressMap, "C:\\>", kBootSliceLimit,
               boot ? "FreeDOS restart with dirty RAM2" : "FreeDOS cold boot with dirty RAM2");

      // This is the same guest queue the bank-58 mailbox feeds. Seeing the
      // BOULDER.EXE entry proves BIOS keyboard delivery and disk reads after
      // both initial launch and reusing a previously running native core.
      queue(keyboard, boot ? "DIX\bR\r" : "DIR\r");
      runUntil(image, addressMap, "BOULDER  EXE", kCommandSliceLimit,
               "DIR keyboard command");
      runUntil(image, addressMap, "C:\\>", kCommandSliceLimit,
               "completed DIR and returned prompt");
      const std::string screen = textScreen(addressMap);
      for (const char *name : {"FDAUTO   BAT", "FREEDOS", "AUTOEXEC BAT", "BOULDER  EXE"}) {
        const auto first = screen.find(name);
        if (first == std::string::npos || screen.find(name, first + 1) != std::string::npos)
          throw std::runtime_error(std::string("missing/duplicated directory row: ") + name);
      }
    }

    mpe5::CgaText terminal;
    std::vector<uint8_t> records(mpe5::CgaTextCells * sizeof(mpe5::TextCell));
    const uint16_t changed = terminal.changes(
        addressMap.data() + mpe5::NativeTextViewportAddress, records.data(),
        mpe5::CgaTextCells);
    if (!changed) throw std::runtime_error("CGA text buffer remained empty");

    printScreen(addressMap);
    std::cout << "MPE5 VM host acceptance passed: dirty-RAM2 cold boot and restart, "
                 "isolated BIOS console, clean returned prompt, DIR/Backspace, "
                 "sector callback, and " << changed
              << " CGA text cells.\n";
    mpe5::coreReset();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "MPE5 VM host acceptance failed: " << error.what() << '\n';
    mpe5::coreReset();
    return 1;
  }
}
