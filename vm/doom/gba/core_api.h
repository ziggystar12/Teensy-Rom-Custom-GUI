// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void GbaCoreStart(void);
void GbaCoreStep(uint32_t keys);
unsigned GbaCoreTic(void);
int GbaCoreInLevel(void);
uint32_t GbaCoreInputMask(void);
void GbaSoundReset(void);
void GbaSoundTick(void);
void GbaSoundStop(int channel);
void GbaSoundPayload(uint8_t payload[26]);
void *GbaZone(unsigned *bytes);
void *GbaSupportAlloc(size_t bytes);
void GbaZoneAllocated(unsigned bytes);
void GbaZoneReleased(unsigned bytes);
void GbaZoneFailure(unsigned bytes);
uint32_t GbaFileSize(void);
void GbaRead(uint32_t offset,void *buffer,uint32_t bytes);
void GbaResourceLoaded(unsigned lump,unsigned bytes);
const void *GbaFrameLump(int lump);
void GbaEndFrame(void);
int GbaPatchWidth(int lump);
void GbaSetZoneTag(void *p,int tag);
int GbaClockTics(void);
void GbaFatal(const char *message) __attribute__((noreturn));
void I_Error(const char *format,...) __attribute__((noreturn));
char *strupr(char *s);
#ifdef __cplusplus
}
#endif
