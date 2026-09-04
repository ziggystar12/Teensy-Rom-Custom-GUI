#ifndef MPE5_REDIRECTOR_H
#define MPE5_REDIRECTOR_H

#include "mpe5_platform.h"

namespace mpe5 {

// Core/TSR ABI. Service registers contain the original INT 2F caller's FLAGS,
// SS and SP, including its six-byte interrupt return frame at SS:SP.
struct RedirectorRegisters {
  uint16_t ax=0, bx=0, cx=0, dx=0, si=0, di=0, bp=0;
  uint16_t ds=0, es=0, ss=0, sp=0, flags=0;
};

struct RedirectorMemory {
  void *context = nullptr;
  bool (*read)(void *, uint32_t, uint8_t *, uint32_t) = nullptr;
  bool (*write)(void *, uint32_t, const uint8_t *, uint32_t) = nullptr;
};

struct RedirectorFileInfo {
  char name[13]{}; // One ASCII DOS 8.3 component, never a path.
  uint32_t size = 0;
  uint16_t time = 0, date = 0x0021; // Packed FAT time/date.
  uint8_t attributes = 0;
};

// Every callback returns a DOS error number (zero means success). Enumeration
// returns 18 at its end. Paths are canonical absolute paths relative to the
// shared host folder: '/' is /DOSVM/D. The adapter confines all operations to
// that root. No callback may retain the passed path or buffer pointer.
struct RedirectorHost {
  void *context = nullptr;
  uint16_t (*stat)(void *, const char *, RedirectorFileInfo &) = nullptr;
  uint16_t (*enumerate)(void *, const char *, uint16_t, RedirectorFileInfo &) = nullptr;
  // slot is 0..15. mode preserves DOS access/share bits. action is the DOS
  // extended-open action: 01 open existing, 10 create new, 11 open/create,
  // 12 create/replace. result is 1 opened, 2 created, or 3 replaced.
  uint16_t (*open)(void *, uint8_t, const char *, uint16_t mode,
                   uint16_t action, uint8_t attributes,
                   RedirectorFileInfo &, uint16_t &result) = nullptr;
  uint16_t (*close)(void *, uint8_t) = nullptr;
  uint16_t (*read)(void *, uint8_t, uint32_t offset, uint8_t *,
                  uint16_t requested, uint16_t &actual) = nullptr;
  uint16_t (*write)(void *, uint8_t, uint32_t offset, const uint8_t *,
                   uint16_t requested, uint16_t &actual) = nullptr;
  uint16_t (*truncate)(void *, uint8_t, uint32_t size) = nullptr;
  uint16_t (*flush)(void *, uint8_t) = nullptr;
  uint16_t (*setTime)(void *, uint8_t, uint16_t time, uint16_t date) = nullptr;
  uint16_t (*setAttributes)(void *, const char *, uint8_t) = nullptr;
  uint16_t (*mkdir)(void *, const char *) = nullptr;
  uint16_t (*rmdir)(void *, const char *) = nullptr;
  uint16_t (*remove)(void *, const char *) = nullptr;
  uint16_t (*rename)(void *, const char *, const char *) = nullptr;
  uint16_t (*space)(void *, uint32_t &totalSectors, uint32_t &freeSectors) = nullptr;
};

class Redirector {
 public:
  static constexpr uint8_t HandleCount = 16, SearchCount = 16;
  static constexpr uint16_t PathBytes = 128;
  MPE5_CODE void configure(const RedirectorMemory &, const RedirectorHost &);
  MPE5_CODE void reset();
  // operation 0 installs: DS:SI=SDA, ES:BX=List of Lists, AL=zero-based drive.
  // operation 1 dispatches INT 2F/11xx. False means chain without register edits.
  MPE5_CODE bool service(uint8_t operation, RedirectorRegisters &);
  bool installed() const { return installed_; }

 private:
  struct Handle { uint32_t sft=0; uint16_t owner=0; bool used=false; char path[PathBytes]{}; };
  struct Search { char path[PathBytes]{}; char pattern[11]{}; uint32_t dta=0; uint16_t cursor=0, token=0, owner=0;
                  uint8_t attributes=0; bool used=false; };
  RedirectorMemory memory_{};
  RedirectorHost host_{};
  Handle handles_[HandleCount]{};
  Search searches_[SearchCount]{};
  uint8_t buffer_[1024]{};
  uint32_t sda_=0, cds_=0;
  uint16_t token_=0;
  uint8_t drive_=3;
  bool installed_=false, memoryFailed_=false;
  MPE5_CODE bool read(uint32_t, void *, uint32_t);
  MPE5_CODE bool write(uint32_t, const void *, uint32_t);
  MPE5_CODE uint8_t byte(uint32_t);
  MPE5_CODE uint16_t word(uint32_t);
  MPE5_CODE uint32_t dword(uint32_t);
  MPE5_CODE uint32_t farPointer(uint32_t);
  MPE5_CODE bool putWord(uint32_t, uint16_t);
  MPE5_CODE bool putDword(uint32_t, uint32_t);
  MPE5_CODE uint16_t path(uint32_t, char *, bool wildcard=false);
  MPE5_CODE bool ours(uint8_t, const RedirectorRegisters &);
  MPE5_CODE int handle(uint32_t);
  MPE5_CODE uint16_t openFile(RedirectorRegisters &, uint8_t);
  MPE5_CODE uint16_t transfer(RedirectorRegisters &, bool);
  MPE5_CODE uint16_t find(RedirectorRegisters &, bool);
  MPE5_CODE uint16_t findNext(Search &);
  MPE5_CODE uint16_t closeFile(uint32_t);
  MPE5_CODE uint16_t eraseOrRename(bool);
  MPE5_CODE uint16_t dispatch(uint8_t, RedirectorRegisters &);
};

static_assert(sizeof(Redirector) < 12u*1024u, "Redirector must fit borrowed RAM1");

} // namespace mpe5
#endif
