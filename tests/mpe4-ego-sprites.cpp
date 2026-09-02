#include "mpe4_session.h"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using Bytes=std::vector<uint8_t>;
static Bytes load(const char *path) {std::ifstream f(path,std::ios::binary);assert(f.good());return Bytes(std::istreambuf_iterator<char>(f),{});}
static bool raw(void *ctx,uint32_t off,uint8_t *out,uint16_t n) {const auto &b=*static_cast<Bytes*>(ctx);if(off>b.size()||n>b.size()-off)return false;memcpy(out,b.data()+off,n);return true;}
static uint8_t code(const mpe4::EgoSprites &s,unsigned layer,unsigned x,unsigned y) {return(s.shapes[layer*64+y*3+x/4]>>(6-(x%4)*2))&3;}
static int color(const mpe4::EgoSprites &s,unsigned x,unsigned y) {
  const unsigned section=y/21,body=code(s,section,x,y%21),accent=code(s,section+2,x,y%21);
  // No two layers own one source pixel. VIC body slots have lower IDs.
  assert(!body||!accent);
  if(body)return body==1?s.colors[0]:body==3?s.colors[1]:s.colors[2+section];
  if(accent){assert(accent==2);return s.colors[4+section];}return -1;
}
static void pixel(uint8_t *p,unsigned x,unsigned y,uint8_t v) {unsigned at=y*160+x;if(at&1)p[at/2]=(p[at/2]&0xf0)|v;else p[at/2]=(p[at/2]&15)|(v<<4);}
static void headerFlags(Bytes &bytes,uint32_t flags) {
  for(unsigned i=0;i<4;i++){bytes[32+i]=uint8_t(flags>>(i*8));bytes[28+i]=0;}
  const uint32_t crc=mpe4::crc32Update(0xffffffffu,bytes.data(),64)^0xffffffffu;
  for(unsigned i=0;i<4;i++)bytes[28+i]=uint8_t(crc>>(i*8));
}
struct Synthetic {
  Bytes view;
  static uint32_t size(void *ctx,uint8_t type,uint8_t) {return type==mpe4::View?static_cast<Synthetic*>(ctx)->view.size():0;}
  static bool read(void *ctx,uint8_t type,uint8_t,uint32_t off,uint8_t *out,uint16_t n) {
    auto &bytes=static_cast<Synthetic*>(ctx)->view;if(type!=mpe4::View||off>bytes.size()||n>bytes.size()-off)return false;
    memcpy(out,bytes.data()+off,n);return true;
  }
  void cel(unsigned width,unsigned height) {
    view={0,0,1,0,0,7,0,1,3,0,uint8_t(width),uint8_t(height),15};
    for(unsigned y=0;y<height;y++){for(unsigned x=0;x<width;x++)view.push_back(uint8_t((x%4+1)*16+1));view.push_back(0);}
  }
};
int main(int argc,char **argv) {
  assert(argc==3);auto package=load(argv[1]),cels=load(argv[2]);
  mpe4::Session session{};assert(session.start(raw,&package,0,package.size(),{}));
  assert(session.package.egoSprites);auto &render=session.renderer;
  mpe4::State scene{};scene.graphics=scene.pictureVisible=true;scene.graphicsTop=8;scene.priorityBase=48;
  auto &ego=scene.objects[0];ego.x=66;ego.y=112;ego.view=0;ego.priority=10;
  ego.flags=mpe4::Animated|mpe4::Drawn|mpe4::FixedPriority;
  std::array<uint8_t,10000> background{},frame{};
  mpe4::EgoSprites sprite{};unsigned count=0,eyePixels=0,accentFrames=0,threeLayers=0,fourLayers=0;
  for(size_t at=0;at<cels.size();) {
    ego.loop=cels[at++];ego.cel=cels[at++];const uint8_t width=cels[at++],height=cels[at++],transparent=cels[at++];
    const uint8_t *source=cels.data()+at;at+=width*height;assert(at<=cels.size());
    memset(session.visual,0x55,sizeof(session.visual));memset(session.priority,0x44,sizeof(session.priority));
    scene.textDirty=scene.frameDirty=true;
    assert(render.render(scene,frame.data(),nullptr,false,&sprite));
    auto noEgo=scene;noEgo.objects[0].flags=0;assert(render.render(noEgo,background.data()));
    assert(frame==background); // No bitmap-shaped holes or duplicate ego.
    assert(sprite.x==24+ego.x*2&&sprite.y==50+8+ego.y-height+1);
    unsigned layers=0;for(unsigned l=0;l<4;l++){layers+=(sprite.enable>>(l+1))&1;assert(!sprite.shapes[l*64+63]);}
    threeLayers+=layers==3;fourLayers+=layers==4;accentFrames+=(sprite.enable&0x18)!=0;
    for(unsigned y=0;y<42;y++)for(unsigned x=0;x<12;x++) {
      const bool opaque=x<width&&y<height&&source[y*width+x]!=transparent;
      assert((color(sprite,x,y)>=0)==opaque);
      if(opaque&&source[y*width+x]==7&&y<(height+2)/3&&session.package.spritePaletteProfile) {
        assert(color(sprite,x,y)==0);eyePixels++;
      }
    }
    // Exact source-priority mask applies to bodies and accents together.
    const auto original=sprite;const int top=int(ego.y)-height+1;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)if((x+y)%3==0)pixel(session.priority,ego.x+x,top+y,15);
    assert(render.render(scene,frame.data(),nullptr,false,&sprite));
    assert(!memcmp(sprite.colors,original.colors,6));
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)
      assert(color(sprite,x,y)==((x+y)%3==0?-1:color(original,x,y)));
    // Text cells are opaque even where their glyph has a transparent-looking
    // blank; closing that cell restores exactly the previous source pose.
    memset(session.priority,0x44,sizeof(session.priority));const unsigned tx=ego.x/4,ty=(top+8)/8;
    scene.text[ty*40+tx]=' ';scene.attributes[ty*40+tx]=0;
    assert(render.render(scene,frame.data(),nullptr,false,&sprite));
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
      const bool covered=(ego.x+x)/4==tx&&(top+8+y)/8==ty;
      assert(color(sprite,x,y)==(covered?-1:color(original,x,y)));
    }
    scene.text[ty*40+tx]=0;assert(render.render(scene,frame.data(),nullptr,false,&sprite));
    assert(!memcmp(sprite.shapes,original.shapes,256));
    // A later same-priority object hides only its opaque pixels. Earlier
    // foreground-baseline order remains identical to the original compositor.
    auto &other=scene.objects[1];other=ego;other.x+=2;
    assert(render.render(scene,frame.data(),nullptr,false,&sprite));
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
      const bool covered=x>=2&&source[y*width+x-2]!=transparent;
      assert(color(sprite,x,y)==(covered?-1:color(original,x,y)));
    }
    other.flags=0;count++;
  }
  assert(count&&accentFrames&&threeLayers+fourLayers);
  if(session.package.spritePaletteProfile)assert(eyePixels);
  // Coordinate-only frames reuse all four shapes. A changed cel or mask sends
  // two complete halves; frame-end ACK is the only publication point.
  session.game.state=scene;session.game.state.frameDirty=true;session.game.state.running=false;
  memset(session.visual,0x55,sizeof(session.visual));memset(session.priority,0x44,sizeof(session.priority));
  uint8_t payload[228]{},descriptor[11]{};
  assert(session.prepareFrame({}));assert(session.spritePacket(payload)==130&&payload[0]==1&&payload[1]==0);
  assert(!memcmp(payload+2,session.nextEgo.shapes,128));assert(!session.currentEgo.enable);
  assert(session.spritePacket(payload)==130&&payload[1]==1);assert(!memcmp(payload+2,session.nextEgo.shapes+128,128));
  assert(!session.spritePacket(payload));assert(session.spriteDescriptor(descriptor)==11);
  assert(descriptor[0]==1&&descriptor[1]==session.nextEgo.enable);assert(!session.prepareFrame({}));
  session.acknowledgeFrame();const auto first=session.currentEgo;
  session.game.state.objects[0].x++;session.game.state.frameDirty=true;
  assert(session.prepareFrame({}));assert(!session.spritePacket(payload));
  assert(session.nextEgo.x==first.x+2);assert(!memcmp(session.nextEgo.shapes,first.shapes,256));session.acknowledgeFrame();
  session.game.state.text[14*40+17]=' ';session.game.state.frameDirty=true;
  assert(session.prepareFrame({}));assert(session.spritePacket(payload)==130);assert(session.spritePacket(payload)==130);session.acknowledgeFrame();
  session.game.state.graphics=false;session.game.state.frameDirty=true;
  assert(session.prepareFrame({}));assert(!session.nextEgo.enable&&!session.spritePacket(payload));session.acknowledgeFrame();
  // Capability is package-owned, never inferred from a stale pointer or game
  // save. Older cartridges keep 26-byte SID packets and bitmap composition.
  headerFlags(package,0);mpe4::Session old{};assert(old.start(raw,&package,0,package.size(),{}));
  old.game.state=scene;old.game.state.running=false;old.game.state.frameDirty=true;
  assert(old.prepareFrame({}));assert(!old.spritePacket(payload)&&!old.spriteDescriptor(descriptor));assert(!old.nextEgo.enable);
  assert(memcmp(old.next,background.data(),10000));
  mpe4::Package invalid{};
  for(uint32_t flags:{0x100u,0x302u,0x402u,0x80000002u}) {headerFlags(package,flags);assert(!invalid.open(raw,&package,0,package.size()));}
  // Exercise the exact 12x42 boundary, safe bitmap fallback beyond it, and
  // clipping of partial cels. A source pixel owns at most one layer throughout.
  Synthetic synthetic;auto syntheticHost=render.host;syntheticHost.context=&synthetic;
  syntheticHost.resourceSize=Synthetic::size;syntheticHost.readResource=Synthetic::read;
  assert(render.init(syntheticHost,session.visual,session.priority,session.next,session.font));
  scene.objects[0].loop=scene.objects[0].cel=0;scene.objects[0].view=255;scene.objects[0].x=150;scene.objects[0].y=20;
  scene.objects[1].flags=0;memset(scene.text,0,sizeof(scene.text));
  for(const auto dimensions:{std::array<unsigned,2>{12,42},{13,42},{12,43}}) {
    synthetic.cel(dimensions[0],dimensions[1]);
    assert(render.init(syntheticHost,session.visual,session.priority,session.next,session.font));
    assert(render.render(scene,frame.data(),nullptr,false,&sprite));
    if(dimensions[0]>12||dimensions[1]>42){assert(!sprite.enable);assert(render.render(scene,background.data()));assert(frame==background);continue;}
    for(unsigned y=0;y<42;y++)for(unsigned x=0;x<12;x++)assert((color(sprite,x,y)>=0)==(x<10&&y>=21));
    scene.shakeTicks=1;assert(render.render(scene,frame.data(),nullptr,false,&sprite));
    assert(sprite.x==24+(150+2)*2&&sprite.y==50+20-42+1+8+2);
    for(unsigned y=0;y<42;y++)for(unsigned x=0;x<12;x++)assert((color(sprite,x,y)>=0)==(x<8&&y>=21));
    scene.shakeTicks=0;
  }
  std::cout<<"{\"cels\":"<<count<<",\"grayEyePixelsProtected\":"<<eyePixels<<",\"accentFrames\":"<<accentFrames
    <<",\"threeLayerFrames\":"<<threeLayers<<",\"fourLayerFrames\":"<<fourLayers
    <<",\"sessionBytes\":"<<sizeof(session)<<",\"stateBytes\":"<<sizeof(mpe4::State)
    <<",\"sourceVisibilityAndTextMasks\":true,\"coordinateOnlySkipsShapes\":true,\"legacyBitmapFallback\":true,\"geometryAndClipping\":true}"<<std::endl;
}
