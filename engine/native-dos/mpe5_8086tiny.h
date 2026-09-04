#ifndef MPE5_8086TINY_H
#define MPE5_8086TINY_H

#include "mpe5_platform.h"
#include "mpe5_video.h"
#include "mpe5_redirector.h"

namespace mpe5 {

// Memory callbacks transfer bytes by address; they never expose cache pointers.
// reset makes every unpinned address logically zero without requiring a full
// write of the backing file. A false result stops the current CPU session.
struct MemoryAccess {
  void *context = nullptr;
  bool (*reset)(void *context) = nullptr;
  bool (*read)(void *context, uint32_t address, uint8_t *out, uint32_t length) = nullptr;
  bool (*write)(void *context, uint32_t address, const uint8_t *data, uint32_t length) = nullptr;
  // Optional foreground service limit, such as four physical SD page I/Os.
  bool (*shouldYield)(void *context) = nullptr;
};

// The core owns no storage. Flat addressMap remains available for reference
// tests. A paged host instead supplies callbacks, a permanent writable F000
// segment (registers, BIOS data and BIOS stack), and private console buffers.
struct CoreHost {
  uint8_t *addressMap = nullptr;
  uint32_t addressMapBytes = 0;
  // Paged/hybrid hosts may pin conventional memory directly. The CPU adapter
  // serves this span before calling the generic memory callbacks.
  uint8_t *conventionalRam = nullptr;
  uint32_t conventionalRamBytes = 0;
  // 8086tiny derives a writable 20x256 decode table from its BIOS at startup.
  // Firmware supplies this from its reset-only RAM1 arena.
  uint8_t *decodeTable = nullptr;
  uint32_t decodeTableBytes = 0;
  const uint8_t *bios = nullptr;
  uint16_t biosBytes = 0;
  BlockDevice drive{};
  Keyboard *keyboard = nullptr;
  PcSpeaker *speaker = nullptr;
  MemoryAccess memory{};
  uint8_t *fixedF000 = nullptr;
  uint32_t fixedF000Bytes = 0;
  uint8_t *consoleShadow = nullptr;
  uint8_t *consoleViewport = nullptr;
  VideoObserver video{};
  // Monotonic host clock. Null selects deterministic instruction-derived
  // time for host acceptance; the cartridge supplies Arduino millis().
  uint32_t (*milliseconds)() = nullptr;
  void *redirectorContext = nullptr;
  bool (*redirector)(void *, uint8_t operation, RedirectorRegisters &) = nullptr;
  void (*redirectorReset)(void *) = nullptr;
};

enum class CoreStop : uint8_t {
  None, Stopped, InvalidRead, InvalidWrite, ReadFailure, WriteFailure
};

// Capture the first failure before a later operand can hide its address.
// This is reset per session and only written on failure, not per instruction.
struct CoreDiagnostic {
  uint32_t address = 0;
  uint16_t cs = 0, ip = 0;
  CoreStop reason = CoreStop::None;
  uint8_t opcode = 0;
};

struct ConsoleCursor {
  uint16_t position = 0;
  bool visible = true;
};

// Start verifies the initialized 512KiB RAM, prepares its BIOS boot banner,
// and leaves the 8086 at F000:0100 with BIOS drive 0x80 selected. The firmware
// presents that complete banner before permitting the first run(). run()
// executes a bounded instruction slice so SD and rendering always remain in
// the foreground poller, never in the PHI2 interrupt path.
MPE5_CODE bool coreStart(const CoreHost &host);
MPE5_CODE bool coreRun(uint32_t instructionBudget);
MPE5_CODE void coreReset();
MPE5_CODE CoreDiagnostic coreDiagnostic();
// Attach after coreStart and before the first coreRun, when the caller can
// reuse the BIOS source buffer as a zeroed VRAM mirror. Observation covers
// every successful guest write, including REP and disk-sector transfers.
MPE5_CODE void coreSetVideoObserver(const VideoObserver &observer);
MPE5_CODE VideoState coreVideoState();
// Cursor state belongs to the private 80-column console, never guest VRAM.
MPE5_CODE ConsoleCursor coreConsoleCursor();
// This view follows the active core; it does not retain movable guest pointers.
MPE5_CODE RedirectorMemory coreRedirectorMemory();
// Rewrite the pinned BIOS INT12 immediate to the configured conventional
// memory size. An already-patched staged copy is accepted.
MPE5_CODE bool patchBiosConventionalMemory(uint8_t *bios, uint32_t bytes);

}  // namespace mpe5

#endif
