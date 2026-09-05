// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../abi/vm_abi.h"
namespace doomvm {
struct Metrics {
    uint32_t heapHighWater,heapUsed,heapFailure;
    uint32_t zoneRequest,zonePinned,zonePurgeable,zoneFree;
    uint32_t reads,readBytes;
    uint32_t zoneUsed,zoneHighWater;
};
bool prepare(const VmHost *host,void *heap,uint32_t heapBytes,const char *wad);
void closeFiles();
Metrics metrics();
}
