#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../abi/vm_abi.h"
#include "../../engine/native-dos/mpe5_video.cpp"
#include "../dos/video.h"
#include "../video/mpe_video_live.cpp"
static unsigned color(const uint8_t *c,unsigned x,unsigned y,bool hires,unsigned background){
 if(hires)return c[y]&(128>>x)?c[8]>>4:c[8]&15;
 const auto b=(c[y]>>(6-(x/2)*2))&3;
 return b==0?background:b==1?c[8]>>4:b==2?c[8]&15:c[9];
}
int main(){
 static uint8_t vram[32768],arena[mpe5::CgaVideo::WorkspaceBytes],records[12000];
 for(unsigned i=0;i<sizeof vram;i++)vram[i]=uint8_t(i*73+(i>>5)*11);
 mpe5::CgaVideo old;assert(old.start(arena,sizeof arena));old.write(0,vram,sizeof vram);
 mpe_video::LiveConverter converter;mpe_video::LiveFrame frame{};DosRaster raster;
 VmIndexedRasterFrame source{};source.read_pixel=DosRaster::pixel;source.context=&raster;
 for(unsigned mode:{4u,5u,6u})for(unsigned select=0;select<64;select++){
  mpe5::VideoState state{};state.mode=mode;state.enabled=true;state.colorSelect=select;state.startAddress=4093;
  old.setState(state);assert(old.changes(records,1000)==1000);
  raster.capture(state,vram,source);const auto &s=source.frame;
  mpe_video::IndexedSource native{nullptr,s.palette,s.width,s.height,0,s.colors,source.geometry,DosRaster::pixel,&raster};
  assert(converter.render(native,mode==6?3:0,frame));
  for(unsigned cell=0;cell<1000;cell++)for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)
   assert(color(records+cell*12+2,x,y,mode==6,old.background())==color(frame.cells[cell],x,y,mode==6,frame.background));
 }
 // Hardware nibble order, two/four-bank addressing, display-start wrap,
 // palette mask, blanking, and native geometry survive the new adapter.
 for(unsigned mode:{8u,9u}){
  mpe5::VideoState state{};state.mode=mode;state.enabled=true;state.startAddress=4095;state.tandyMask=7;
  for(unsigned i=0;i<16;i++)state.tandyPalette[i]=15-i;
  raster.capture(state,vram,source);const unsigned banks=mode==8?2:4,stride=mode==8?80:160;
  assert(source.frame.width==(mode==8?160:320));assert(bool(source.geometry&2)==(mode==8));
  for(unsigned y=0;y<200;y++)for(unsigned x=0;x<source.frame.width;x++){
   const auto b=vram[(y%banks)*8192+((8190+(y/banks)*stride+x/2)&8191)];
   assert(DosRaster::pixel(&raster,x,y)==((b>>(x%2?0:4))&15));
  }
  state.enabled=false;raster.capture(state,vram,source);
  for(auto c:raster.palette)assert(!c);assert(!DosRaster::pixel(&raster,0,0));
 }
 puts("PASS: shared F1 CGA 4/5 and Sharp 640 shrink exactly match legacy DOS colors/pixels across 192 palette cases; Tandy bank/wrap/palette/blanking geometry");
}
