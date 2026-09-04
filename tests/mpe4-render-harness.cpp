#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "mpe4_render.h"

using Bytes=std::vector<uint8_t>;
struct Fixture {
  Bytes resources[3][256];
  uint32_t reads=0,maxRead=0;
  static uint32_t size(void *ctx,uint8_t type,uint8_t id) {
    return type<3?uint32_t(static_cast<Fixture*>(ctx)->resources[type][id].size()):0;
  }
  static bool read(void *ctx,uint8_t type,uint8_t id,uint32_t off,uint8_t *out,uint16_t n) {
    auto &f=*static_cast<Fixture*>(ctx);uint32_t size=Fixture::size(ctx,type,id);
    if(off>size||n>size-off)return false;
    f.reads++;if(n>f.maxRead)f.maxRead=n;std::memcpy(out,f.resources[type][id].data()+off,n);return true;
  }
};
static Bytes load(const std::string &file) {std::ifstream f(file,std::ios::binary);assert(f.good());return Bytes(std::istreambuf_iterator<char>(f),{});}
static uint16_t u16(const uint8_t *p){return p[0]|(uint16_t(p[1])<<8);}
static uint32_t u32(const uint8_t *p){return u16(p)|(uint32_t(u16(p+2))<<16);}
static uint8_t get(const uint8_t *p,unsigned x,unsigned y){unsigned at=y*160+x;return(p[at/2]>>((at&1)?0:4))&15;}
static uint8_t colorAt(const uint8_t *f,unsigned x,unsigned y) {
  unsigned cell=(y/8)*40+x/4,code=(f[cell*8+(y&7)]>>(6-(x&3)*2))&3;
  return code==0?0:code==1?f[8000+cell]>>4:code==2?f[8000+cell]&15:f[9000+cell];
}
struct PublicationStats {uint64_t wrongPixelStates=0,paletteWrites=0;unsigned worst=0,frame=0,cell=0,write=0;};
static void publication(const uint8_t *old,const uint8_t *next,unsigned frame,PublicationStats &s) {
  for(unsigned cell=0;cell<1000;cell++) {
    if(!std::memcmp(old+cell*8,next+cell*8,8)&&old[8000+cell]==next[8000+cell]&&old[9000+cell]==next[9000+cell])continue;
    uint8_t working[10];std::memcpy(working,old+cell*8,8);working[8]=old[8000+cell];working[9]=old[9000+cell];
    s.paletteWrites+=(old[8000+cell]!=next[8000+cell])+(old[9000+cell]!=next[9000+cell]);
    // Exact live terminal order: eight bitmap bytes, screen byte, color byte.
    for(unsigned write=0;write<10;write++) {
      working[write]=write<8?next[cell*8+write]:next[(write==8?8000:9000)+cell];unsigned wrong=0;
      for(unsigned p=0;p<32;p++) {
        unsigned code=(working[p/4]>>(6-(p%4)*2))&3;
        uint8_t c=code==0?0:code==1?working[8]>>4:code==2?working[8]&15:working[9];
        unsigned x=(cell%40)*4+p%4,y=(cell/40)*8+p/4;
        if(c!=colorAt(old,x,y)&&c!=colorAt(next,x,y))wrong++;
      }
      s.wrongPixelStates+=wrong;
      if(wrong>s.worst){s.worst=wrong;s.frame=frame;s.cell=cell;s.write=write;}
    }
  }
}
int main(int argc,char **argv) {
  assert(argc==2);std::string dir=argv[1];Fixture fixture;
  auto resources=load(dir+"/resources.bin");size_t off=0;
  while(off<resources.size()){uint8_t type=resources[off],id=resources[off+1];uint32_t n=u32(resources.data()+off+2);off+=6;
    assert(type<3&&off+n<=resources.size());fixture.resources[type][id]=Bytes(resources.begin()+off,resources.begin()+off+n);off+=n;}
  auto font=load(dir+"/font.bin");assert(font.size()==1024);
  // Exact guards catch any packed-plane, flood-fill or output-frame overrun.
  std::array<uint8_t,13442> visual{},priority{};std::array<uint8_t,10002> next{};
  visual.front()=visual.back()=priority.front()=priority.back()=next.front()=next.back()=0xa5;
  mpe4::Host host{};host.context=&fixture;host.resourceSize=Fixture::size;host.readResource=Fixture::read;
  mpe4::Renderer render;assert(render.init(host,visual.data()+1,priority.data()+1,next.data()+1,font.data()));
  auto guard=[&](){assert(visual.front()==0xa5&&visual.back()==0xa5&&priority.front()==0xa5&&priority.back()==0xa5&&next.front()==0xa5&&next.back()==0xa5);};
  auto planes=[&](const uint8_t *expected,const char *kind,unsigned id){
    if(std::memcmp(visual.data()+1,expected,13440)||std::memcmp(priority.data()+1,expected+13440,13440)){
      std::cerr<<kind<<" "<<id<<" plane mismatch\n";for(unsigned i=0;i<26880;i++){
        uint8_t actual=i<13440?visual[i+1]:priority[i-13440+1];if(actual!=expected[i]){std::cerr<<" byte "<<i<<" actual "<<unsigned(actual)<<" expected "<<unsigned(expected[i])<<"\n";break;}}
      std::abort();}guard();};
  auto pictures=load(dir+"/pictures.bin");off=0;unsigned pictureCount=0,overlayCount=0;
  while(off<pictures.size()) {
    uint8_t base=pictures[off++],id=pictures[off++],overlay=pictures[off++];
    if(overlay)assert(render.drawPicture(base,false));
    assert(render.drawPicture(id,overlay));
    planes(pictures.data()+off,"picture",id);off+=26880;
    if(overlay)overlayCount++;else pictureCount++;
  }
  auto cels=load(dir+"/cels.bin");off=0;unsigned celCount=0;
  while(off<cels.size()) {
    const uint8_t *h=cels.data()+off;off+=10;mpe4::CelInfo info{};
    assert(render.viewCelInfo(h[0],h[1],h[2],&info));
    assert(info.width==h[6]&&info.height==h[7]&&info.loops==h[8]&&info.cels==h[9]);
    std::memset(visual.data()+1,0xff,13440);std::memset(priority.data()+1,0x44,13440);
    assert(render.addToPicture(h[0],h[1],h[2],h[3],h[4],h[5],4));
    planes(cels.data()+off,"cel",celCount);off+=26880;celCount++;
  }
  auto frames=load(dir+"/frames.bin");off=0;unsigned frameCount=0;
  while(off<frames.size()) {
    mpe4::State state{};state.priorityBase=48;state.graphics=frames[off++];
    state.pictureVisible=frames[off++];uint8_t picture=frames[off++];state.graphicsTop=frames[off++];
    uint8_t count=frames[off++];assert(count<=32);
    for(uint8_t i=0;i<count;i++){const uint8_t *a=frames.data()+off;off+=8;auto &o=state.objects[a[0]];
      o.view=a[1];o.loop=a[2];o.cel=a[3];o.x=a[4];o.y=a[5];o.priority=a[6];o.flags=a[7];}
    std::memcpy(state.text,frames.data()+off,1000);off+=1000;
    std::memcpy(state.attributes,frames.data()+off,1000);off+=1000;
    if(state.pictureVisible)assert(render.drawPicture(picture,false));
    const auto oldVisual=visual,oldPriority=priority;
    assert(render.render(state,next.data()+1));guard();assert(visual==oldVisual&&priority==oldPriority);
    if(std::memcmp(next.data()+1,frames.data()+off,10000)){
      std::cerr<<"frame "<<frameCount<<" mismatch\n";for(unsigned i=0;i<10000;i++)if(next[i+1]!=frames[off+i]){
        std::cerr<<" byte "<<i<<" actual "<<unsigned(next[i+1])<<" expected "<<unsigned(frames[off+i])<<"\n";break;}std::abort();}
    off+=10000;frameCount++;
  }
  // Priority3 terminates control scans; control0..2 survive permanent draws.
  std::memset(visual.data()+1,0xff,13440);std::memset(priority.data()+1,0x44,13440);
  priority[1+100*80]=0x03;priority[1+101*80]=0x55;
  assert(render.priorityAt(0,100)==0&&render.priorityAt(1,100)==3);
  fixture.resources[2][255]={0,0,1,0,0,7,0,1,3,0,2,3,15,0x22,0,0x22,0,0x22,0};
  priority[1+10*80]=0x0c;priority[1+11*80]=0x24;priority[1+12*80]=0x34;
  assert(render.addToPicture(255,0,0,0,12,5,4));
  assert(get(visual.data()+1,0,10)==2&&get(visual.data()+1,1,10)==15);
  assert(render.priorityAt(0,10)==0&&render.priorityAt(0,11)==2&&render.priorityAt(0,12)==5);
  assert(render.priorityAt(1,10)==12&&render.priorityAt(1,11)==5);
  assert(render.addToPicture(255,0,0,0,12,5,2));
  for(unsigned y=10;y<=12;y++)assert(render.priorityAt(0,y)==2&&render.priorityAt(1,y)==2);
  // Dynamic objects obey background priority and restore scenery when moving.
  std::memset(visual.data()+1,0xff,13440);std::memset(priority.data()+1,0x44,13440);
  priority[1+10*80]=0x4c;
  mpe4::State scene{};scene.graphics=true;scene.pictureVisible=true;scene.graphicsTop=8;scene.priorityBase=48;
  auto &actor=scene.objects[0];actor.view=255;actor.x=0;actor.y=12;actor.priority=5;actor.flags=mpe4::Animated|mpe4::Drawn|mpe4::FixedPriority;
  assert(render.render(scene,next.data()+1));
  assert(colorAt(next.data()+1,0,18)==5&&colorAt(next.data()+1,1,18)==1);
  actor.x=4;assert(render.render(scene,next.data()+1));
  assert(colorAt(next.data()+1,0,18)==1&&colorAt(next.data()+1,4,18)==5);
  scene.text[2*40+1]=32;scene.attributes[2*40+1]=0;
  assert(render.render(scene,next.data()+1));assert(colorAt(next.data()+1,4,18)==0);
  scene.text[2*40+1]=0;assert(render.render(scene,next.data()+1));assert(colorAt(next.data()+1,4,18)==5);
  // Compact text uses the established three-column glyph and a blank gutter.
  // In particular I/i retain their center stem; raw even-bit sampling lost it.
  scene.pictureVisible=false;std::memset(scene.text,0,1000);std::memset(scene.attributes,15,1000);
  scene.text[0]='I';scene.text[1]='i';assert(render.render(scene,next.data()+1));
  assert(!std::memcmp(next.data()+1,next.data()+9,8));
  assert(colorAt(next.data()+1,0,1)==0&&colorAt(next.data()+1,1,1)==1&&colorAt(next.data()+1,2,1)==0);
  for(unsigned y=0;y<8;y++)assert(colorAt(next.data()+1,3,y)==0&&colorAt(next.data()+1,7,y)==0);
  scene.attributes[0]=0xf0;assert(render.render(scene,next.data()+1));
  assert(colorAt(next.data()+1,1,1)==0&&colorAt(next.data()+1,3,1)==1);
  // Eight edge/corner forms reproduce the existing assembly's exact pixels.
  const uint8_t edges[]={1,2,4,8,5,9,6,10};
  std::memset(scene.text,0,1000);std::memset(scene.attributes,0xf0,1000);
  for(unsigned i=0;i<8;i++)scene.text[i]=mpe4::WindowMarker|edges[i];
  assert(render.render(scene,next.data()+1));
  for(unsigned i=0;i<8;i++)for(unsigned y=0;y<8;y++) {
    uint8_t oldPattern=(edges[i]&4)?0xa5:(edges[i]&8)?0x5a:0x55;
    if(((edges[i]&1)&&(y==0||y==2))||((edges[i]&2)&&(y==5||y==7)))oldPattern=0xaa;
    for(unsigned x=0;x<4;x++)assert(colorAt(next.data()+1,i*4+x,y)==(((oldPattern>>(6-x*2))&3)==2?2:1));
  }
  scene.graphics=false;assert(render.render(scene,next.data()+1));
  for(unsigned i=0;i<8;i++)for(unsigned y=0;y<8;y++) {
    const unsigned pattern=next[1+i*8+y];assert(next[1+8000+i]==0x21);
    for(unsigned x=0;x<4;x++) {
      bool red=((edges[i]&4)&&x<2)||((edges[i]&8)&&x>=2)||
        ((edges[i]&1)&&(y==0||y==2))||((edges[i]&2)&&(y==5||y==7));
      assert(((pattern>>(6-x*2))&3)==(red?3u:0u));
    }
  }
  // The original C64 parser is a hires row24 edit strip, with row23 blank for
  // the raster switch. It does not rescale the picture or duplicate row22.
  mpe4::State parser{};parser.graphics=parser.pictureVisible=parser.inputEnabled=true;
  parser.graphicsTop=8;parser.priorityBase=48;parser.inputRow=22;
  const char *command=">Look Around_";parser.inputLength=11;
  std::memset(parser.text+880,' ',40);std::memcpy(parser.text+880,command,std::strlen(command));
  std::memset(parser.attributes+880,15,40);
  assert(mpe4::Renderer::parserSplit(parser));assert(render.drawPicture(2,false));
  assert(render.render(parser,next.data()+1));guard();
  for(unsigned cell=880;cell<960;cell++) {
    for(unsigned y=0;y<8;y++)assert(next[1+cell*8+y]==0);
    assert(next[1+8000+cell]==0&&next[1+9000+cell]==0);
  }
  for(unsigned col=0;col<40;col++) {
    unsigned cell=960+col;assert(!std::memcmp(next.data()+1+cell*8,font.data()+parser.text[880+col]*8,8));
    assert(next[1+8000+cell]==0x10&&next[1+9000+cell]==0);
  }
  std::ofstream parserFrame(dir+"/parser-hires.frame",std::ios::binary);
  parserFrame.write(reinterpret_cast<const char*>(next.data()+1),10000);
  parser.inputLength=0;assert(mpe4::Renderer::parserSplit(parser));assert(render.render(parser,next.data()+1));
  for(unsigned cell=880;cell<920;cell++)for(unsigned y=0;y<8;y++)assert(next[1+cell*8+y]==0);
  parser.inputLength=11;parser.modal=mpe4::Message;assert(!mpe4::Renderer::parserSplit(parser));
  parser.modal=mpe4::StringInput;assert(!mpe4::Renderer::parserSplit(parser));
  parser.modal=mpe4::NoModal;parser.graphics=false;assert(!mpe4::Renderer::parserSplit(parser));
  assert(render.render(parser,next.data()+1));
  for(unsigned col=0;col<40;col++)assert(!std::memcmp(next.data()+1+(880+col)*8,font.data()+parser.text[880+col]*8,8));
  // Every final motion pixel remains identical to the source conversion.
  // Independently count temporary colors that match neither old nor new frame.
  const auto motion=load(dir+"/motion.bin");off=0;unsigned motionFrames=0,actorLoss=0,totalLoss=0;
  std::array<uint8_t,10000> oldCanonical{},oldStable{},canonical{},stable{};
  std::array<uint8_t,10000> refined{},refinedAgain{},movingAgain{};
  unsigned headLossBefore=0,headLossAfter=0;
  PublicationStats before{},after{};
  std::ofstream canonicalFrames(dir+"/motion-canonical.frames",std::ios::binary),stableFrames(dir+"/motion-stable.frames",std::ios::binary);
  while(off<motion.size()) {
    const uint8_t *h=motion.data()+off;off+=7;
    mpe4::State m{};m.graphics=m.pictureVisible=true;m.graphicsTop=8;m.priorityBase=48;
    auto &o=m.objects[0];o.view=h[1];o.loop=h[2];o.cel=h[3];o.x=h[4];o.y=h[5];o.priority=h[6];
    o.flags=mpe4::Animated|mpe4::Drawn|mpe4::FixedPriority;
    assert(render.drawPicture(h[0],false));assert(render.render(m,canonical.data()));
    assert(!std::memcmp(canonical.data(),motion.data()+off,10000));off+=10000;
    const uint8_t *target=motion.data()+off;off+=32000;const uint8_t *mask=motion.data()+off;off+=32000;
    assert(render.render(m,stable.data(),motionFrames%36?oldStable.data():nullptr));guard();
    for(unsigned cell=0;cell<1000;cell++) {
      uint16_t colors=0;for(unsigned p=0;p<32;p++)colors|=uint16_t(1)<<target[((cell/40)*8+p/4)*160+(cell%40)*4+p%4];
      colors&=~1u;unsigned local=0;for(unsigned c=1;c<16;c++)local+=(colors>>c)&1;
      for(unsigned p=0;p<32;p++) {
        unsigned x=(cell%40)*4+p%4,y=(cell/40)*8+p/4,at=y*160+x;
        const uint8_t actual=colorAt(stable.data(),x,y);
        assert(actual==colorAt(canonical.data(),x,y));
        if(actual!=target[at]){assert(local>3);totalLoss++;if(mask[at])actorLoss++;}
      }
    }
    if(motionFrames%36){publication(oldCanonical.data(),canonical.data(),motionFrames,before);publication(oldStable.data(),stable.data(),motionFrames,after);}
    // One explicitly requested stationary refinement improves the head. It
    // converges immediately and switching it off restores moving pixels exactly.
    assert(render.render(m,refined.data(),stable.data(),true));
    assert(render.render(m,refinedAgain.data(),refined.data(),true));
    assert(refined==refinedAgain);
    assert(render.render(m,movingAgain.data(),refined.data(),false));
    mpe4::CelInfo ci{};assert(render.viewCelInfo(o.view,o.loop,o.cel,&ci));
    int headEnd=int(o.y)-ci.height+1+8+(ci.height+2)/3;
    for(unsigned y=0;y<200;y++)for(unsigned x=0;x<160;x++) {
      assert(colorAt(movingAgain.data(),x,y)==colorAt(canonical.data(),x,y));
      unsigned at=y*160+x;if(mask[at]&&int(y)<headEnd) {
        headLossBefore+=colorAt(stable.data(),x,y)!=target[at];
        headLossAfter+=colorAt(refined.data(),x,y)!=target[at];
      }
    }
    canonicalFrames.write(reinterpret_cast<const char*>(canonical.data()),10000);
    stableFrames.write(reinterpret_cast<const char*>(stable.data()),10000);
    oldCanonical=canonical;oldStable=stable;motionFrames++;
  }
  assert(motionFrames==108&&before.wrongPixelStates>0&&after.wrongPixelStates<before.wrongPixelStates);
  assert(after.paletteWrites<before.paletteWrites);
  assert(headLossAfter<headLossBefore);
  assert(!render.render(scene,stable.data(),stable.data()));
  // Minimal reproduction: the same three colors exchange frequency ranks.
  // Fresh encoding swaps slots; stable encoding changes only bitmap pixels.
  mpe4::State ranks{};ranks.graphics=ranks.pictureVisible=true;ranks.graphicsTop=8;ranks.priorityBase=48;
  auto rankPixels=[&](unsigned whites){std::memset(visual.data()+1,0xff,13440);
    for(unsigned p=0;p<32;p++){unsigned at=(p/4)*160+p%4;uint8_t c=p<whites?15:p<28?1:4;
      uint8_t &b=visual[1+at/2];if(at&1)b=(b&0xf0)|c;else b=(b&15)|(c<<4);}};
  rankPixels(16);assert(render.render(ranks,oldCanonical.data()));
  rankPixels(12);assert(render.render(ranks,canonical.data()));assert(render.render(ranks,stable.data(),oldCanonical.data()));
  PublicationStats rankBefore{},rankAfter{};publication(oldCanonical.data(),canonical.data(),0,rankBefore);
  publication(oldCanonical.data(),stable.data(),0,rankAfter);
  assert(rankBefore.wrongPixelStates>0&&rankAfter.wrongPixelStates==0&&rankAfter.paletteWrites==0);
  // Bounds/truncation failures do not write outside caller buffers.
  fixture.resources[1][255]={0xf0};assert(!render.drawPicture(255,false));guard();
  fixture.resources[2][255]={0,0,1,0,0,7,0,1,3,0,2,1,0,0x03};
  mpe4::CelInfo info{};assert(render.viewCelInfo(255,0,0,&info));
  assert(!render.addToPicture(255,0,0,0,1,15,4));guard();
  assert(fixture.maxRead<=512);
  std::cout<<"{\"pictures\":"<<pictureCount<<",\"overlays\":"<<overlayCount<<",\"cels\":"<<celCount
    <<",\"frames\":"<<frameCount<<",\"maximumFillSeeds\":"<<render.maximumFillSeeds
    <<",\"maximumReadBytes\":"<<fixture.maxRead<<",\"resourceReads\":"<<fixture.reads
    <<",\"rendererBytes\":"<<sizeof(render)<<",\"stateBytes\":"<<sizeof(mpe4::State)
    <<",\"guardsIntact\":true,\"renderPreservesSourcePlanes\":true,\"compactGlyphsAndSpacing\":true,\"windowForms\":8"
    <<",\"motionFrames\":"<<motionFrames<<",\"finalMotionPixelsUnchanged\":true,\"quantizationLossOnlyOverThreeLocalColors\":true"
    <<",\"sameColorsRankSwapGlitchEliminated\":true,\"hiresParserRow24\":true,\"parserSeparatorBlank\":true"
    <<",\"idleRefinementConverges\":true,\"movingAfterRefinementPixelsExact\":true,\"headLossBefore\":"<<headLossBefore<<",\"headLossAfter\":"<<headLossAfter
    <<",\"actorQuantizationLossPixels\":"<<actorLoss<<",\"totalQuantizationLossPixels\":"<<totalLoss
    <<",\"publicationBefore\":{\"wrongPixelStates\":"<<before.wrongPixelStates<<",\"paletteWrites\":"<<before.paletteWrites
    <<",\"worst\":"<<before.worst<<",\"frame\":"<<before.frame<<",\"cell\":"<<before.cell<<",\"write\":"<<before.write<<"}"
    <<",\"publicationAfter\":{\"wrongPixelStates\":"<<after.wrongPixelStates<<",\"paletteWrites\":"<<after.paletteWrites
    <<",\"worst\":"<<after.worst<<",\"frame\":"<<after.frame<<",\"cell\":"<<after.cell<<",\"write\":"<<after.write<<"}}"<<std::endl;
}
