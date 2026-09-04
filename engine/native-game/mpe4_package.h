#ifndef MPE4_PACKAGE_H
#define MPE4_PACKAGE_H
#include <stddef.h>
#include <stdint.h>
#ifndef MPE4_CODE
#define MPE4_CODE
#endif
namespace mpe4 {
typedef bool (*RawRead)(void *, uint32_t, uint8_t *, uint16_t);
struct Entry { uint32_t offset, length, crc; uint8_t type, id; };
class Package {
 public:
  MPE4_CODE bool open(RawRead, void *, uint32_t root, uint32_t limit);
  MPE4_CODE bool find(uint8_t type, uint8_t id, Entry &);
  MPE4_CODE uint32_t size(uint8_t type, uint8_t id);
  MPE4_CODE bool read(uint8_t type, uint8_t id, uint32_t offset, uint8_t *, uint16_t);
  MPE4_CODE bool verify(uint8_t type, uint8_t id);
  uint32_t root, bytes, crc;
  uint16_t count;
  uint16_t saveEpoch;
  bool ready, originalStartup, egoSprites;
  uint8_t spritePaletteProfile;
  char saveId[7];
 private:
  RawRead reader;
  void *context;
  uint32_t rawLimit, indexOffset, dataOffset, cacheStart;
  uint16_t cacheBytes;
  uint8_t cache[512];
  Entry previous;
  bool previousValid;
  MPE4_CODE bool raw(uint32_t offset, uint8_t *, uint16_t);
  MPE4_CODE bool entry(uint16_t index, Entry &);
};
MPE4_CODE uint32_t crc32Update(uint32_t, const uint8_t *, size_t);
}
#endif
