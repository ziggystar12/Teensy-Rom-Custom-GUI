// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *DoomMalloc(size_t);
void *DoomCalloc(size_t,size_t);
void *DoomRealloc(void *,size_t);
void DoomFree(void *);
char *DoomStrdup(const char *);
void DoomExit(int) __attribute__((noreturn));
unsigned char *DoomZone(int *);
void DoomZoneFailure(unsigned,unsigned,unsigned,unsigned);
void DoomZoneAllocated(unsigned);
void DoomZoneReleased(unsigned);
void delay(unsigned long);
void emu_DrawLine16(unsigned short *,int,int,int);
void emu_GetTimeOfDay(int *,int *);
void *emu_Malloc(int);
void emu_printf(const char *);
#ifdef __cplusplus
}
#endif
#define malloc DoomMalloc
#define calloc DoomCalloc
#define realloc DoomRealloc
#define free DoomFree
#define strdup DoomStrdup
#define exit DoomExit
