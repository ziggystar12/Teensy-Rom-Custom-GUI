#ifndef MPE4_SESSION_H
#define MPE4_SESSION_H
#include "mpe4_package.h"
#include "mpe4_render.h"
namespace mpe4 {
struct Storage {
  void *context;
  bool (*save)(void *, const char *saveId, uint16_t saveEpoch, uint8_t slot, const State *, size_t);
  bool (*restore)(void *, const char *saveId, uint16_t saveEpoch, uint8_t slot, State *, size_t);
  // May reuse next[] as scratch while opening a modal, before rendering begins.
  SaveInfo (*saveInfo)(void *, const char *saveId, uint16_t saveEpoch, uint8_t slot);
};
// Construct in the retired intro arena. The published frame is never used as
// decoder scratch, and a new game tick cannot begin until frame-end ACK.
class Session {
 public:
#if defined(MHS_AGI_EXTERNAL_STATE)
  Game game;
  explicit Session(State &guest) : game(guest) {}
#else
  Game game{};
#endif
  Package package{};
  Renderer renderer{};
  uint8_t visual[13440], priority[13440], current[10000], next[10000], font[1024];
  uint8_t sid[26];
  EgoSprites currentEgo{},nextEgo{};
  bool ready=false, framePending=false, fullFrame=true, hires=true, parserSplit=false,refineHead=false;
  uint8_t error=0;
  uint32_t frames=0;
  MPE4_CODE bool start(RawRead,void *,uint32_t root,uint32_t limit,const Storage &);
  MPE4_CODE bool prepareFrame(Input);
  MPE4_CODE uint8_t cells(uint8_t *records,uint8_t maximum,bool &first);
  // Type5 transfers only changed shapes into the terminal's hidden bank.
  // The final SID descriptor commits that bank and its coordinates together.
  MPE4_CODE uint8_t spritePacket(uint8_t *payload);
  MPE4_CODE uint8_t spriteDescriptor(uint8_t *payload) const;
  MPE4_CODE void acknowledgeFrame();
  // Caller reconstructs the already displayed intro endpoint in current[];
  // the first native frame then transmits only its genuine differences.
  MPE4_CODE void seedPresentedFrame(bool highResolution);
 private:
  struct Voice { uint32_t cursor,end; uint16_t remaining; bool ended; } voices[3];
  Storage storage{};
  uint8_t soundId=0,registers[25]{},lastRoom=255,lastPicture=255;
  uint16_t cellCursor=0;
  uint32_t lastEgoPose=0;
  uint8_t lastEgoView=255,stillFrames=0;
  uint8_t spritePart=2;
  bool soundActive=false,soundDone=false,lastHires=true,lastParserSplit=false,hasCurrent=false;
  bool lastRefineHead=false;
  MPE4_CODE bool play(uint8_t);
  MPE4_CODE void stop();
  MPE4_CODE bool scoreTick();
  MPE4_CODE static uint32_t size(void *,uint8_t,uint8_t);
  MPE4_CODE static bool read(void *,uint8_t,uint8_t,uint32_t,uint8_t *,uint16_t);
  MPE4_CODE static bool picture(void *,uint8_t,bool);
  MPE4_CODE static bool cel(void *,uint8_t,uint8_t,uint8_t,CelInfo *);
  MPE4_CODE static bool add(void *,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t);
  MPE4_CODE static uint8_t pri(void *,uint8_t,uint8_t);
  MPE4_CODE static bool sound(void *,uint8_t);
  MPE4_CODE static void silence(void *);
  MPE4_CODE static bool save(void *,uint8_t,const State *,size_t);
  MPE4_CODE static bool restore(void *,uint8_t,State *,size_t);
  MPE4_CODE static SaveInfo saveInfo(void *,uint8_t);
};
static_assert(sizeof(Session)<=65536,"native gameplay must reuse the existing 64 KiB intro arena");
}
#endif
