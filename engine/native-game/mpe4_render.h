#ifndef MPE4_RENDER_H
#define MPE4_RENDER_H
#include "mpe4_game.h"

namespace mpe4 {
// Four original C64 actor layers: upper/lower multicolor bodies, then the
// matching accent overlays. Each section has two private colors in addition
// to the two VIC shared colors. Coordinates include the VIC screen origin.
struct EgoSprites {
  uint8_t shapes[256]{};
  uint16_t x=0;
  uint8_t y=0, colors[6]{}, enable=0;
};
// Caller-owned buffers: two packed 160x168 planes, an unpublished 10K frame
// reused by bounded picture flood fill, and the package's 128 ASCII glyphs.
// The renderer owns no heap, framebuffer, CPU, bus, or DMA state.
class Renderer {
 public:
  static constexpr uint16_t PlaneBytes=13440, FrameBytes=10000, FontBytes=1024;
  Host host{};
  uint8_t *visual=nullptr, *priority=nullptr, *scratch=nullptr;
  const uint8_t *font=nullptr;
  uint8_t priorityBase=48;
  uint8_t egoPaletteProfile=0;
  uint16_t maximumFillSeeds=0;
  MPE4_CODE bool init(const Host &,uint8_t *visualPlane,uint8_t *priorityPlane,
                       uint8_t *unpublishedFrame,const uint8_t *asciiFont);
  MPE4_CODE bool drawPicture(uint8_t id,bool overlay);
  MPE4_CODE bool viewCelInfo(uint8_t view,uint8_t loop,uint8_t cel,CelInfo *);
  MPE4_CODE bool addToPicture(uint8_t view,uint8_t loop,uint8_t cel,
                              uint8_t x,uint8_t y,uint8_t p,uint8_t margin);
  MPE4_CODE uint8_t priorityAt(uint8_t x,uint8_t y) const;
  // A distinct previous frame stabilizes color-code slots on live incremental
  // updates. Omit it for the first frame, a room replacement, or a mode change.
  // Matches the original C64 edit strip: one hires row only while typing.
  MPE4_CODE static bool parserSplit(const State &);
  // Optional idle refinement protects visible ego head colors. The Session
  // enables it only after a stable pose; the normal moving path is unchanged.
  MPE4_CODE bool render(const State &,uint8_t frame[FrameBytes],const uint8_t *previousFrame=nullptr,bool refineHead=false,EgoSprites *egoSprites=nullptr);
 private:
  struct Cel { uint32_t offset,size,nativeOffset; uint16_t nativePalette; uint8_t view,width,height,transparent,loops,cels,nativeBits; bool mirrored,nativeDecoded; };
  uint8_t cache[512]{};
  uint32_t cacheOffset=0;
  uint16_t cacheLength=0;
  uint8_t cacheType=255,cacheId=255;
  bool valid=true,visualOn=false,priorityOn=false;
  uint8_t visualColor=15,priorityColor=4,patternCode=0,patternNumber=0;
  MPE4_CODE bool byte(uint8_t type,uint8_t id,uint32_t offset,uint8_t &);
  MPE4_CODE bool word(uint8_t type,uint8_t id,uint32_t offset,uint16_t &);
  MPE4_CODE bool cel(uint8_t view,uint8_t loop,uint8_t number,Cel &);
  MPE4_CODE bool nativeCel(uint8_t view,uint8_t loop,uint8_t number,Cel &);
  MPE4_CODE bool celRow(const Cel &,uint8_t row,uint8_t *pixels);
  MPE4_CODE uint8_t autoPriority(int16_t y) const;
  MPE4_CODE uint8_t effectivePriority(int16_t x,int16_t y) const;
  MPE4_CODE uint8_t egoColor(uint8_t source,uint8_t view) const;
  MPE4_CODE void put(int16_t x,int16_t y);
  MPE4_CODE void line(int16_t x,int16_t y,int16_t endX,int16_t endY);
  MPE4_CODE bool fillAllowed(int16_t x,int16_t y) const;
  MPE4_CODE bool fill(int16_t x,int16_t y);
  MPE4_CODE void pattern(int16_t x,int16_t y);
};
static_assert(sizeof(Renderer) <= 768,"renderer state including resource cache must fit 768 bytes");
}
#endif
