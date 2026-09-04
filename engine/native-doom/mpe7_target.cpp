// SPDX-License-Identifier: GPL-2.0-or-later
// TeensyROM/SdFat boundary for the reset-only adapted MCUME Doom core.

#include <Arduino.h>
#include <SD.h>
#include <limits.h>
#include <new>
#include <stdint.h>
#include <string.h>

#include "mpe7_target.h"
#include "ff.h"

namespace {

static constexpr uint32_t AllocationMagic = 0x3748504du;  // MPH7
static constexpr size_t AllocationAlignment = 8u;
static constexpr size_t WadPathBytes = 256u;

struct AllocationHeader {
  uint32_t bytes;
  uint32_t magic;
};

alignas(FsFile) static uint8_t WadFileStorage[sizeof(FsFile)];
static FsFile *WadFile;
static char WadPath[WadPathBytes];
static uint32_t WadBytes;
static uint8_t *PrivateBegin;
static uint8_t *PrivateCursor;
static uint8_t *PrivateLimit;
static size_t PrivateHighWater;
static uint8_t *EmuBegin;
static uint8_t *EmuCursor;
static uint8_t *EmuLimit;
static size_t EmuHighWater;
static bool Prepared;
static bool Claimed;
static bool Healthy;
static char LastError[96];

static MPE7_TARGET_CODE uintptr_t alignUp(uintptr_t value, size_t alignment) {
  return (value + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
}

static MPE7_TARGET_CODE void setError(const char *message) {
  Healthy = false;
  if (!message) message = "unknown Doom target error";
  size_t bytes = strlen(message);
  if (bytes >= sizeof(LastError)) bytes = sizeof(LastError) - 1u;
  memcpy(LastError, message, bytes);
  LastError[bytes] = '\0';
}

static MPE7_TARGET_CODE FsFile *asFile(FIL *file) {
  FsFile *stream = file && *file ? reinterpret_cast<FsFile *>(*file) : nullptr;
  return stream == WadFile ? stream : nullptr;
}

static MPE7_TARGET_CODE bool exactWadPath(const char *path) {
  return path && WadPath[0] && strcmp(path, WadPath) == 0;
}

}  // namespace

long systime;
int joystick;

bool MPE7TargetPrepare(const char *wad_path, void *private_heap,
                       size_t private_heap_bytes) {
  MPE7TargetResetBeforeClaim();
  if (!wad_path || !wad_path[0] || strlen(wad_path) >= sizeof(WadPath) ||
      !private_heap || private_heap_bytes < 128u * 1024u) {
    setError("invalid Doom WAD path or private heap");
    return false;
  }

  WadFile = new (WadFileStorage) FsFile(SD.sdfs.open(wad_path, O_RDONLY));
  uint8_t header[4] = {};
  if (!WadFile->isOpen() || WadFile->fileSize() < sizeof(header) ||
      WadFile->fileSize() > UINT32_MAX ||
      WadFile->read(header, sizeof(header)) != (int)sizeof(header) ||
      (memcmp(header, "IWAD", sizeof(header)) != 0 &&
       memcmp(header, "PWAD", sizeof(header)) != 0) ||
      !WadFile->seekSet(0)) {
    if (WadFile->isOpen()) WadFile->close();
    WadFile->~FsFile();
    WadFile = nullptr;
    setError("unable to pre-open a valid Doom WAD");
    return false;
  }

  memcpy(WadPath, wad_path, strlen(wad_path) + 1u);
  WadBytes = (uint32_t)WadFile->fileSize();
  PrivateBegin = reinterpret_cast<uint8_t *>(
      alignUp(reinterpret_cast<uintptr_t>(private_heap), AllocationAlignment));
  const uintptr_t supplied = reinterpret_cast<uintptr_t>(private_heap);
  const uintptr_t limit = supplied + private_heap_bytes;
  if (limit < supplied || reinterpret_cast<uintptr_t>(PrivateBegin) >= limit) {
    WadFile->close();
    WadFile->~FsFile();
    WadFile = nullptr;
    setError("Doom private heap bounds overflowed");
    return false;
  }
  PrivateCursor = PrivateBegin;
  PrivateLimit = reinterpret_cast<uint8_t *>(limit);
  PrivateHighWater = 0u;
  Prepared = Healthy = true;
  LastError[0] = '\0';
  return true;
}

bool MPE7TargetBeginClaimed(void *emu_arena, size_t emu_arena_bytes) {
  if (!Prepared || Claimed || !Healthy || !WadFile || !WadFile->isOpen() || !emu_arena ||
      emu_arena_bytes < 80u * 1024u) {
    setError("Doom target could not enter reset-only mode");
    return false;
  }
  EmuBegin = reinterpret_cast<uint8_t *>(
      alignUp(reinterpret_cast<uintptr_t>(emu_arena), AllocationAlignment));
  const uintptr_t supplied = reinterpret_cast<uintptr_t>(emu_arena);
  const uintptr_t limit = supplied + emu_arena_bytes;
  if (limit < supplied || reinterpret_cast<uintptr_t>(EmuBegin) >= limit) {
    setError("Doom emulation arena bounds overflowed");
    return false;
  }
  EmuCursor = EmuBegin;
  EmuLimit = reinterpret_cast<uint8_t *>(limit);
  EmuHighWater = 0u;
  Claimed = true;
  return true;
}

void MPE7TargetResetBeforeClaim() {
  if (Claimed) return;
  if (WadFile) {
    if (WadFile->isOpen()) WadFile->close();
    WadFile->~FsFile();
    WadFile = nullptr;
  }
  WadPath[0] = '\0';
  WadBytes = 0u;
  PrivateBegin = PrivateCursor = PrivateLimit = nullptr;
  PrivateHighWater = 0u;
  EmuBegin = EmuCursor = EmuLimit = nullptr;
  EmuHighWater = 0u;
  Prepared = false;
  Healthy = true;
  LastError[0] = '\0';
}

bool MPE7TargetHealthy() { return Healthy; }
const char *MPE7TargetLastError() { return LastError; }
size_t MPE7TargetPrivateHeapUsed() {
  return PrivateBegin && PrivateCursor ? (size_t)(PrivateCursor - PrivateBegin)
                                       : 0u;
}
size_t MPE7TargetPrivateHeapHighWater() { return PrivateHighWater; }
size_t MPE7TargetEmuArenaUsed() {
  return EmuBegin && EmuCursor ? (size_t)(EmuCursor - EmuBegin) : 0u;
}
size_t MPE7TargetEmuArenaHighWater() { return EmuHighWater; }

extern "C" MPE7_TARGET_CODE void *MPE7Malloc(size_t bytes) {
  if (!bytes) return nullptr;
  if (bytes > SIZE_MAX - (AllocationAlignment - 1u)) {
    setError("Doom allocation size overflowed");
    return nullptr;
  }
  const uintptr_t cursor = alignUp(reinterpret_cast<uintptr_t>(PrivateCursor),
                                   AllocationAlignment);
  const size_t payload = (bytes + AllocationAlignment - 1u) &
                         ~(AllocationAlignment - 1u);
  if (payload > SIZE_MAX - sizeof(AllocationHeader)) {
    setError("Doom allocation size overflowed");
    return nullptr;
  }
  const size_t total = sizeof(AllocationHeader) + payload;
  const uintptr_t limit = reinterpret_cast<uintptr_t>(PrivateLimit);
  if (!Prepared || !Healthy || !PrivateCursor || !PrivateLimit ||
      cursor > limit || total > limit - cursor) {
    setError("Doom private heap exhausted");
    return nullptr;
  }
  auto *header = reinterpret_cast<AllocationHeader *>(cursor);
  header->bytes = (uint32_t)bytes;
  header->magic = AllocationMagic;
  PrivateCursor = reinterpret_cast<uint8_t *>(cursor + total);
  const size_t used = MPE7TargetPrivateHeapUsed();
  if (used > PrivateHighWater) PrivateHighWater = used;
  return header + 1;
}

extern "C" MPE7_TARGET_CODE void *MPE7Calloc(size_t count, size_t bytes) {
  if (count && bytes > SIZE_MAX / count) {
    setError("Doom calloc size overflowed");
    return nullptr;
  }
  const size_t total = count * bytes;
  void *memory = MPE7Malloc(total);
  if (memory) memset(memory, 0, total);
  return memory;
}

extern "C" MPE7_TARGET_CODE void *MPE7Realloc(void *memory, size_t bytes) {
  if (!memory) return MPE7Malloc(bytes);
  if (!bytes) return nullptr;
  const uintptr_t pointer = reinterpret_cast<uintptr_t>(memory);
  const uintptr_t begin = reinterpret_cast<uintptr_t>(PrivateBegin);
  const uintptr_t cursor = reinterpret_cast<uintptr_t>(PrivateCursor);
  if (!Prepared || !Healthy || !PrivateBegin || !PrivateCursor ||
      pointer < begin + sizeof(AllocationHeader) || pointer > cursor) {
    setError("Doom realloc received a foreign pointer");
    return nullptr;
  }
  auto *header = reinterpret_cast<AllocationHeader *>(memory) - 1;
  if (reinterpret_cast<uintptr_t>(header) < begin ||
      reinterpret_cast<uintptr_t>(header) >= cursor ||
      header->magic != AllocationMagic || header->bytes > cursor - pointer) {
    setError("Doom realloc received a foreign pointer");
    return nullptr;
  }
  void *replacement = MPE7Malloc(bytes);
  if (replacement)
    memcpy(replacement, memory, header->bytes < bytes ? header->bytes : bytes);
  return replacement;
}

extern "C" MPE7_TARGET_CODE void MPE7Free(void *) {
  // One process, one WAD, one reset-only session. Individual frees are safely
  // deferred to the mandatory MCU reset; realloc still preserves old bytes.
}

extern "C" MPE7_TARGET_CODE char *MPE7Strdup(const char *text) {
  if (!text) return nullptr;
  const size_t bytes = strlen(text) + 1u;
  char *copy = static_cast<char *>(MPE7Malloc(bytes));
  if (copy) memcpy(copy, text, bytes);
  return copy;
}

extern "C" MPE7_TARGET_CODE void MPE7Exit(int) {
  setError("Doom requested process exit");
  // RAM2 now contains the live Doom process and the USB1 storage controller
  // has been quiesced.  Never enter the normal Teensy yield path again.
  for (;;) __asm__ volatile("wfi");
}

extern "C" MPE7_TARGET_CODE void *emu_Malloc(int bytes) {
  if (bytes <= 0) return nullptr;
  const uintptr_t cursor = alignUp(reinterpret_cast<uintptr_t>(EmuCursor),
                                   AllocationAlignment);
  const uintptr_t limit = reinterpret_cast<uintptr_t>(EmuLimit);
  const size_t aligned = ((size_t)bytes + AllocationAlignment - 1u) &
                         ~(AllocationAlignment - 1u);
  if (!Claimed || !Healthy || !EmuCursor || !EmuLimit || cursor > limit ||
      aligned > limit - cursor) {
    setError("Doom framebuffer/view arena exhausted");
    return nullptr;
  }
  void *memory = reinterpret_cast<void *>(cursor);
  memset(memory, 0, aligned);
  EmuCursor = reinterpret_cast<uint8_t *>(cursor + aligned);
  const size_t used = MPE7TargetEmuArenaUsed();
  if (used > EmuHighWater) EmuHighWater = used;
  return memory;
}

extern "C" MPE7_TARGET_CODE void *emu_MallocI(unsigned int bytes) {
  return bytes <= (unsigned int)INT_MAX ? emu_Malloc((int)bytes) : nullptr;
}

extern "C" MPE7_TARGET_CODE void emu_Free(void *) {
  // The fixed RAM2 emulation arena is reclaimed only by the mandatory reset.
}

extern "C" MPE7_TARGET_CODE void emu_GetTimeOfDay(int *microseconds, int *seconds) {
  const uint32_t now = micros();
  systime = (long)millis();
  if (seconds) *seconds = (int)(now / 1000000u);
  if (microseconds) *microseconds = (int)(now % 1000000u);
}

extern "C" MPE7_TARGET_CODE void emu_printf(const char *text) {
  if (text && text[0]) setError(text);
}

extern "C" MPE7_TARGET_CODE void emu_DrawLine16(unsigned short *, int, int, int) {}

extern "C" MPE7_TARGET_CODE char *strupr(char *text) {
  if (!text) return nullptr;
  for (char *cursor = text; *cursor; ++cursor)
    if (*cursor >= 'a' && *cursor <= 'z') *cursor -= ('a' - 'A');
  return text;
}

extern "C" MPE7_TARGET_CODE FRESULT f_open(FIL *file, const char *path,
                                             unsigned char mode) {
  if (!file || !exactWadPath(path) || (mode & FA_WRITE) ||
      !WadFile || !WadFile->isOpen() || !WadFile->seekSet(0))
    return FR_NO_FILE;
  *file = reinterpret_cast<FIL>(WadFile);
  return FR_OK;
}

extern "C" MPE7_TARGET_CODE FRESULT f_close(FIL *file) {
  if (!asFile(file)) return FR_INVALID_OBJECT;
  // Keep the pre-opened handle alive in RAM1 for the reset-only session.
  *file = nullptr;
  return FR_OK;
}

extern "C" MPE7_TARGET_CODE FRESULT f_read(FIL *file, void *buffer,
                                             unsigned int bytes,
                                             unsigned int *read_bytes) {
  FsFile *stream = asFile(file);
  if (!stream || (!buffer && bytes)) return FR_INVALID_OBJECT;
  const int count = stream->read(buffer, bytes);
  if (read_bytes) *read_bytes = count > 0 ? (unsigned int)count : 0u;
  return count < 0 ? FR_DISK_ERR : FR_OK;
}

extern "C" MPE7_TARGET_CODE FRESULT f_readn(FIL *file, void *buffer,
                                              unsigned int bytes,
                                              unsigned int *read_bytes) {
  unsigned int total = 0u;
  while (total != bytes) {
    unsigned int chunk = 0u;
    FRESULT result = f_read(file, static_cast<uint8_t *>(buffer) + total,
                            bytes - total, &chunk);
    if (result != FR_OK || !chunk) {
      if (read_bytes) *read_bytes = total;
      return result == FR_OK ? FR_DISK_ERR : result;
    }
    total += chunk;
  }
  if (read_bytes) *read_bytes = total;
  return FR_OK;
}

extern "C" MPE7_TARGET_CODE FRESULT f_write(FIL *, const void *, unsigned int,
                                              unsigned int *written) {
  if (written) *written = 0u;
  return FR_WRITE_PROTECTED;
}

extern "C" MPE7_TARGET_CODE FRESULT f_writen(FIL *file, const void *buffer,
                                               unsigned int bytes,
                                               unsigned int *written) {
  return f_write(file, buffer, bytes, written);
}

extern "C" MPE7_TARGET_CODE FRESULT f_lseek(FIL *file, unsigned long offset) {
  FsFile *stream = asFile(file);
  return stream && stream->seekSet(offset) ? FR_OK : FR_DISK_ERR;
}

extern "C" MPE7_TARGET_CODE unsigned long f_tell(FIL *file) {
  FsFile *stream = asFile(file);
  return stream ? (unsigned long)stream->curPosition() : 0ul;
}

extern "C" MPE7_TARGET_CODE unsigned long f_size(FIL *file) {
  FsFile *stream = asFile(file);
  return stream ? (unsigned long)stream->fileSize() : 0ul;
}

extern "C" MPE7_TARGET_CODE FRESULT f_unlink(const char *) {
  return FR_WRITE_PROTECTED;
}
extern "C" MPE7_TARGET_CODE FRESULT f_rename(const char *, const char *) {
  return FR_WRITE_PROTECTED;
}

extern "C" MPE7_TARGET_CODE FRESULT f_stat(const char *path, FILINFO *info) {
  if (!exactWadPath(path) || !WadFile || !WadFile->isOpen()) return FR_NO_FILE;
  if (info) {
    memset(info, 0, sizeof(*info));
    info->fsize = (signed long)WadBytes;
  }
  return FR_OK;
}

extern "C" MPE7_TARGET_CODE FRESULT f_mkdir(const char *) {
  // Saves are intentionally outside this first playable proof. Pretending the
  // empty configuration directory exists avoids any post-takeover SD open.
  return FR_OK;
}
