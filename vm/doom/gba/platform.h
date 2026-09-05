// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../../abi/vm_abi.h"
namespace gbadoomvm {
struct Metrics {
    uint32_t zoneUsed,zoneHighWater,zoneRequest,supportUsed;
    uint32_t reads,readBytes,resourceBytes,resourceCount,lastLump;
};
bool prepare(const VmHost *host);
bool start();
bool step(uint32_t keys);
void close();
const char *error();
Metrics metrics();
const uint8_t *pixels();
const uint8_t *palette();
}
