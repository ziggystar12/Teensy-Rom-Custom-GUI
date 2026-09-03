#ifndef MPE5_8086TINY_H
#define MPE5_8086TINY_H

#include "mpe5_platform.h"

namespace mpe5 {

// The core owns no storage. The firmware supplies the exclusive PSRAM arena,
// immutable 8086tiny BIOS bytes, read-only C: sectors, and C64 keyboard queue.
struct CoreHost {
  uint8_t *addressMap = nullptr;
  uint32_t addressMapBytes = 0;
  const uint8_t *bios = nullptr;
  uint16_t biosBytes = 0;
  BlockDevice drive{};
  Keyboard *keyboard = nullptr;
  PcSpeaker *speaker = nullptr;
};

// Start leaves the 8086 at F000:0100 with BIOS drive 0x80 selected. run()
// executes a bounded instruction slice so SD and rendering always remain in
// the foreground poller, never in the PHI2 interrupt path.
MPE5_CODE bool coreStart(const CoreHost &host);
MPE5_CODE bool coreRun(uint32_t instructionBudget);
MPE5_CODE void coreReset();

}  // namespace mpe5

#endif
