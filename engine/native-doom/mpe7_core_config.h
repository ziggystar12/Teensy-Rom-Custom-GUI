// SPDX-License-Identifier: GPL-2.0-or-later
// Target-only compatibility injected before every adapted MCUME C source.
#ifndef MPE7_CORE_CONFIG_H
#define MPE7_CORE_CONFIG_H

#include <Arduino.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *MPE7Malloc(size_t bytes);
void *MPE7Calloc(size_t count, size_t bytes);
void *MPE7Realloc(void *memory, size_t bytes);
void MPE7Free(void *memory);
char *MPE7Strdup(const char *text);
void MPE7Exit(int status) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

// The normal Teensy heap lives in RAM2 and ceases to exist when MPE7 seals
// reset-only ownership. Redirect only the generated Doom C translation units
// to the private fully-resident cartridge-tail allocator.
#define malloc(bytes) MPE7Malloc(bytes)
#define calloc(count, bytes) MPE7Calloc((count), (bytes))
#define realloc(memory, bytes) MPE7Realloc((memory), (bytes))
#define free(memory) MPE7Free(memory)
#define strdup(text) MPE7Strdup(text)
#define exit(status) MPE7Exit(status)

#endif
