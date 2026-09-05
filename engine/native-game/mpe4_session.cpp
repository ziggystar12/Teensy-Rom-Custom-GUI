#include "mpe4_session.h"
#include <string.h>
namespace mpe4 {
static MPE4_CODE uint32_t sound24(const uint8_t *p) { return p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16); }
MPE4_CODE uint32_t Session::size(void *p,uint8_t t,uint8_t id) { return static_cast<Session *>(p)->package.size(t,id); }
MPE4_CODE bool Session::read(void *p,uint8_t t,uint8_t id,uint32_t o,uint8_t *b,uint16_t n) { return static_cast<Session *>(p)->package.read(t,id,o,b,n); }
MPE4_CODE bool Session::picture(void *p,uint8_t id,bool overlay) { return static_cast<Session *>(p)->renderer.drawPicture(id,overlay); }
MPE4_CODE bool Session::cel(void *p,uint8_t v,uint8_t l,uint8_t c,CelInfo *i) { return static_cast<Session *>(p)->renderer.viewCelInfo(v,l,c,i); }
MPE4_CODE bool Session::add(void *p,uint8_t v,uint8_t l,uint8_t c,uint8_t x,uint8_t y,uint8_t pr,uint8_t m) {
  Session &a=*static_cast<Session *>(p);a.renderer.priorityBase=a.game.state.priorityBase;
  return a.renderer.addToPicture(v,l,c,x,y,pr,m);
}
MPE4_CODE uint8_t Session::pri(void *p,uint8_t x,uint8_t y) { return static_cast<Session *>(p)->renderer.priorityAt(x,y); }
MPE4_CODE bool Session::sound(void *p,uint8_t id) { return static_cast<Session *>(p)->play(id); }
MPE4_CODE void Session::silence(void *p) { static_cast<Session *>(p)->stop(); }
MPE4_CODE bool Session::save(void *p,uint8_t slot,const State *s,size_t n) {
  Session &a=*static_cast<Session *>(p);
  return a.storage.save && a.storage.save(a.storage.context,a.package.saveId,a.package.saveEpoch,slot,s,n);
}
MPE4_CODE bool Session::restore(void *p,uint8_t slot,State *s,size_t n) {
  Session &a=*static_cast<Session *>(p);
  if(!a.storage.restore || !a.storage.restore(a.storage.context,a.package.saveId,a.package.saveEpoch,slot,s,n)) return false;
  a.stop(); a.fullFrame=true; a.lastRoom=255; return true;
}
MPE4_CODE SaveInfo Session::saveInfo(void *p,uint8_t slot) {
  Session &a=*static_cast<Session *>(p);
  return a.storage.saveInfo?a.storage.saveInfo(a.storage.context,a.package.saveId,a.package.saveEpoch,slot):SaveInfo{SaveUnavailable,0,0};
}
MPE4_CODE bool Session::start(RawRead fn,void *context,uint32_t root,uint32_t limit,const Storage &store) {
  ready=false; framePending=false; frames=0; error=0; storage=store; hasCurrent=false;
  fullFrame=true; lastRoom=lastPicture=255; lastHires=true; lastParserSplit=parserSplit=false; cellCursor=0; stop();
  lastEgoPose=0;lastEgoView=255;stillFrames=0;lastRefineHead=refineHead=false;
  currentEgo=nextEgo=EgoSprites{};spritePart=2;
  if(!package.open(fn,context,root,limit)) { error=1; return false; }
  if(package.size(6,0)!=sizeof(font)||!package.read(6,0,0,font,sizeof(font))) {error=2;return false;}
  Host host{this,size,read,picture,cel,add,pri,sound,silence,save,restore,saveInfo};
  memset(current,0,sizeof(current)); memset(next,0,sizeof(next));
  if(!renderer.init(host,visual,priority,next,font)||!game.start(host,!package.originalStartup,package.crc)) {error=3;return false;}
  renderer.egoPaletteProfile=package.spritePaletteProfile;
  ready=true; return true;
}
MPE4_CODE void Session::stop() {
  soundActive=false;soundDone=false;memset(registers,0,sizeof(registers));memset(sid,0,sizeof(sid));
  for(auto &v:voices) {v.cursor=v.end=0;v.remaining=0;v.ended=true;}
}
MPE4_CODE bool Session::play(uint8_t id) {
  uint8_t header[9];uint32_t length=package.size(Sound,id);
  if(length<24||!package.read(Sound,id,0,header,9))return false;
  stop();soundId=id;
  for(uint8_t v=0;v<3;v++) {
    uint32_t begin=sound24(header+v*3),end=v==2?length:sound24(header+(v+1)*3);
    if((!v&&begin!=9)||begin>=end||end>length||(end-begin)%5)return false;
    voices[v]={begin,end,0,false};
  }
  soundActive=true;registers[24]=15;return true;
}
MPE4_CODE bool Session::scoreTick() {
  sid[0]=0;
  if(soundActive) {
    bool ended=true;
    for(uint8_t i=0;i<3;i++) {
      Voice &v=voices[i];uint8_t *r=registers+i*7;
      if(v.ended)continue;
      if(v.remaining&&--v.remaining) {ended=false;continue;}
      uint8_t b[5];if(v.cursor>v.end||v.end-v.cursor<5||!package.read(Sound,soundId,v.cursor,b,5))return false;
      v.cursor+=5;uint16_t duration=b[0]|(uint16_t(b[1])<<8);
      if(duration==65535) {if(v.cursor!=v.end)return false;v.ended=true;r[4]=0x40;continue;}
      if(!duration||(b[4]&0xe0))return false;
      ended=false;v.remaining=duration;
      if((b[4]&16)||(r[4]&128))sid[0]|=1<<i;
      r[0]=b[2];r[1]=b[3];r[2]=0;r[3]=8;r[5]=0;r[6]=(b[4]&15)<<4;
      r[4]=!(b[4]&15)?0:((b[4]&16)?0x81:0x41);
    }
    if(ended) {soundActive=false;soundDone=true;}
  }
  memcpy(sid+1,registers,25);
  if(!game.flag(9)) {sid[0]=0;memset(sid+1,0,25);}
  return true;
}
MPE4_CODE bool Session::prepareFrame(Input input) {
  if(!ready||framePending)return false;
  input.soundFinished|=soundDone;soundDone=false;
  if(game.tick(input)==Failed) {error=uint8_t(16+game.state.error);return false;}
  hires=!game.state.graphics;
  parserSplit=Renderer::parserSplit(game.state);
  fullFrame=!hasCurrent||lastHires!=hires||(frames&&(lastRoom!=game.state.vars[0]||lastPicture!=game.state.picture));
  const State &s=game.state;const Object &ego=s.objects[0];
  const uint32_t pose=ego.x|(uint32_t(ego.y)<<8)|(uint32_t(ego.loop)<<16)|(uint32_t(ego.cel)<<24);
  const bool stationary=!fullFrame&&s.graphics&&s.playerControl&&s.modal==NoModal&&
    (ego.flags&Drawn)&&!ego.direction&&pose==lastEgoPose&&ego.view==lastEgoView;
  if(!stationary)stillFrames=0;
  else if(stillFrames<6){const uint32_t ticks=stillFrames+(input.elapsed60Hz?input.elapsed60Hz:1);stillFrames=ticks<6?ticks:6;}
  refineHead=stillFrames>=6;
  // Refine once after six stationary video ticks. Movement immediately returns
  // to the existing color selection; neither path changes game timing.
  if(fullFrame||lastParserSplit!=parserSplit||lastRefineHead!=refineHead||game.state.frameDirty||game.state.textDirty) {
    if(!renderer.render(game.state,next,fullFrame?nullptr:current,refineHead,package.egoSprites?&nextEgo:nullptr)) {error=4;return false;}
    game.state.frameDirty=game.state.textDirty=false;
  } else {memcpy(next,current,sizeof(next));nextEgo=currentEgo;}
  if(!scoreTick()) {error=5;return false;}
  spritePart=package.egoSprites&&nextEgo.enable&&(!currentEgo.enable||
    memcmp(currentEgo.shapes,nextEgo.shapes,sizeof(nextEgo.shapes)))?0:2;
  cellCursor=0;framePending=true;return true;
}
MPE4_CODE uint8_t Session::spritePacket(uint8_t *payload) {
  if(!payload||!framePending||!package.egoSprites||spritePart>=2)return 0;
  payload[0]=1;payload[1]=spritePart;
  memcpy(payload+2,nextEgo.shapes+uint16_t(spritePart)*128,128);spritePart++;
  return 130;
}
MPE4_CODE uint8_t Session::spriteDescriptor(uint8_t *payload) const {
  if(!payload||!package.egoSprites)return 0;
  payload[0]=1;payload[1]=nextEgo.enable;payload[2]=uint8_t(nextEgo.x);payload[3]=uint8_t(nextEgo.x>>8);
  payload[4]=nextEgo.y;memcpy(payload+5,nextEgo.colors,6);return 11;
}
MPE4_CODE uint8_t Session::cells(uint8_t *records,uint8_t maximum,bool &first) {
  first=fullFrame&&cellCursor==0;uint8_t count=0;
  while(cellCursor<1000&&count<maximum) {
    uint16_t c=cellCursor++;
    if(!fullFrame&&!memcmp(current+c*8,next+c*8,8)&&current[8000+c]==next[8000+c]&&current[9000+c]==next[9000+c])continue;
    uint8_t *r=records+count++*12;r[0]=c;r[1]=c>>8;memcpy(r+2,next+c*8,8);r[10]=next[8000+c];r[11]=next[9000+c];
  }
  return count;
}
MPE4_CODE void Session::acknowledgeFrame() {
  if(!framePending)return;
  memcpy(current,next,sizeof(current));framePending=false;frames++;hasCurrent=true;
  currentEgo=nextEgo;
  lastRoom=game.state.vars[0];lastPicture=game.state.picture;lastHires=hires;lastParserSplit=parserSplit;
  const Object &ego=game.state.objects[0];
  lastEgoPose=ego.x|(uint32_t(ego.y)<<8)|(uint32_t(ego.loop)<<16)|(uint32_t(ego.cel)<<24);
  lastEgoView=ego.view;lastRefineHead=refineHead;
}
MPE4_CODE void Session::seedPresentedFrame(bool highResolution) {
  hasCurrent=true;lastHires=highResolution;lastParserSplit=false;
  stillFrames=0;lastEgoView=255;lastRefineHead=refineHead=false;
}
}
