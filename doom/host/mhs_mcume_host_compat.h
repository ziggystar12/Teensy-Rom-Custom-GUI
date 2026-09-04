// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MHS_MCUME_HOST_COMPAT_H
#define MHS_MCUME_HOST_COMPAT_H

// The embedded sketch supplies these declarations through the Arduino build.
// Force-including this header keeps the pinned C sources type-safe when they
// are compiled directly by MinGW.
void delay(unsigned long milliseconds);
void emu_DrawLine16(unsigned short *pixels, int width, int height, int line);
void emu_GetTimeOfDay(int *microseconds, int *seconds);
void *emu_Malloc(int size);
void emu_printf(const char *text);

#endif
