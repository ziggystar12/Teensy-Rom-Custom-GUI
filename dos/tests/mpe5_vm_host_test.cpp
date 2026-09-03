// Host acceptance test for the exact native 8086 adapter used by the MPE5
// firmware.  It boots a supplied FreeDOS image through the same BIOS, sector
// callback, 20-bit memory map, CGA text address, and keyboard queue as the
// Teensy build.  It deliberately has no C64 or EasyFlash dependency.

#include <algorithm>
#include <chrono>
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
#include "../../engine/native-dos/mpe5_paged_memory.h"

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

struct PagedMachine {
  std::vector<uint8_t> store = std::vector<uint8_t>(mpe5::PagedMemory::PageCount * 512u, 0xa5);
  std::vector<uint8_t> workspace = std::vector<uint8_t>(mpe5::PagedMemory::WorkspaceBytes, 0xa5);
  std::vector<uint8_t> fixed = std::vector<uint8_t>(65536, 0xa5);
  std::vector<uint8_t> shadow = std::vector<uint8_t>(4000, 0xa5);
  std::vector<uint8_t> viewport = std::vector<uint8_t>(2000, 0xa5);
  mpe5::PagedMemory pager;
  mpe5::Keyboard keyboard;
  mpe5::PcSpeaker speaker;
  uint32_t sliceIo = 0, maximumSliceIo = 0, slices = 0;
  uint64_t maximumSliceUs = 0;
  bool failRead = false, failWrite = false;

  static bool pageRead(void *context, uint32_t page, uint8_t out[512]) {
    auto &self = *static_cast<PagedMachine *>(context);
    std::copy_n(self.store.data() + page * 512u, 512, out); return true;
  }
  static bool pageWrite(void *context, uint32_t page, const uint8_t data[512]) {
    auto &self = *static_cast<PagedMachine *>(context);
    std::copy_n(data, 512, self.store.data() + page * 512u); return true;
  }
  static bool reset(void *context) { return static_cast<PagedMachine *>(context)->pager.reset(); }
  static void unpinned(uint32_t address, uint32_t length) {
    if (length && address < 0x100000u && address + length > 0xf0000u)
      throw std::runtime_error("core passed permanently pinned F000 memory to the pager");
  }
  static bool read(void *context, uint32_t address, uint8_t *out, uint32_t length) {
    auto &self = *static_cast<PagedMachine *>(context); unpinned(address, length);
    return !self.failRead && self.pager.read(address, out, length);
  }
  static bool write(void *context, uint32_t address, const uint8_t *data, uint32_t length) {
    auto &self = *static_cast<PagedMachine *>(context); unpinned(address, length);
    return !self.failWrite && self.pager.write(address, data, length);
  }
  static bool yield(void *context) {
    auto &self = *static_cast<PagedMachine *>(context);
    const auto stats = self.pager.stats();
    return stats.pageReads + stats.pageWrites - self.sliceIo >= 4u;
  }
  void start(const std::vector<uint8_t> &bios, Image &image) {
    failRead = failWrite = false;
    if (!pager.start(workspace.data(), workspace.size(), {this, pageRead, pageWrite}))
      throw std::runtime_error("pager could not start");
    // Exercise the firmware's overlapping BIOS-source/permanent-memory case.
    std::copy(bios.begin(), bios.end(), fixed.begin());
    mpe5::CoreHost host{};
    host.bios = fixed.data(); host.biosBytes = uint16_t(bios.size());
    host.drive = {&image, readSector, uint32_t(image.bytes.size() / 512u)};
    host.keyboard = &keyboard; host.speaker = &speaker;
    host.memory = {this, reset, read, write, yield};
    host.fixedF000 = fixed.data(); host.fixedF000Bytes = uint32_t(fixed.size());
    host.consoleShadow = shadow.data(); host.consoleViewport = viewport.data();
    keyboard.clear();
    poisonNativeStartupState(workspace);
    if (!mpe5::coreStart(host)) throw std::runtime_error("paged core start failed");
  }
  bool run(uint32_t budget) {
    const auto before = pager.stats(); sliceIo = before.pageReads + before.pageWrites;
    const auto started = std::chrono::steady_clock::now();
    const bool running = mpe5::coreRun(budget);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    maximumSliceUs = std::max(maximumSliceUs, uint64_t(elapsed));
    const auto after = pager.stats();
    maximumSliceIo = std::max(maximumSliceIo, after.pageReads + after.pageWrites - sliceIo);
    ++slices; return running;
  }
  std::string screen() const {
    std::string result;
    for (unsigned cell = 0; cell < 1000; ++cell) {
      const uint8_t c = viewport[cell * 2]; result += c >= 32 && c <= 126 ? char(c) : ' ';
      if (cell % 40 == 39) result += '\n';
    }
    return result;
  }
  bool prompt() const {
    if (MPE5TextCursor < 4 || MPE5TextCursor % 80 != 4) return false;
    const char expected[] = "C:\\>";
    for (unsigned i = 0; i < 4; ++i)
      if (shadow[2 * (MPE5TextCursor - 4 + i)] != uint8_t(expected[i])) return false;
    return true;
  }
  void until(const char *needle, bool requirePrompt) {
    for (unsigned slice = 0; slice < 200000; ++slice) {
      if (!run(25000)) throw std::runtime_error("paged FreeDOS stopped:\n" + screen());
      if (screen().find(needle) != std::string::npos && (!requirePrompt || prompt())) return;
    }
    throw std::runtime_error("paged FreeDOS timeout:\n" + screen());
  }
  void program(std::initializer_list<uint8_t> bytes, uint16_t ip = 511) {
    const std::vector<uint8_t> code(bytes);
    if (!pager.write(0x10000u + ip, code.data(), uint32_t(code.size())))
      throw std::runtime_error("program write failed");
    regs16[REG_CS] = 0x1000; reg_ip = ip;
    regs16[REG_DS] = 0x2000; regs16[REG_ES] = 0x6000;
    regs16[REG_SS] = 0x5000; regs16[REG_SP] = 513;
    regs8[FLAG_IF] = regs8[FLAG_TF] = regs8[FLAG_DF] = 0;
    seg_override_en = rep_override_en = 0;
    MPE5RepeatPending = false;
  }
};

void verifyPagedCpu(const std::vector<uint8_t> &bios, Image &image,
                    const std::string &reference) {
  PagedMachine machine;
  for (unsigned boot = 0; boot < 2; ++boot) {
    machine.start(bios, image);
    machine.until("C:\\>", true);
    queue(machine.keyboard, boot ? "DIX\bR\r" : "DIR\r");
    machine.until("BOULDER  EXE", false);
    machine.until("C:\\>", true);
    if (machine.screen() != reference)
      throw std::runtime_error("paged/flat final DOS screens differ:\n" + machine.screen());
    const auto stats = machine.pager.stats();
    if (!stats.evictions || !stats.pageReads || !stats.pageWrites)
      throw std::runtime_error("DOS acceptance did not exercise swap eviction and rereads");
    if (machine.maximumSliceIo > 8u)
      throw std::runtime_error("DOS disk/REP processing exceeded the bounded swap-I/O slice");
    std::cout << "Paged FreeDOS " << (boot ? "restart" : "boot") << ": "
              << stats.pageReads << " swap reads, " << stats.pageWrites << " swap writes, "
              << machine.maximumSliceIo << " maximum swap I/Os per slice, "
              << machine.maximumSliceUs << " maximum host microseconds per slice.\n";
  }

  machine.start(bios, image);
  // Fetch begins at the last byte of a cache page; all word operands and the
  // stack write also cross page boundaries. Literal values check CPU results.
  machine.program({0xc7,0x06,0xff,0x01,0x34,0x12, 0x81,0x06,0xff,0x01,0x21,0x43,
                   0xa1,0xff,0x01, 0x50, 0xb8,0,0, 0x58, 0x90});
  for (unsigned instruction = 0; instruction < 6; ++instruction)
    if (!machine.run(1)) throw std::runtime_error("cross-page CPU fixture stopped");
  if (regs16[REG_AX] != 0x5555 || regs16[REG_SP] != 513 ||
      mpe5_detail::readBits(0x201ff, 2) != 0x5555 ||
      mpe5_detail::readBits(0x501ff, 2) != 0x5555)
    throw std::runtime_error("cross-page arithmetic or stack result mismatch");

  // Guest signed bytes must behave identically when the host's plain char is
  // unsigned (ARM default): backwards branches, disp8, and sign-extended imm8.
  machine.program({0xb9,3,0, 0xb8,1,0, 0x49,0x75,0xfd, 0x83,0xc0,0xff,
                   0xbb,1,2, 0xc6,0x47,0xfe,0x7a, 0xeb,0xfe});
  for (unsigned instruction = 0; instruction < 11; ++instruction)
    if (!machine.run(1)) throw std::runtime_error("signed-byte control-flow fixture stopped");
  if (regs16[REG_AX] || regs16[REG_CX] || mpe5_detail::readBits(0x201ff,1) != 0x7a)
    throw std::runtime_error("signed branch/displacement/immediate depended on host char type");
  machine.program({0xb0,0xfe, 0xb3,3, 0xf6,0xeb}); // IMUL BL: -2 * 3 = -6.
  for (unsigned instruction = 0; instruction < 3; ++instruction) machine.run(1);
  if (regs16[REG_AX] != 0xfffa) throw std::runtime_error("signed8-bit IMUL failed");
  machine.program({0xb8,0xfa,0xff, 0xb3,3, 0xf6,0xfb}); // IDIV BL: -6 / 3 = -2.
  for (unsigned instruction = 0; instruction < 3; ++instruction) machine.run(1);
  if (regs16[REG_AX] != 0x00fe) throw std::runtime_error("signed8-bit IDIV failed");
  machine.program({0x3f}); regs16[REG_AX] = 0x020b; regs8[FLAG_AF] = 0;
  if (!machine.run(1) || regs16[REG_AX] != 0x0105 || !regs8[FLAG_CF] || !regs8[FLAG_AF])
    throw std::runtime_error("AAS negative adjustment depended on host char type");

  machine.start(bios, image);
  std::vector<uint8_t> pattern(65536);
  for (uint32_t i = 0; i < pattern.size(); ++i) pattern[i] = uint8_t(i * 37u ^ (i >> 8));
  if (!machine.pager.write(0x20000, pattern.data(), uint32_t(pattern.size())))
    throw std::runtime_error("REP source write failed");
  machine.program({0xf3,0xa5,0xeb,0xfe});
  regs16[REG_CX] = 65535; regs16[REG_SI] = regs16[REG_DI] = 0;
  if (!machine.run(1)) throw std::runtime_error("REP prefix failed");
  uint16_t prior = regs16[REG_CX]; uint32_t repSlices = 0;
  do {
    if (!machine.run(7)) throw std::runtime_error("paged REP MOVSW stopped");
    const uint16_t current = regs16[REG_CX];
    if (prior - current > 7 || prior == current)
      throw std::runtime_error("REP iteration budget was not respected");
    prior = current; ++repSlices;
  } while (MPE5RepeatPending);
  std::vector<uint8_t> copied(pattern.size());
  if (regs16[REG_CX] || regs16[REG_SI] != 65534 || regs16[REG_DI] != 65534 ||
      !machine.pager.read(0x60000, copied.data(), uint32_t(copied.size())) || copied != pattern)
    throw std::runtime_error("resumed REP MOVSW changed guest results");

  machine.program({0xf3,0xa6,0xeb,0xfe});
  regs16[REG_CX] = 1000; regs16[REG_SI] = regs16[REG_DI] = 0;
  const uint8_t mismatch = uint8_t(pattern[500] ^ 0xffu);
  if (!machine.pager.write(0x601f4, &mismatch, 1)) throw std::runtime_error("compare fixture write failed");
  if (!machine.run(1)) throw std::runtime_error("compare prefix failed");
  do { if (!machine.run(11)) throw std::runtime_error("REP compare stopped"); } while (MPE5RepeatPending);
  if (regs16[REG_CX] != 499 || regs16[REG_SI] != 501 || regs16[REG_DI] != 501 || regs8[FLAG_ZF])
    throw std::runtime_error("resumed REPE comparison/flags mismatch");

  machine.program({0x90}); machine.failRead = true;
  if (machine.run(1) || machine.run(1)) throw std::runtime_error("memory read failure was not sticky");
  machine.start(bios, image);
  machine.program({0xa2,0x00,0x01}); machine.failWrite = true;
  if (machine.run(1) || machine.run(1)) throw std::runtime_error("memory write failure was not sticky");
  machine.start(bios, image);
  if (!machine.run(1)) throw std::runtime_error("failed-memory session could not restart");
  mpe5::coreReset();
  std::cout << "Paged CPU checks passed: word/fetch/stack page boundaries, "
            << repSlices << " bounded REP slices, comparison flags, sticky I/O failures and restart.\n";
  std::cout << "Signed-byte CPU checks passed with "
            << (std::is_signed<char>::value ? "signed" : "unsigned") << " host char.\n";
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
    verifyPagedCpu(bios, image, textScreen(addressMap));
    mpe5::coreReset();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "MPE5 VM host acceptance failed: " << error.what() << '\n';
    mpe5::coreReset();
    return 1;
  }
}
