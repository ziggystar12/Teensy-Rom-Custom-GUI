#include "mpe5_8086tiny.h"

// The upstream source is compiled in this translation unit after its host
// main loop has been replaced with the bounded MPE5 adapter hooks.
#ifndef DMAMEM
#define DMAMEM
#endif
#define MPE5_NATIVE 1
#define NO_GRAPHICS 1
#include "vendor/8086tiny/8086tiny.c"

namespace mpe5 {

MPE5_CODE bool coreStart(const CoreHost &host) { return MPE5VendorStart(host); }

MPE5_CODE bool coreRun(uint32_t instructionBudget) {
  return MPE5VendorRun(instructionBudget);
}

MPE5_CODE void coreReset() { MPE5VendorReset(); }

}  // namespace mpe5
