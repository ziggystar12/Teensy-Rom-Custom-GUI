// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace gb {
constexpr uint32_t ClockHz=2097152; // half-dot units; LCD rate unchanged in CGB double speed
struct Video {void *context;void (*line)(void *,unsigned,const uint8_t *,const uint16_t *);void (*frame)(void *);};
// One machine per independently loaded VM. No heap, storage, or hardware calls.
const char *inspect(const uint8_t *rom,size_t bytes);
bool start(const uint8_t *rom,size_t bytes,Video video);
unsigned run(unsigned units); // actual elapsed units, including final instruction overshoot
void buttons(uint8_t nesWireButtons);
void capture(bool enabled);
const char *error();
bool color();
uint64_t ticks();
void sid(uint8_t packet[26],uint32_t sidClock=1022727);
uint8_t peek(uint16_t address);
void poke(uint16_t address,uint8_t value);
// Battery-backed SRAM only; module owns storage and persistence. The core
// performs no file I/O. Non-battery cartridges return null/zero.
uint8_t *saveData();
unsigned saveBytes();
uint32_t saveRevision();
}
