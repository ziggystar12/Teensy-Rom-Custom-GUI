#include "mpe4_render.h"
#include <string.h>

namespace mpe4 {
namespace {
constexpr uint8_t mapping[16] MPE4_RODATA={0,6,5,3,2,4,9,15,11,14,13,3,10,4,7,1};
constexpr uint8_t palette[16][3] MPE4_RODATA={{0,0,0},{255,255,255},{136,0,0},{170,255,238},
  {204,68,204},{0,204,85},{0,0,170},{238,238,119},{221,136,85},{102,68,0},
  {255,119,119},{51,51,51},{119,119,119},{170,255,102},{0,136,255},{187,187,187}};
constexpr uint8_t circleStart[8] MPE4_RODATA={0,1,4,9,16,25,37,50};
constexpr uint16_t circles[] MPE4_RODATA={0x8000,0,0xe000,0,0x7000,0xf800,0xf800,0xf800,0x7000,
  0x3800,0x7c00,0xfe00,0xfe00,0xfe00,0x7c00,0x3800,
  0x1c00,0x7f00,0xff80,0xff80,0xff80,0xff80,0xff80,0x7f00,0x1c00,
  0x0e00,0x3f80,0x7fc0,0x7fc0,0xffe0,0xffe0,0xffe0,0x7fc0,0x7fc0,0x3f80,0x1f00,0x0e00,
  0x0f80,0x3fe0,0x7ff0,0x7ff0,0xfff8,0xfff8,0xfff8,0xfff8,0xfff8,0x7ff0,0x7ff0,0x3fe0,0x0f80,
  0x07c0,0x1ff0,0x3ff8,0x7ffc,0x7ffc,0xfffe,0xfffe,0xfffe,0xfffe,0xfffe,0x7ffc,0x7ffc,0x3ff8,0x1ff0,0x07c0};
MPE4_CODE int16_t renderClamp(int16_t v,int16_t low,int16_t high) {return v<low?low:v>high?high:v;}
MPE4_CODE uint8_t pixel(const uint8_t *p,uint16_t at) {return (p[at>>1] >> ((at&1)?0:4))&15;}
MPE4_CODE void pixel(uint8_t *p,uint16_t at,uint8_t v) {
  if(at&1)p[at>>1]=(p[at>>1]&0xf0)|(v&15);else p[at>>1]=(p[at>>1]&15)|(v<<4);
}
MPE4_CODE uint32_t distance(uint8_t a,uint8_t b) {
  uint32_t d=0;for(uint8_t i=0;i<3;i++){int16_t v=int16_t(palette[a][i])-palette[b][i];d+=int32_t(v)*v;}return d;
}
MPE4_CODE void convertCell(const uint8_t *source,uint8_t *frame,uint16_t cell,const uint8_t *previous,bool protectHead=false) {
  uint8_t colors[4]={0,0,0,0},counts[16]={};
  for(uint8_t i=0;i<32;i++)counts[mapping[source[i]&15]]++;
  if(protectHead) {
    uint16_t faceColors=0;
    for(uint8_t i=0;i<32;i++)if(source[i]&128)faceColors|=uint16_t(1)<<mapping[source[i]&15];
    for(uint8_t color=1;color<16;color++)if(faceColors&(uint16_t(1)<<color))counts[color]+=8;
  }
  counts[0]=0;
  for(uint8_t pick=1;pick<4;pick++) {
    uint8_t best=0;for(uint8_t c=1;c<16;c++)if(counts[c]>counts[best])best=c;
    colors[pick]=counts[best]&&best?best:0;counts[best]=0;
  }
  uint8_t canonical[4];memcpy(canonical,colors,4);
  if(previous) {
    const uint8_t old[4]={0,uint8_t(previous[8000+cell]>>4),uint8_t(previous[8000+cell]&15),uint8_t(previous[9000+cell]&15)};
    uint8_t selected[4];memcpy(selected,colors,4);memcpy(colors,old,4);
    uint8_t used=1;
    // Pixel-count changes must not swap the meanings of the bitmap's codes.
    // Keep surviving colors in their exact former slots before assigning new
    // colors. Unused slots retain their old values, avoiding needless writes.
    for(uint8_t pick=1;pick<4;pick++)if(selected[pick])
      for(uint8_t slot=1;slot<4;slot++)if(!(used&(1u<<slot))&&selected[pick]==old[slot]) {
        used|=1u<<slot;selected[pick]=0;break;
      }
    for(uint8_t pick=1;pick<4;pick++)if(selected[pick])
      for(uint8_t slot=1;slot<4;slot++)if(!(used&(1u<<slot))) {
        colors[slot]=selected[pick];used|=1u<<slot;break;
      }
  }
  frame[8000+cell]=(colors[1]<<4)|colors[2];frame[9000+cell]=colors[3];
  uint8_t codes[16];memset(codes,255,sizeof(codes));
  for(uint8_t y=0;y<8;y++) {
    uint8_t bits=0;
    for(uint8_t x=0;x<4;x++) {
      uint8_t color=mapping[source[y*4+x]&15],best=codes[color];
      if(best==255) {
        best=0;uint32_t score=distance(color,canonical[0]);
        for(uint8_t c=1;c<4;c++){uint32_t d=distance(color,canonical[c]);if(d<score){score=d;best=c;}}
        const uint8_t chosen=canonical[best];
        for(best=0;best<3&&colors[best]!=chosen;best++){}
        codes[color]=best;
      }
      bits|=best<<(6-x*2);
    }
    frame[cell*8+y]=bits;
  }
}
}

MPE4_CODE bool Renderer::init(const Host &h,uint8_t *v,uint8_t *p,uint8_t *s,const uint8_t *f) {
  host=h;visual=v;priority=p;scratch=s;font=f;cacheLength=0;cacheType=255;valid=true;
  maximumFillSeeds=0;priorityBase=48;
  if(!v||!p||!s||!f||!h.resourceSize||!h.readResource)return false;
  memset(visual,0xff,PlaneBytes);memset(priority,0x44,PlaneBytes);return true;
}
MPE4_CODE bool Renderer::byte(uint8_t type,uint8_t id,uint32_t off,uint8_t &out) {
  if(!valid)return false;
  if(type!=cacheType || id!=cacheId || off<cacheOffset || off-cacheOffset>=cacheLength) {
    uint32_t size=host.resourceSize(host.context,type,id);
    if(off>=size){valid=false;return false;}
    cacheOffset=off;cacheLength=uint16_t(size-off>sizeof(cache)?sizeof(cache):size-off);
    cacheType=type;cacheId=id;
    if(!host.readResource(host.context,type,id,off,cache,cacheLength)){valid=false;cacheLength=0;return false;}
  }
  out=cache[off-cacheOffset];return true;
}
MPE4_CODE bool Renderer::word(uint8_t type,uint8_t id,uint32_t off,uint16_t &out) {
  uint8_t a,b;if(!byte(type,id,off,a)||!byte(type,id,off+1,b))return false;out=uint16_t(a)|(uint16_t(b)<<8);return true;
}
MPE4_CODE uint8_t Renderer::autoPriority(int16_t y) const {
  if(priorityBase>=168)return uint8_t(renderClamp(y/12+1,4,15));
  if(priorityBase==48)return uint8_t(renderClamp(y/12+1,4,15));
  if(y<int16_t(priorityBase))return 4;
  return uint8_t(renderClamp(5+(y-priorityBase)*10/(168-priorityBase),4,15));
}
MPE4_CODE uint8_t Renderer::priorityAt(uint8_t x,uint8_t y) const {
  return x<160&&y<168?pixel(priority,uint16_t(y)*160+x):0;
}
MPE4_CODE uint8_t Renderer::effectivePriority(int16_t x,int16_t y) const {
  for(;y<168;y++){uint8_t p=pixel(priority,uint16_t(y)*160+x);if(p>2)return p;}return 0;
}
MPE4_CODE void Renderer::put(int16_t x,int16_t y) {
  if(x<0||x>=160||y<0||y>=168)return;
  uint16_t at=uint16_t(y)*160+x;
  if(visualOn)pixel(visual,at,visualColor);
  if(priorityOn)pixel(priority,at,priorityColor);
}
MPE4_CODE void Renderer::line(int16_t x,int16_t y,int16_t xx,int16_t yy) {
  x=renderClamp(x,0,159);y=renderClamp(y,0,167);xx=renderClamp(xx,0,159);yy=renderClamp(yy,0,167);
  int16_t dx=xx>x?xx-x:x-xx,dy=yy>y?y-yy:yy-y,sx=x<xx?1:-1,sy=y<yy?1:-1,err=dx+dy;
  for(;;){put(x,y);if(x==xx&&y==yy)break;int16_t twice=err*2;if(twice>=dy){err+=dy;x+=sx;}if(twice<=dx){err+=dx;y+=sy;}}
}
MPE4_CODE bool Renderer::fillAllowed(int16_t x,int16_t y) const {
  if(x<0||x>=160||y<0||y>=168)return false;
  uint16_t at=uint16_t(y)*160+x;
  if(!priorityOn&&visualOn&&visualColor!=15)return pixel(visual,at)==15;
  if(priorityOn&&!visualOn&&priorityColor!=4)return pixel(priority,at)==4;
  return visualOn&&visualColor!=15&&pixel(visual,at)==15;
}
MPE4_CODE bool Renderer::fill(int16_t startX,int16_t startY) {
  if(!fillAllowed(startX,startY))return true;
  uint16_t count=0;
  auto push=[&](int16_t x,int16_t y)->bool {
    if(count>=FrameBytes/2)return false;
    uint16_t at=uint16_t(y)*160+x;
    scratch[count*2]=uint8_t(at);scratch[count*2+1]=uint8_t(at>>8);count++;
    if(count>maximumFillSeeds)maximumFillSeeds=count;
    return true;
  };
  if(!push(startX,startY))return false;
  while(count) {
    count--;uint16_t at=uint16_t(scratch[count*2])|(uint16_t(scratch[count*2+1])<<8);
    int16_t x=at%160,y=at/160;if(!fillAllowed(x,y))continue;
    while(fillAllowed(x-1,y))x--;
    bool up=false,down=false;
    for(;fillAllowed(x,y);x++) {
      put(x,y);
      if(fillAllowed(x,y-1)){if(!up&&!push(x,y-1))return false;up=true;}else up=false;
      if(fillAllowed(x,y+1)){if(!down&&!push(x,y+1))return false;down=true;}else down=false;
    }
  }
  return true;
}
MPE4_CODE void Renderer::pattern(int16_t x,int16_t y) {
  uint8_t size=patternCode&7,random=patternNumber|1;
  int16_t firstX=renderClamp(x*2-size,0,320-2*size)>>1,firstY=renderClamp(y-size,0,167-2*size);
  uint8_t circle=circleStart[size];
  for(int16_t py=firstY;py<firstY+size*2+1;py++,circle++) {
    int16_t px=firstX;
    for(uint8_t n=0;n<=(size*2+1)*2;n+=4,px++) {
      if((patternCode&16)||((0x8000>>(n>>1))&circles[circle])) {
        if(patternCode&32){uint8_t bit=random&1;random>>=1;if(bit)random^=0xb8;}
        if(!(patternCode&32)||(random&3)==2)put(px,py);
      }
    }
  }
}
MPE4_CODE bool Renderer::drawPicture(uint8_t id,bool overlay) {
  valid=true;cacheLength=0;visualOn=priorityOn=false;visualColor=15;priorityColor=4;patternCode=patternNumber=0;
  uint32_t size=host.resourceSize(host.context,Picture,id),at=0;if(!size)return false;
  if(!overlay){memset(visual,0xff,PlaneBytes);memset(priority,0x44,PlaneBytes);}
  auto parameter=[&](int16_t &value)->bool {uint8_t b;if(at>=size||!byte(Picture,id,at,b)||b>=0xf0)return false;at++;value=b;return true;};
  auto point=[&](int16_t &x,int16_t &y)->bool {return parameter(x)&&parameter(y);};
  while(at<size&&valid) {
    uint8_t op;if(!byte(Picture,id,at++,op))return false;
    if(op==0xff)return true;
    int16_t x,y,next,xx,yy;
    switch(op) {
      case 0xf0:if(!byte(Picture,id,at++,visualColor)||visualColor>15)return false;visualOn=true;break;
      case 0xf1:visualOn=false;break;
      case 0xf2:if(!byte(Picture,id,at++,priorityColor)||priorityColor>15)return false;priorityOn=true;break;
      case 0xf3:priorityOn=false;break;
      case 0xf4:case 0xf5:
        if(!point(x,y))break;
        put(x,y);
        while(parameter(next)) {
          if(op==0xf4){line(x,y,x,next);y=next;}else{line(x,y,next,y);x=next;}
          if(!parameter(next))break;
          if(op==0xf4){line(x,y,next,y);x=next;}else{line(x,y,x,next);y=next;}
        }break;
      case 0xf6:
        if(!point(x,y))break;
        put(x,y);
        while(point(xx,yy)){line(x,y,xx,yy);x=xx;y=yy;}break;
      case 0xf7:
        if(!point(x,y))break;
        put(x,y);
        while(parameter(next)) {int16_t dx=next>>4,dy=next&15;if(dx&8)dx=-(dx&7);if(dy&8)dy=-(dy&7);
          line(x,y,x+dx,y+dy);x+=dx;y+=dy;}break;
      case 0xf8:while(point(x,y))if(!fill(x,y))return false;break;
      case 0xf9:if(!byte(Picture,id,at++,patternCode))return false;break;
      case 0xfa:
        for(;;){if(patternCode&32){if(!parameter(next))break;patternNumber=uint8_t(next);}
          if(!point(x,y))break;
          pattern(x,y);}break;
      default:return false;
    }
  }
  return false; // A resource must terminate explicitly; truncated input cannot publish.
}

MPE4_CODE bool Renderer::cel(uint8_t view,uint8_t loop,uint8_t number,Cel &out) {
  uint8_t loops,cels;uint16_t lo,co;
  uint32_t size=host.resourceSize(host.context,View,view);
  if(size<5||!byte(View,view,2,loops)||loop>=loops||5u+loops*2u>size)return false;
  if(!word(View,view,5+loop*2,lo)||!byte(View,view,lo,cels)||number>=cels||uint32_t(lo)+1+cels*2u>size)return false;
  if(!word(View,view,lo+1+number*2,co))return false;
  uint32_t at=uint32_t(lo)+co;uint8_t w,h,f;
  if(!byte(View,view,at,w)||!byte(View,view,at+1,h)||!byte(View,view,at+2,f)||!w||!h)return false;
  out={at+3,size,view,w,h,uint8_t(f&15),loops,cels,bool((f&128)&&((f>>4)&7)!=loop)};return true;
}
MPE4_CODE bool Renderer::viewCelInfo(uint8_t view,uint8_t loop,uint8_t number,CelInfo *out) {
  valid=true;Cel c;if(!out||!cel(view,loop,number,c))return false;*out={c.width,c.height,c.loops,c.cels};return true;
}
MPE4_CODE bool Renderer::celRow(const Cel &c,uint8_t row,uint8_t *pixels) {
  if(row>=c.height)return false;
  uint32_t at=c.offset;
  memset(pixels,c.transparent,c.width);
  for(uint16_t y=0;y<=row;y++) {
    uint16_t x=0;
    for(;;) {
      uint8_t run;if(!byte(View,c.view,at++,run))return false;if(!run)break;
      uint8_t length=run&15;if(!length||x+length>c.width)return false;
      if(y==row)for(uint8_t n=0;n<length;n++)pixels[c.mirrored?c.width-1-(x+n):x+n]=run>>4;
      x+=length;
    }
  }
  return true;
}
MPE4_CODE bool Renderer::addToPicture(uint8_t view,uint8_t loop,uint8_t number,
    uint8_t x,uint8_t y,uint8_t p,uint8_t margin) {
  valid=true;Cel c;if(!cel(view,loop,number,c)||p>15)return false;if(!p)p=autoPriority(y);
  uint8_t row[255];int16_t top=int16_t(y)-c.height+1;
  for(uint16_t cy=0;cy<c.height;cy++) {
    int16_t yy=top+cy;if(yy<0||yy>=168)continue;
    if(!celRow(c,uint8_t(cy),row))return false;
    for(uint16_t cx=0;cx<c.width&&cx+x<160;cx++) {
      if(row[cx]==c.transparent||effectivePriority(x+cx,yy)>p)continue;
      uint16_t at=uint16_t(yy)*160+x+cx;pixel(visual,at,row[cx]);if(pixel(priority,at)>2)pixel(priority,at,p);
    }
  }
  // AGI add.to.pic's margin0..3 draws a control rectangle in the baseline's
  // priority band; preserve those control values for native movement tests.
  if(margin<4&&x<160&&y<168) {
    int16_t h=1;while(h<c.height&&int16_t(y)-h>=0&&autoPriority(y-h)==autoPriority(y))h++;
    for(uint16_t cx=0;cx<c.width&&x+cx<160;cx++) {
      pixel(priority,uint16_t(y)*160+x+cx,margin);
      pixel(priority,uint16_t(y-h+1)*160+x+cx,margin);
    }
    for(int16_t cy=y-h+1;cy<=y;cy++) {
      pixel(priority,uint16_t(cy)*160+x,margin);
      if(uint16_t(x)+c.width<=160)pixel(priority,uint16_t(cy)*160+x+c.width-1,margin);
    }
  }
  return valid;
}

MPE4_CODE bool Renderer::parserSplit(const State &s) {
  return s.graphics&&s.inputEnabled&&s.inputLength&&s.modal==NoModal&&s.inputRow<25;
}

MPE4_CODE bool Renderer::render(const State &s,uint8_t frame[FrameBytes],const uint8_t *previousFrame,bool refineHead) {
  if(!frame||!font||!visual||!priority)return false;
  if(previousFrame==frame)return false;
  valid=true;priorityBase=s.priorityBase<168?s.priorityBase:48;
  memset(frame,0,FrameBytes);
  if(!s.graphics) {
    for(uint16_t i=0;i<1000;i++) {
      uint8_t ch=s.text[i],attr=s.attributes[i];
      if((ch&0xf0)==WindowMarker) {
        for(uint8_t y=0;y<8;y++)frame[i*8+y]=
          (((ch&WindowTop)&&(y==0||y==2))||((ch&WindowBottom)&&(y==5||y==7)))?255:
          uint8_t(((ch&WindowLeft)?0xf0:0)|((ch&WindowRight)?0x0f:0));
        frame[8000+i]=0x21;continue;
      }
      ch&=127;if(!ch)ch=32;
      memcpy(frame+i*8,font+ch*8,8);frame[8000+i]=(mapping[attr&15]<<4)|mapping[attr>>4];
    }
    return true;
  }
  struct Actor {Cel c;int16_t x,top,sort;uint8_t p,order;};Actor actors[33];uint8_t actorCount=0;
  if(s.pictureVisible)for(uint8_t i=0;i<32;i++) {
    const Object &o=s.objects[i];if((o.flags&(Animated|Drawn))!=(Animated|Drawn))continue;
    Cel c;if(!cel(o.view,o.loop,o.cel,c))return false;
    uint8_t p=(o.flags&FixedPriority)?o.priority:autoPriority(o.y);
    int16_t order=o.y;
    if(o.flags&FixedPriority){order=0;while(order<168&&autoPriority(order)<p)order++;}
    actors[actorCount++]={c,o.x,int16_t(int16_t(o.y)-c.height+1),order,p,i};
  }
  if(s.showObject) {
    Cel c;if(!cel(s.showObjectView,0,0,c))return false;
    actors[actorCount++]={c,int16_t((159-int16_t(c.width))/2),int16_t(168-c.height),32767,15,255};
  }
  for(uint8_t i=1;i<actorCount;i++){Actor a=actors[i];uint8_t j=i;while(j&&actors[j-1].sort>a.sort){actors[j]=actors[j-1];j--;}actors[j]=a;}
  uint8_t pixels[32],row[255];
  const int16_t top=s.graphicsTop,shake=(s.shakeTicks&1)?2:0;
  const bool split=parserSplit(s),parser=s.inputEnabled&&s.modal==NoModal&&s.inputRow<25;
  for(uint16_t cell=0;cell<1000;cell++) {
    if(split&&cell>=920) {
      // The row above the hires strip is a black raster-switch separator.
      // Its blank scanlines tolerate PAL/NTSC IRQ entry and sprite steals.
      if(cell>=960) {
        uint16_t source=uint16_t(s.inputRow)*40+cell-960;
        uint8_t ch=s.text[source]&127;if(!ch)ch=32;
        memcpy(frame+cell*8,font+ch*8,8);
        uint8_t attr=s.attributes[source];frame[8000+cell]=(mapping[attr&15]<<4)|mapping[attr>>4];
      }
      continue;
    }
    int16_t sx=(cell%40)*4,sy=(cell/40)*8;
    bool protectHead=false;
    for(uint8_t py=0;py<8;py++)for(uint8_t px=0;px<4;px++) {
      int16_t y=sy+py-top-shake,x=sx+px-shake;
      pixels[py*4+px]=s.pictureVisible&&x>=0&&x<160&&y>=0&&y<168?pixel(visual,uint16_t(y)*160+x):0;
    }
    for(uint8_t a=0;a<actorCount;a++) {
      const Actor &actor=actors[a];int16_t left=actor.x+shake,upper=actor.top+top+shake;
      if(sx+4<=left||sx>=left+actor.c.width||sy+8<=upper||sy>=upper+actor.c.height)continue;
      for(uint8_t py=0;py<8;py++) {
        int16_t cy=sy+py-upper;if(cy<0||cy>=actor.c.height)continue;
        if(!celRow(actor.c,uint8_t(cy),row))return false;
        for(uint8_t px=0;px<4;px++) {
          int16_t cx=sx+px-left,xx=sx+px-shake,yy=sy+py-top-shake;
          if(cx<0||cx>=actor.c.width||xx<0||xx>=160||yy<0||yy>=168||row[cx]==actor.c.transparent)continue;
          if(effectivePriority(xx,yy)<=actor.p) {
            pixels[py*4+px]=row[cx];
            // Only an explicitly requested idle refinement examines the head.
            // Bit7 is cell-local scratch and is removed by palette conversion;
            // later actors/text overwrite it, preserving authored occlusion.
            if(refineHead&&actor.order==0&&cy<(actor.c.height+2)/3) {
              pixels[py*4+px]|=128;protectHead=true;
            }
          }
        }
      }
    }
    // The ordinary input line is displayed by the bottom hires strip. Empty
    // edits disappear; authored text and synchronous input retain their rows.
    if(s.text[cell]&&!(parser&&cell/40==s.inputRow)) {
      protectHead=false;
      uint8_t ch=s.text[cell],attr=s.attributes[cell];
      if((ch&0xf0)==WindowMarker) {
        // Exact C64 UI window strokes: $a5/$5a verticals, with $aa on
        // top scanlines 0/2 and bottom scanlines 5/7. EGA4 maps to red2.
        for(uint8_t py=0;py<8;py++)for(uint8_t px=0;px<4;px++) {
          bool red=((ch&WindowLeft)&&px<2)||((ch&WindowRight)&&px>=2)||
            ((ch&WindowTop)&&(py==0||py==2))||((ch&WindowBottom)&&(py==5||py==7));
          pixels[py*4+px]=red?4:15;
        }
        convertCell(pixels,frame,cell,previousFrame);continue;
      }
      ch&=127;
      // Match the established C64 bitmap UI: three pixels from the six useful
      // ROM columns, followed by one blank pixel. Use the uppercase glyph for
      // compact gameplay text; hires text above retains its original case.
      if(ch>='a'&&ch<='z')ch-=32;
      for(uint8_t py=0;py<8;py++)for(uint8_t px=0;px<4;px++)
        pixels[py*4+px]=(px<3&&(font[ch*8+py]&(0x60>>(px*2))))?(attr&15):(attr>>4);
    }
    convertCell(pixels,frame,cell,previousFrame,protectHead);
  }
  return valid;
}
}
