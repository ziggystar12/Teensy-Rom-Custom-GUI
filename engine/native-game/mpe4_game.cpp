#include "mpe4_game.h"
#include <string.h>

namespace mpe4 {
static const uint8_t arity[] MPE4_RODATA = {
  0,1,1,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,1,1,
  1,1,0,1,1,3,3,3,3,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,1,2,1,1,1,1,1,1,
  1,1,1,1,1,3,1,1,1,2,1,2,2,1,1,2,2,5,5,3,1,1,2,2,1,1,4,0,1,1,1,2,
  2,2,1,2,0,1,1,3,3,3,0,0,1,2,1,3,0,0,2,5,2,1,2,0,0,3,7,7,0,0,0,0,
  0,1,3,0,0,1,1,0,0,0,0,0,0,0,1,1,1,0,0,3,3,0,3,4,4,1,5,2,1,2,0,1,
  1,0,1,0,0,2,2,2,2,0,1,0,0,0,1,1,0,1,0,4,2,0,0
};
static const char gameSaveFailedText[] MPE4_RODATA="The game could not be saved. Check the SD card and try again.";
static const char gameRestoreFailedText[] MPE4_RODATA="No usable saved game was found. You can continue playing.";
static const char gamePausedText[] MPE4_RODATA="Game paused. Press Enter to continue.";
static const char gamePriorityText[] MPE4_RODATA="Priority map inspection is unavailable.";
static const char gameRestartText[] MPE4_RODATA="Restart the game? Enter: restart  Escape: continue";
static MPE4_CODE uint16_t le16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
static MPE4_CODE uint8_t clamp(int n, int low, int high) { return n < low ? low : n > high ? high : n; }
static MPE4_CODE int distance(int a, int b) { return a > b ? a-b : b-a; }
static MPE4_CODE bool asciiDigit(uint8_t c) { return c>='0'&&c<='9'; }
static MPE4_CODE uint8_t asciiLower(uint8_t c) { return c>='A'&&c<='Z'?c+('a'-'A'):c; }
static MPE4_CODE bool asciiAlnum(uint8_t c) { return asciiDigit(c)||(asciiLower(c)>='a'&&asciiLower(c)<='z'); }
static MPE4_CODE bool normalizedEqual(const char *a,const char *b) {
  // AGI strings are bounded ASCII. Skip punctuation without libc locale tables.
  unsigned ai=0,bi=0;
  for(;;){while(ai<40&&a[ai]&&!asciiAlnum((uint8_t)a[ai]))ai++;
    while(bi<40&&b[bi]&&!asciiAlnum((uint8_t)b[bi]))bi++;
    const uint8_t ac=ai<40?asciiLower((uint8_t)a[ai]):0;
    const uint8_t bc=bi<40?asciiLower((uint8_t)b[bi]):0;
    if(ac!=bc)return false;if(!ac)return true;ai++;bi++;}
}
static MPE4_CODE uint8_t directionTo(int x, int y, int tx, int ty, int tolerance=0) {
  const int dx = distance(x,tx) <= tolerance ? 0 : tx>x ? 1 : -1;
  const int dy = distance(y,ty) <= tolerance ? 0 : ty>y ? 1 : -1;
  static const uint8_t dirs[3][3] MPE4_RODATA={{8,1,2},{7,0,3},{6,5,4}};
  return dirs[dy+1][dx+1];
}
static MPE4_CODE void copyText(char *to, const char *from, size_t n) {
  if (!n) return; size_t i=0; while (i+1<n && from[i]) { to[i]=from[i]; ++i; } to[i]=0;
}
static MPE4_CODE void appendText(char *to,size_t capacity,const char *from) {
  const size_t used=strlen(to);if(used<capacity)copyText(to+used,from,capacity-used);
}
static MPE4_CODE void unsignedText(char *to,size_t capacity,unsigned value,unsigned width=0) {
  char digits[10];unsigned count=0;do{digits[count++]=char('0'+value%10);value/=10;}while(value&&count<sizeof(digits));
  unsigned at=0;while(width>count&&at+1<capacity){to[at++]='0';width--;}
  while(count&&at+1<capacity)to[at++]=digits[--count];if(capacity)to[at]=0;
}
static MPE4_CODE void appendUnsigned(char *to,size_t capacity,unsigned value) {
  char text[11];unsignedText(text,sizeof(text),value);appendText(to,capacity,text);
}
MPE4_CODE bool Game::flag(uint8_t n) const { return (state.flags[n>>3] & (1u<<(n&7))) != 0; }
MPE4_CODE void Game::setFlag(uint8_t n, bool value) {
  if(value) state.flags[n>>3] |= 1u<<(n&7); else state.flags[n>>3] &= ~(1u<<(n&7));
}
MPE4_CODE bool Game::fail(Error e, uint8_t op) {
  if (!state.error) { state.error=e; state.errorLogic=state.logic; state.errorOpcode=op;
    state.errorIp=state.callDepth ? state.calls[state.callDepth-1].ip : 0; }
  state.running=false; return false;
}
MPE4_CODE bool Game::read(uint8_t type,uint8_t id,uint32_t off,uint8_t *data,uint16_t n) {
  const uint32_t size=host.resourceSize(host.context,type,id);
  if(!size) return fail(ResourceMissing);
  if(off>size || n>size-off) return fail(ResourceBounds);
  return host.readResource(host.context,type,id,off,data,n) || fail(HostFailure);
}
MPE4_CODE bool Game::start(const Host &h, bool skip, uint32_t seed) {
  return reset(h,skip,seed,false);
}
MPE4_CODE bool Game::reset(const Host &h,bool skip,uint32_t seed,bool restarting) {
  host=h;memset(queuedControllers,0,sizeof(queuedControllers));
  if(restarting){
    // Restart retains strings and menu definitions in place. SQ1's restart
    // branch bypasses menu construction; no large temporary copy is needed.
    const uint8_t menuCount=state.menuCount,menuItems=state.menuItemCount;
    const size_t after=offsetof(State,strings)+sizeof(state.strings);
    memset(&state,0,offsetof(State,strings));
    memset((uint8_t*)&state+after,0,offsetof(State,menuTitles)-after);
    memset((uint8_t*)&state+offsetof(State,bindings),0,sizeof(state)-offsetof(State,bindings));
    state.menuCount=menuCount;state.menuItemCount=menuItems;
    for(auto &item:state.menuItems)item.enabled=1;
  }else memset(&state,0,sizeof(state));
  if(!host.resourceSize || !host.readResource || !host.drawPicture ||
     !host.viewCelInfo || !host.addToPicture || !host.priorityAt) return fail(HostFailure);
  state.signature=0x3153344d; state.random=seed ? seed:1;
  state.running=true; state.playerControl=true; state.inputEnabled=true;
  state.graphics=true; state.foreground=15; state.cursor='_'; state.horizon=36;
  state.priorityBase=48; state.graphicsTop=8; state.inputRow=22;
  state.scriptEnabled=true; state.scriptLimit=50; state.menuAllowed=true;
  state.skipPresentedIntro=skip; state.vars[8]=255; state.vars[10]=2;
  state.vars[24]=40; state.vars[26]=3; setFlag(5,true); setFlag(9,true);
  memset(state.attributes,15,1000);
  for(auto &o:state.objects) { o.stepSize=o.stepTime=o.cycleTime=1; o.flags=Motion; }
  uint8_t count[2]; if(!read(Objects,0,0,count,2))return false;
  state.objectCount=le16(count); if(state.objectCount>256)return fail(ObjectBounds);
  if(!read(Objects,0,2,state.inventory,state.objectCount))return false;
  state.firstScan=true; state.frameDirty=state.textDirty=true;
  return true;
}
MPE4_CODE bool Game::restartGame() {
  Host h=host;const uint32_t seed=state.random;
  if(host.stopSound)host.stopSound(host.context);
  if(!reset(h,false,seed,true))return false;setFlag(6,true);
  state.inScan=true;return pushLogic(0);
}
MPE4_CODE bool Game::pushLogic(uint8_t id) {
  if(state.callDepth>=16)return fail(StackOverflow);
  uint8_t data[2]; if(!read(Logic,id,0,data,2))return false;
  const uint16_t end=le16(data);
  if(!end || uint32_t(end)+2>host.resourceSize(host.context,Logic,id) || state.scanStart[id]>=end)
    return fail(BadLogic);
  state.calls[state.callDepth++]={state.scanStart[id],end,id}; state.logic=id; return true;
}
MPE4_CODE bool Game::fetch(uint8_t &v) {
  if(!state.callDepth)return fail(BadLogic);
  Call &c=state.calls[state.callDepth-1]; state.logic=c.logic;
  if(c.ip>=c.end)return fail(BadLogic);
  return read(Logic,c.logic,2+c.ip++,&v,1);
}
MPE4_CODE bool Game::fetch16(uint16_t &v) {
  uint8_t a,b; if(!fetch(a)||!fetch(b))return false; v=a|uint16_t(b)<<8; return true;
}
MPE4_CODE bool Game::test(uint8_t op,bool evaluate,bool &result) {
  static const uint8_t counts[] MPE4_RODATA={0,2,2,2,2,2,2,1,1,1,2,5,1,0,0,2,5,5,5,0};
  if(op==0 || op>=sizeof(counts))return fail(UnsupportedTest,op);
  uint8_t a[5]={}; result=false;
  if(op==14) {
    uint8_t n; if(!fetch(n))return false;
    bool match=evaluate && flag(2) && !flag(4); bool rest=false;
    for(unsigned i=0;i<n;i++) { uint16_t word;if(!fetch16(word))return false;
      if(rest)continue; if(word==9999) {rest=true;continue;}
      if(i>=state.wordCount || (word!=1 && state.words[i]!=word))match=false;
    }
    if(!rest && n!=state.wordCount)match=false;
    if(match)setFlag(4,true); result=match; return true;
  }
  for(unsigned i=0;i<counts[op];i++)if(!fetch(a[i]))return false;
  if(!evaluate)return true;
  const uint8_t *v=state.vars;
  switch(op) {
    case 1: result=v[a[0]]==a[1];break; case 2:result=v[a[0]]==v[a[1]];break;
    case 3:result=v[a[0]]<a[1];break;case 4:result=v[a[0]]<v[a[1]];break;
    case 5:result=v[a[0]]>a[1];break;case 6:result=v[a[0]]>v[a[1]];break;
    case 7:result=flag(a[0]);break;case 8:result=flag(v[a[0]]);break;
    case 9:if(a[0]>=state.objectCount)return fail(ObjectBounds);result=state.inventory[a[0]]==255;break;
    case 10:if(a[0]>=state.objectCount)return fail(ObjectBounds);result=state.inventory[a[0]]==v[a[1]];break;
    case 12:result=(state.controllers[a[0]>>3]&(1u<<(a[0]&7)))!=0;break;
    case 13:result=state.key!=0 || state.keyScan!=0;break;
    case 15: {
      if(a[0]>=25||a[1]>=25)return fail(StringBounds);
      result=normalizedEqual(state.strings[a[0]],state.strings[a[1]]);break;
    }
    case 11:case 16:case 17:case 18: {
      if(a[0]>=32)return fail(ObjectBounds); const Object &o=state.objects[a[0]];
      int x=o.x;if(op==17)x+=o.width/2;if(op==18)x+=o.width?o.width-1:0;
      result=x>=a[1]&&x<=a[3]&&o.y>=a[2]&&o.y<=a[4];
      if(op==16)result=result && o.x+o.width-1<=a[3]; break;
    }
    case 19:result=false;break;
    default:return fail(UnsupportedTest,op);
  } return true;
}
MPE4_CODE bool Game::expression(bool &result) {
  bool all=true,inOr=false,orValue=false,neg=false,orTerm=false;
  for(;;) { uint8_t op;if(!fetch(op))return false;
    if(op==0xff) { if(inOr||neg)return fail(BadLogic); result=all;return true; }
    if(op==0xfd){neg=!neg;continue;}
    if(op==0xfc){if(neg)return fail(BadLogic);if(inOr){if(!orTerm)return fail(BadLogic);all=all&&orValue;inOr=false;}
      else {inOr=true;orValue=false;orTerm=false;}continue;}
    bool term=false,evaluate=all&&(!inOr||!orValue);if(!test(op,evaluate,term))return false;
    if(neg&&evaluate)term=!term;neg=false;
    if(inOr){orTerm=true;orValue=orValue||term;}else all=all&&term;
  }
}
MPE4_CODE bool Game::newRoom(uint8_t room) {
  if(state.skipPresentedIntro && room==67){room=69;state.skipPresentedIntro=false;}
  if(!host.resourceSize(host.context,Logic,room))return fail(ResourceMissing);
  if(host.stopSound)host.stopSound(host.context);
  if(state.soundActive)setFlag(state.soundFlag,true);state.soundActive=false;
  state.vars[1]=state.vars[0];state.vars[0]=room;state.vars[16]=state.objects[0].view;
  const uint8_t edge=state.vars[2]; Object &ego=state.objects[0];
  if(edge==1)ego.y=167;else if(edge==2)ego.x=0;else if(edge==3)ego.y=state.horizon+1;
  else if(edge==4)ego.x=160-(ego.width?ego.width:1);
  for(auto &o:state.objects){o.flags &= ~(Animated|Drawn);o.flags|=Updating|Cycling|Motion;
    o.flags &= ~(FixedLoop|FixedPriority|IgnoreHorizon|IgnoreObjects|IgnoreBlocks|OnWater|OnLand);
    o.stepTime=o.cycleTime=o.stepSize=1;o.stepCounter=o.cycleCounter=0;o.motionMode=o.cycleMode=0;}
  state.horizon=36;state.blockActive=false;state.playerControl=true;
  state.vars[2]=state.vars[4]=state.vars[5]=state.vars[6]=0;
  ego.direction=0;setFlag(0,false);setFlag(2,false);setFlag(3,false);setFlag(4,false);setFlag(5,true);
  // The editable command survives a room boundary (SQ1's Orat encounter relies
  // on pretyping before entry). Only submitted parser words are discarded.
  state.wordCount=0;
  state.callDepth=0;state.roomPending=false;state.inScan=true;
  state.visualLength=0;state.frameDirty=true;state.pictureVisible=false;
  memset(state.scanStart,0,sizeof(state.scanStart));
  return pushLogic(0);
}
MPE4_CODE bool Game::celInfo(uint8_t index) {
  if(index>=32)return fail(ObjectBounds);Object &o=state.objects[index];CelInfo c={};
  if(!host.viewCelInfo(host.context,o.view,o.loop,o.cel,&c))return fail(HostFailure);
  if(!c.width||!c.height||!c.loops||!c.cels)return fail(ResourceBounds);
  if(c.width>160||c.height>168)return fail(ResourceBounds);
  o.width=c.width;o.height=c.height;
  o.x=clamp(o.x,0,160-o.width);
  unsigned minimumY=o.height-1;
  if(!(o.flags&IgnoreHorizon)&&minimumY<=state.horizon)minimumY=state.horizon+1;
  o.y=clamp(o.y,minimumY,167);
  if(!index)updateEgoControls();
  return true;
}
MPE4_CODE bool Game::inventoryName(uint8_t id,char *out,uint8_t n) {
  if(id>=state.objectCount)return fail(ObjectBounds);uint32_t off=2+state.objectCount;
  unsigned item=0;uint8_t c;unsigned at=0;
  while(item<=id) {if(!read(Objects,0,off++,&c,1))return false;
    if(item==id&&at+1<n)out[at++]=c;if(!c)item++;}
  if(n)out[at<n?at:n-1]=0;return true;
}
MPE4_CODE bool Game::message(uint8_t logic,uint8_t number,char *out,uint16_t capacity) {
  return messageImpl(logic,number,out,capacity,0);
}
MPE4_CODE bool Game::messageImpl(uint8_t logic,uint8_t number,char *out,uint16_t capacity,uint8_t depth) {
  if(depth>8)return fail(BadLogic);
  if(!capacity)return fail(ResourceBounds);out[0]=0;if(!number)return true;
  uint8_t header[3];if(!read(Logic,logic,0,header,2))return false;
  uint32_t base=2+le16(header);if(!read(Logic,logic,base,header,3))return false;
  if(number>header[0])return fail(BadLogic);
  uint8_t ptr[2];if(!read(Logic,logic,base+3+2u*(number-1),ptr,2))return false;
  if(!le16(ptr))return true;uint32_t off=base+1+le16(ptr);unsigned at=0;
  // Message references are expanded by a bounded local recursion counter.
  // Games never provide pointers; all characters retain resource bounds checks.
  while(at+1<capacity) {uint8_t c;if(!read(Logic,logic,off++,&c,1))return false;
    if(!c)break;
    if(c=='\\') {if(!read(Logic,logic,off++,&c,1))return false;out[at++]=c=='n'?'\n':c;continue;}
    if(c!='%'){out[at++]=c;continue;}
    uint8_t kind;if(!read(Logic,logic,off++,&kind,1))return false;
    unsigned id=0,digits=0;uint8_t next;
    while(true){if(!read(Logic,logic,off,&next,1))return false;if(next<'0'||next>'9')break;
      off++;id=id*10+next-'0';if(++digits>3||id>255)return fail(BadLogic);}
    char value[128]={};
    if(kind=='v'){unsigned width=0;if(next=='|'){off++;while(true){if(!read(Logic,logic,off,&next,1))return false;
      if(next<'0'||next>'9')break;off++;width=width*10+next-'0';if(width>40)return fail(BadLogic);}}
      unsignedText(value,sizeof(value),state.vars[id],width);}
    else if(kind=='s'){if(id>=25)return fail(StringBounds);copyText(value,state.strings[id],sizeof(value));}
    else if(kind=='w'){if(id>0&&id<=state.wordCount){unsigned n=state.wordLength[id-1];
      memcpy(value,state.parsedText+state.wordOffset[id-1],n);value[n]=0;}}
    else if(kind=='o'){if(!inventoryName((uint8_t)id,value,sizeof(value)))return false;}
    else if(kind=='m'||kind=='g') {
      if(!messageImpl(kind=='g'?0:logic,id,out+at,capacity-at,depth+1))return false;
      at+=strlen(out+at);continue;
    } else {out[at++]='%';if(at+1<capacity)out[at++]=kind;continue;}
    for(unsigned i=0;value[i]&&at+1<capacity;i++)out[at++]=value[i];
  }out[at]=0;return true;
}
MPE4_CODE bool Game::parse(const char *input) {
  char clean[81];unsigned n=0;bool space=true;
  for(unsigned i=0;input[i]&&n<80;i++){const uint8_t c=(uint8_t)input[i];
    if(c=='\''||c=='`'||c=='-'||c=='\\'||c=='"')continue;
    if(asciiAlnum(c)){clean[n++]=(char)asciiLower(c);space=false;}
    else if(!space){clean[n++]=' ';space=true;}}
  if(n&&clean[n-1]==' ')n--;clean[n]=0;copyText(state.parsedText,clean,sizeof(state.parsedText));
  state.wordCount=0;state.vars[9]=0;setFlag(2,false);setFlag(4,false);
  uint8_t h[3];if(!read(Vocabulary,0,0,h,2))return false;unsigned count=le16(h);
  if(count>8192)return fail(BadVocabulary);unsigned start=0;
  while(start<n&&state.wordCount<20) {
    uint32_t off=2;unsigned bestLength=0;uint16_t bestId=0;
    for(unsigned i=0;i<count;i++){if(!read(Vocabulary,0,off,h,3))return false;off+=3;
      unsigned len=h[2];if(!len||len>80)return fail(BadVocabulary);char phrase[81];
      if(!read(Vocabulary,0,off,(uint8_t*)phrase,len))return false;off+=len;
      if(len>bestLength&&start+len<=n&&!memcmp(clean+start,phrase,len)&&
         (start+len==n||clean[start+len]==' ')){bestLength=len;bestId=le16(h);}}
    bool unknown=!bestLength;if(unknown){while(start+bestLength<n&&clean[start+bestLength]!=' ')bestLength++;}
    if(bestId||unknown){unsigned w=state.wordCount++;state.words[w]=bestId;
      state.wordOffset[w]=start;state.wordLength[w]=bestLength;
      if(unknown&&!state.vars[9])state.vars[9]=w+1;}
    if(unknown)break;
    start+=bestLength;while(start<n&&clean[start]==' ')start++;
  }setFlag(2,state.wordCount!=0);return true;
}

MPE4_CODE void Game::textAt(uint8_t row,uint8_t col,const char *s,uint8_t width) {
  if(row>=25||col>=40)return;unsigned start=col;unsigned right=col+width;
  if(right>40)right=40;
  while(*s&&row<25){uint8_t c=*s++;
    if(c=='\r')continue;if(c=='\n'){row++;col=start;continue;}
    if(col>=right){row++;col=start;if(row>=25)break;}
    // Wrap a word intact when it fits the line but not its remainder.
    if(c!=' '&&(col==start||s[-2]==' ')){unsigned length=1;while(s[length-1]&&s[length-1]!=' '&&s[length-1]!='\n')length++;
      if(length<=width&&col+length>right){row++;col=start;if(row>=25)break;}}
    state.text[row*40+col]=c;state.attributes[row*40+col]=state.foreground|(state.background<<4);col++;
  }state.textDirty=state.frameDirty=true;
}
MPE4_CODE void Game::clearLines(uint8_t first,uint8_t last,uint8_t color) {
  if(first>24)return;if(last>24)last=24;if(last<first)return;
  for(unsigned row=first;row<=last;row++){memset(state.text+row*40,state.graphics?0:' ',40);
    memset(state.attributes+row*40,(color<<4)|state.foreground,40);}
  state.textDirty=state.frameDirty=true;
}
MPE4_CODE void Game::drawInput() {
  if(!state.inputEnabled&&state.modal!=StringInput&&state.modal!=NumberInput)return;
  uint8_t row=state.modal==StringInput||state.modal==NumberInput?state.modalRow:state.inputRow;
  uint8_t col=state.modal==StringInput||state.modal==NumberInput?state.modalColumn:0;
  if(row>=25||col>=40)return;
  memset(state.text+row*40+col,' ',40-col);
  memset(state.attributes+row*40+col,state.foreground|(state.background<<4),40-col);
  if(state.modal==StringInput||state.modal==NumberInput)textAt(row,col,state.input);
  else {textAt(row,col,state.strings[0]);unsigned prompt=strlen(state.strings[0]);
    if(prompt<40){col=prompt;textAt(row,col,state.input);}}
  unsigned cursor=col+state.inputLength;if(cursor<40)state.text[row*40+cursor]=state.cursor;
  state.frameDirty=state.textDirty=true;
}
MPE4_CODE void Game::drawStatus() {
  if(!state.statusVisible||state.statusRow>=25)return;char s[41];
  copyText(s," Score: ",sizeof(s));appendUnsigned(s,sizeof(s),state.vars[3]);appendText(s,sizeof(s)," of ");
  appendUnsigned(s,sizeof(s),state.vars[7]);appendText(s,sizeof(s),"        Sound: ");appendText(s,sizeof(s),flag(9)?"on":"off");
  uint8_t fg=state.foreground,bg=state.background;state.foreground=0;state.background=15;
  memset(state.text+40*state.statusRow,' ',40);memset(state.attributes+40*state.statusRow,0xf0,40);
  textAt(state.statusRow,0,s);state.foreground=fg;state.background=bg;
}
MPE4_CODE void Game::showMessage(const char *s,uint8_t row,uint8_t col,uint8_t width) {
  if(!state.modalSaved){memcpy(state.savedText,state.text,1000);memcpy(state.savedAttributes,state.attributes,1000);state.modalSaved=true;}
  if(!width)width=30;if(width>38)width=38;
  unsigned lines=1,column=0;for(const char *p=s;*p;p++){
    if(*p=='\r')continue;if(*p=='\n'){lines++;column=0;continue;}
    if(column>=width){lines++;column=0;}
    if(*p!=' '&&(p==s||p[-1]==' ')){unsigned length=0;while(p[length]&&p[length]!=' '&&p[length]!='\n')length++;
      if(length<=width&&column+length>width){lines++;column=0;}}
    column++;}
  if(lines>21)lines=21;if(!row)row=(25-lines)/2;if(!col)col=(40-width)/2;
  if(row>23)row=23;if(row+lines>24)lines=24-row;if(col+width>39)col=39-width;
  const unsigned left=col?col-1:0,top=row?row-1:0,right=col+width,bottom=row+lines;
  for(unsigned y=top;y<=bottom;y++)for(unsigned x=left;x<=right;x++){
    const uint8_t edges=(y==top?WindowTop:0)|(y==bottom?WindowBottom:0)|
      (x==left?WindowLeft:0)|(x==right?WindowRight:0);
    state.text[y*40+x]=edges?WindowMarker|edges:' ';
    state.attributes[y*40+x]=0xf0;}
  uint8_t fg=state.foreground,bg=state.background;state.foreground=0;state.background=15;
  textAt(row,col,s,width);state.foreground=fg;state.background=bg;
  state.modal=Message;state.modalTicks=uint16_t(state.vars[21])*30;state.vars[21]=0;
  state.key=state.keyScan=0;state.frameDirty=true;
  if(flag(15)){state.modal=NoModal;setFlag(15,false);}
}
MPE4_CODE void Game::closeModal() {
  if(state.modalSaved){memcpy(state.text,state.savedText,1000);memcpy(state.attributes,state.savedAttributes,1000);}
  state.modalSaved=false;state.modal=NoModal;state.modalTicks=0;state.showObject=false;
  state.key=state.keyScan=0;state.frameDirty=state.textDirty=true;
}
MPE4_CODE void Game::inventoryMenu() {
  showMessage("",2,2,36);state.modal=Inventory;
  memset(state.text,' ',1000);memset(state.attributes,0xf0,1000);
  uint8_t fg=state.foreground,bg=state.background;state.foreground=0;state.background=15;
  textAt(1,9,"You are carrying:");unsigned row=3;state.menuSelection=255;
  for(unsigned i=0;i<state.objectCount&&row<23;i++)if(state.inventory[i]==255){char s[64];
    if(!inventoryName(i,s,sizeof(s)))return;textAt(row++,3,s);if(state.menuSelection==255)state.menuSelection=i;}
  if(row==3)textAt(3,3,"Nothing");textAt(24,1,"Enter: select    Escape: return");
  state.foreground=fg;state.background=bg;
}
MPE4_CODE void Game::renderMenu() {
  if(!state.modalSaved){memcpy(state.savedText,state.text,1000);memcpy(state.savedAttributes,state.attributes,1000);state.modalSaved=true;}
  memcpy(state.text,state.savedText,1000);memcpy(state.attributes,state.savedAttributes,1000);
  uint8_t fg=state.foreground,bg=state.background;state.foreground=0;state.background=15;
  memset(state.text,' ',40);memset(state.attributes,0xf0,40);unsigned x=1,selectedX=1;
  for(unsigned i=0;i<state.menuCount;i++){if(i==state.menuColumn){selectedX=x;state.foreground=15;state.background=0;}
    textAt(0,x,state.menuTitles[i]);x+=strlen(state.menuTitles[i])+2;state.foreground=0;state.background=15;}
  unsigned width=1,count=0;
  for(unsigned i=0;i<state.menuItemCount;i++)if(state.menuItems[i].menu==state.menuColumn){
    const unsigned length=strlen(state.menuItems[i].text);if(length>width)width=length;count++;}
  if(width>38)width=38;if(count>21)count=21;if(selectedX+width+2>40)selectedX=38-width;
  for(unsigned y=1;y<=count+2;y++)for(unsigned col=selectedX;col<=selectedX+width+1;col++){
    const uint8_t edges=(y==1?WindowTop:0)|(y==count+2?WindowBottom:0)|
      (col==selectedX?WindowLeft:0)|(col==selectedX+width+1?WindowRight:0);
    state.text[y*40+col]=edges?WindowMarker|edges:' ';state.attributes[y*40+col]=0xf0;}
  unsigned y=2;
  for(unsigned i=0;i<state.menuItemCount&&y<count+2;i++)if(state.menuItems[i].menu==state.menuColumn){
    const MenuItem &item=state.menuItems[i];state.foreground=!item.enabled?7:i==state.menuSelection?15:0;
    state.background=i==state.menuSelection?0:15;
    memset(state.attributes+y*40+selectedX+1,state.foreground|(state.background<<4),width);
    textAt(y++,selectedX+1,item.text,width);}
  state.foreground=fg;state.background=bg;state.frameDirty=true;
}
MPE4_CODE uint8_t Game::pointerInput(const Input &in) {
  // A held bar click can finish choosing its title on the first menu frame.
  if(!in.pointerEvent&&!(state.modal==Menu&&(state.pointerButtons&1)))return 0;
  if(in.pointerEvent&&(in.pointerX>=160||in.pointerY>=200||(in.pointerButtons&~3u)))return 0;
  uint8_t pressed=0;
  if(in.pointerEvent){pressed=in.pointerButtons&~state.pointerButtons;
    state.pointerX=in.pointerX;state.pointerY=in.pointerY;state.pointerButtons=in.pointerButtons;}
  const unsigned column=state.pointerX/4,row=state.pointerY/8;
  if(pressed&2)return Escape;
  if(state.modal==Menu){
    if(!state.menuCount||!state.menuItemCount)return 0;
    unsigned left=1;
    if(row==0){
      for(unsigned menu=0;menu<state.menuCount;menu++){
        const unsigned width=strlen(state.menuTitles[menu]);
        if(column>=left&&column<left+width){
          if(state.menuColumn!=menu){state.menuColumn=menu;
            for(unsigned i=0;i<state.menuItemCount;i++)if(state.menuItems[i].menu==menu){state.menuSelection=i;break;}
            renderMenu();}
          return 0;
        }left+=width+2;
      }return 0;
    }
    for(unsigned menu=0;menu<state.menuColumn;menu++)left+=strlen(state.menuTitles[menu])+2;
    unsigned width=1;
    for(unsigned i=0;i<state.menuItemCount;i++)if(state.menuItems[i].menu==state.menuColumn){
      const unsigned length=strlen(state.menuItems[i].text);if(length>width)width=length;}
    if(width>38)width=38;if(left+width+2>40)left=38-width;
    if(column>left&&column<=left+width&&row>=2){
      unsigned itemRow=2;
      for(unsigned i=0;i<state.menuItemCount;i++)if(state.menuItems[i].menu==state.menuColumn){
        if(itemRow++==row){if(state.menuSelection!=i){state.menuSelection=i;renderMenu();}
          return (pressed&1)&&state.menuItems[i].enabled?Enter:0;}
      }
    }
    return pressed&1?Escape:0;
  }
  if(!(pressed&1))return 0;
  if(state.modal==Inventory){
    if(!flag(13))return Enter;
    if(column>=3&&column<39&&row>=3&&row<23){unsigned itemRow=3;
      for(unsigned i=0;i<state.objectCount;i++)if(state.inventory[i]==255&&itemRow++==row){state.menuSelection=i;return Enter;}}
    return 0;
  }
  if(state.modal!=NoModal)return Enter;
  if(row==0)return state.menuAllowed?Escape:0;
  Object &ego=state.objects[0];
  if(!state.graphics||!state.playerControl||!(ego.flags&Drawn)||
     state.pointerY<state.graphicsTop||state.pointerY>=unsigned(state.graphicsTop)+168)return 0;
  // Match the existing C64 pointer tip convention: x is AGI's cel-left
  // coordinate; feet sit graphicsTop+6 pixels above the pointer's screen y.
  ego.targetX=clamp(state.pointerX,0,160-(ego.width?ego.width:1));
  int minimum=ego.height?ego.height-1:0;
  if(!(ego.flags&IgnoreHorizon)&&minimum<=state.horizon)minimum=state.horizon+1;
  ego.targetY=clamp(int(state.pointerY)-state.graphicsTop-6,minimum,167);
  ego.motionMode=4;ego.flags|=Motion;ego.direction=0;state.vars[6]=0;
  return 0;
}
MPE4_CODE void Game::keyInput(const Input &in) {
  uint8_t key=in.key;const uint8_t mouseKey=pointerInput(in);if(!key)key=mouseKey;
  if(in.fire&&!key)key=Enter;
  if(state.modal==Restart){
    if(key==Enter||in.fire)restartGame();else if(key==Escape)closeModal();return;
  }
  if(state.modal==Message||state.modal==Pause||state.modal==Quit){
    if(state.modal==Quit&&(key==Enter||in.fire)){if(host.stopSound)host.stopSound(host.context);state.running=false;return;}
    if(key==Enter||key==Escape||in.fire)closeModal();return;}
  if(state.modal==Inventory){
    if(!flag(13)&&key){closeModal();return;}
    if(key==Escape){state.vars[25]=255;closeModal();}
    else if(key==Enter){state.vars[25]=state.menuSelection;closeModal();}
    else if(key==Up||key==Down){int i=state.menuSelection;if(i==255)i=0;
      const int step=key==Up?-1:1;for(unsigned n=0;n<state.objectCount;n++){i=(i+step+state.objectCount)%state.objectCount;
        if(state.inventory[i]==255){state.menuSelection=i;break;}}
      // Highlight the selected row without changing the source inventory.
      unsigned row=3;for(unsigned n=0;n<state.objectCount&&row<23;n++)if(state.inventory[n]==255){
        memset(state.attributes+row++*40+3,n==state.menuSelection?0x0f:0xf0,36);}state.frameDirty=true;}
    return;
  }
  if(state.modal==Menu){
    if(key==Escape){closeModal();return;}
    if(key==Enter){const MenuItem &item=state.menuItems[state.menuSelection];
      if(item.enabled){const uint8_t controller=item.controller;closeModal();state.controllers[controller>>3]|=1u<<(controller&7);}return;}
    if(key==Left||key==Right){state.menuColumn=(state.menuColumn+state.menuCount+(key==Left?-1:1))%state.menuCount;
      for(unsigned i=0;i<state.menuItemCount;i++)if(state.menuItems[i].menu==state.menuColumn){state.menuSelection=i;break;}}
    if(key==Up||key==Down){int i=state.menuSelection;for(unsigned n=0;n<state.menuItemCount;n++){
      i=(i+state.menuItemCount+(key==Up?-1:1))%state.menuItemCount;if(state.menuItems[i].menu==state.menuColumn){state.menuSelection=i;break;}}}
    if(key)renderMenu();return;
  }
  if(key){state.key=key;state.vars[19]=key;}if(in.scan)state.keyScan=in.scan;
  const bool entry=state.modal==StringInput||state.modal==NumberInput;
  if(!entry){for(unsigned i=0;i<state.bindingCount;i++){const Binding &b=state.bindings[i];
    if((b.ascii&&b.ascii==key)||(!b.ascii&&(key==0||key>=0x80)&&b.scan&&b.scan==in.scan)){
      uint8_t *controllers=state.inScan?queuedControllers:state.controllers;
      controllers[b.controller>>3]|=1u<<(b.controller&7);return;}}}
  if(!entry&&!state.inputEnabled)return;
  if(key==Enter){
    if(entry){if(state.modal==StringInput)copyText(state.strings[state.modalString],state.input,41);
      else{unsigned v=0;for(unsigned i=0;i<state.inputLength;i++)if(asciiDigit((uint8_t)state.input[i]))v=v*10+state.input[i]-'0';state.vars[state.modalString]=v;}
      state.modal=NoModal;state.inputLength=0;state.input[0]=0;state.key=state.keyScan=0;state.frameDirty=true;}
    else{copyText(state.previousInput,state.input,sizeof(state.previousInput));parse(state.input);
      state.inputLength=0;state.input[0]=0;drawInput();}
    return;
  }
  if(key==Backspace||key==127){if(state.inputLength)state.input[--state.inputLength]=0;drawInput();return;}
  if(key>=32&&key<127){unsigned maximum=entry?state.modalMaximum:state.vars[24];if(maximum>40)maximum=40;
    if(state.modal==NumberInput&&!asciiDigit(key))return;
    if(state.inputLength<maximum){state.input[state.inputLength++]=key;state.input[state.inputLength]=0;drawInput();}}
}
MPE4_CODE bool Game::script(uint8_t op,const uint8_t *args,uint8_t count) {
  if(!state.scriptEnabled||flag(7))return true;
  if(op==25)state.visualLength=0;
  if(state.visualLength+2u+count>sizeof(state.visualScript))return fail(ResourceBounds);
  state.visualScript[state.visualLength++]=op;state.visualScript[state.visualLength++]=count;
  memcpy(state.visualScript+state.visualLength,args,count);state.visualLength+=count;return true;
}
MPE4_CODE bool Game::restoreVisuals() {
  unsigned at=0;while(at<state.visualLength){if(at+2>state.visualLength)return fail(BadSave);
    const uint8_t op=state.visualScript[at++],n=state.visualScript[at++];
    if(at+n>state.visualLength)return fail(BadSave);const uint8_t *a=state.visualScript+at;
    if((op==25||op==28)&&n==1){if(!host.drawPicture(host.context,a[0],op==28))return fail(HostFailure);}
    else if(op==122&&n==7){if(!host.addToPicture(host.context,a[0],a[1],a[2],a[3],a[4],a[5],a[6]))return fail(HostFailure);}
    else return fail(BadSave);at+=n;
  }state.frameDirty=true;return true;
}

MPE4_CODE bool Game::action(uint8_t op,const uint8_t *a) {
  uint8_t *v=state.vars;const uint8_t x=a[0],y=a[1];char msg[768];
  if(((op>=33&&op<=89&&op!=34&&op!=63)||op==147||op==148)&&x>=32)return fail(ObjectBounds,op);
  Object &o=state.objects[x<32?x:0];
  switch(op){
    case 0:if(state.callDepth)--state.callDepth;return true;
    case 1:if(v[x]<255)v[x]++;break;case 2:if(v[x])v[x]--;break;
    case 3:v[x]=y;break;case 4:v[x]=v[y];break;case 5:v[x]+=y;break;
    case 6:v[x]+=v[y];break;case 7:v[x]-=y;break;case 8:v[x]-=v[y];break;
    case 9:v[v[x]]=v[y];break;case 10:v[x]=v[v[y]];break;case 11:v[v[x]]=y;break;
    case 12:setFlag(x,true);break;case 13:setFlag(x,false);break;case 14:setFlag(x,!flag(x));break;
    case 15:setFlag(v[x],true);break;case 16:setFlag(v[x],false);break;case 17:setFlag(v[x],!flag(v[x]));break;
    case 18:return newRoom(x);case 19:return newRoom(v[x]);
    case 20:case 21:{const uint8_t id=op==20?x:v[x];if(!host.resourceSize(host.context,Logic,id))return fail(ResourceMissing,op);break;}
    case 22:return pushLogic(x);case 23:return pushLogic(v[x]);
    case 24:if(!host.resourceSize(host.context,Picture,v[x]))return fail(ResourceMissing,op);break;
    case 25:case 28:{uint8_t id=v[x];if(!host.drawPicture(host.context,id,op==28))return fail(HostFailure,op);
      state.picture=id;state.frameDirty=true;if(op==25){state.pictureVisible=false;
        if(state.graphics)for(unsigned row=state.graphicsTop/8;row<state.graphicsTop/8+21&&row<25;row++)memset(state.text+row*40,0,40);}
      updateEgoControls();return script(op,&id,1);}
    case 26:state.pictureVisible=true;updateEgoControls();state.frameDirty=true;break;
    case 27:break; // Immutable PIC resources have no allocation to discard.
    case 29:showMessage(gamePriorityText);break;
    case 30:case 31:if(!host.resourceSize(host.context,View,op==30?x:v[x]))return fail(ResourceMissing,op);break;
    case 32:case 153:break; // Immutable VIEW resources need no discard operation.
    case 33:if(!(o.flags&Animated)){o.flags=Animated|Updating|Cycling|Motion;
      o.direction=0;o.motionMode=o.cycleMode=0;}break;
    case 34:for(auto &ob:state.objects)ob.flags&=~(Animated|Drawn);state.frameDirty=true;break;
    case 35:if(o.flags&Drawn)break;if(!celInfo(x)||!fixPosition(x))return false;
      o.flags|=Drawn|Updating;o.flags&=~SkipCycle;if(!x)updateEgoControls();state.frameDirty=true;break;
    case 36:o.flags&=~Drawn;if(!x)updateEgoControls();state.frameDirty=true;break;
    case 37:case 38:o.x=op==37?y:v[y];o.y=op==37?a[2]:v[a[2]];if(!x)updateEgoControls();state.frameDirty=true;break;
    case 39:v[y]=o.x;v[a[2]]=o.y;break;
    case 40:o.x=clamp(int(o.x)+(int8_t)v[y],0,255);o.y=clamp(int(o.y)+(int8_t)v[a[2]],0,255);
      o.flags|=Repositioned;if(!fixPosition(x))return false;state.frameDirty=true;break;
    case 41:case 42:{o.view=op==41?y:v[y];CelInfo c={};
      if(!host.viewCelInfo(host.context,o.view,0,0,&c))return fail(HostFailure,op);
      if(o.loop>=c.loops)o.loop=0;
      if(!host.viewCelInfo(host.context,o.view,o.loop,0,&c))return fail(HostFailure,op);
      if(o.cel>=c.cels)o.cel=0;
      if(!celInfo(x))return false;state.frameDirty=true;break;}
    case 43:case 44:{o.loop=op==43?y:v[y];CelInfo c={};
      if(!host.viewCelInfo(host.context,o.view,o.loop,0,&c))return fail(HostFailure,op);
      if(o.cel>=c.cels)o.cel=0;
      if(!celInfo(x))return false;state.frameDirty=true;break;}
    case 45:o.flags|=FixedLoop;break;case 46:o.flags&=~FixedLoop;break;
    case 47:case 48:{o.cel=op==47?y:v[y];CelInfo c={};
      if(!host.viewCelInfo(host.context,o.view,o.loop,0,&c))return fail(HostFailure,op);
      if(o.cel>=c.cels)o.cel=c.cels-1;
      if(!celInfo(x))return false;state.frameDirty=true;break;}
    case 49:case 53:{CelInfo c={};if(!host.viewCelInfo(host.context,o.view,o.loop,o.cel,&c))return fail(HostFailure,op);
      v[y]=op==49?c.cels-1:c.loops;break;}
    case 50:v[y]=o.cel;break;case 51:v[y]=o.loop;break;case 52:v[y]=o.view;break;
    case 54:case 55:o.priority=op==54?y:v[y];o.flags|=FixedPriority;if(!x)updateEgoControls();state.frameDirty=true;break;
    case 56:o.flags&=~FixedPriority;if(!x)updateEgoControls();state.frameDirty=true;break;
    case 57:v[y]=o.priority;break;case 58:o.flags&=~Updating;break;
    case 59:o.flags|=Updating;state.frameDirty=true;break;case 60:state.frameDirty=true;break;
    case 61:o.flags|=IgnoreHorizon;break;case 62:o.flags&=~IgnoreHorizon;break;case 63:state.horizon=x;break;
    case 64:o.flags=(o.flags|OnWater)&~OnLand;break;case 65:o.flags=(o.flags|OnLand)&~OnWater;break;
    case 66:o.flags&=~(OnWater|OnLand);break;case 67:o.flags|=IgnoreObjects;break;case 68:o.flags&=~IgnoreObjects;break;
    case 69:if(y>=32)return fail(ObjectBounds,op);{
      const Object &b=state.objects[y];v[a[2]]=(o.flags&Drawn)&&(b.flags&Drawn)?
        clamp(distance(o.x+o.width/2,b.x+b.width/2)+distance(o.y,b.y),0,254):255;}break;
    case 70:o.flags&=~Cycling;break;case 71:o.flags|=Cycling;break;
    case 72:o.cycleMode=0;o.flags|=Cycling;break;
    case 73:case 75:o.cycleMode=op==73?1:3;o.cycleFlag=y;setFlag(y,false);
      o.flags|=Cycling|Updating|SkipCycle;o.cycleCounter=0;break;
    case 74:o.cycleMode=2;o.flags|=Cycling;break;
    case 76:o.cycleTime=v[y]?v[y]:1;break;
    // AGI2 stop.motion changes direction and motion type; it does not disable
    // future normal motion. player.control may restore input without start.motion.
    case 77:o.flags|=Motion;o.direction=0;o.motionMode=0;if(!x){state.playerControl=false;v[6]=0;}break;
    case 78:o.flags|=Motion;o.motionMode=0;if(!x){state.playerControl=true;v[6]=0;o.direction=0;}break;
    case 79:o.stepSize=v[y]?v[y]:1;break;case 80:o.stepTime=v[y]?v[y]:1;break;
    case 81:case 82:o.targetX=op==81?y:v[y];o.targetY=op==81?a[2]:v[a[2]];
      o.moveStep=o.stepSize;{uint8_t step=op==81?a[3]:v[a[3]];if(step)o.stepSize=step;}
      o.motionFlag=a[4];setFlag(a[4],false);o.motionMode=1;o.flags|=Motion;
      if(!x)state.playerControl=false;break;
    case 83:o.followDistance=y<o.stepSize?o.stepSize:y;o.motionFlag=a[2];setFlag(a[2],false);o.motionMode=2;o.flags|=Motion;break;
    case 84:if(o.motionMode!=3)o.wanderCounter=0;o.motionMode=3;o.flags|=Motion;break;
    case 85:o.motionMode=0;o.flags|=Motion;break;
    case 86:o.direction=v[y];if(o.direction>8)o.direction=0;if(!x)v[6]=o.direction;break;
    case 87:v[y]=o.direction;break;case 88:o.flags|=IgnoreBlocks;break;case 89:o.flags&=~IgnoreBlocks;break;
    case 90:memcpy(state.block,a,4);state.blockActive=true;break;case 91:state.blockActive=false;break;
    case 92:case 93:case 94:case 95:case 96:case 97:{
      uint8_t id=(op==93||op==96||op==97)?v[x]:x;if(id>=state.objectCount)return fail(ObjectBounds,op);
      if(op==97)v[y]=state.inventory[id];else state.inventory[id]=op==92||op==93?255:op==94?0:op==96?v[y]:y;break;}
    case 98:if(!host.resourceSize(host.context,Sound,x))return fail(ResourceMissing,op);break;
    case 99:if(state.soundActive)setFlag(state.soundFlag,true);if(host.stopSound)host.stopSound(host.context);
      state.soundFlag=y;setFlag(y,false);state.soundActive=false;
      if(flag(9)&&host.playSound){if(!host.playSound(host.context,x))return fail(HostFailure,op);state.soundActive=true;}
      else setFlag(y,true);break;
    case 100:if(host.stopSound)host.stopSound(host.context);if(state.soundActive)setFlag(state.soundFlag,true);state.soundActive=false;break;
    case 101:case 102:case 151:case 152:if(!message(state.logic,op==102||op==152?v[x]:x,msg,sizeof(msg)))return false;
      showMessage(msg,op>=151?y:0,op>=151?a[2]:0,op>=151?a[3]:0);break;
    case 103:case 104:if(!message(state.logic,op==103?a[2]:v[a[2]],msg,sizeof(msg)))return false;
      textAt(op==103?x:v[x],op==103?y:v[y],msg);break;
    case 105:clearLines(x,y,a[2]);break;
    case 106:state.graphics=false;memset(state.text,' ',1000);memset(state.attributes,state.foreground|(state.background<<4),1000);
      state.frameDirty=state.textDirty=true;break;
    case 107:state.graphics=true;memset(state.text,0,1000);drawStatus();drawInput();state.frameDirty=true;break;
    case 108:if(!message(state.logic,x,msg,sizeof(msg)))return false;state.cursor=msg[0];break;
    case 109:state.foreground=x&15;state.background=y&15;break;
    case 110:state.shakeTicks=clamp(x*8,0,255);state.frameDirty=true;break;
    case 111:state.graphicsTop=clamp(x,0,4)*8;state.inputRow=y;state.statusRow=a[2];break;
    case 112:state.statusVisible=true;drawStatus();break;
    case 113:state.statusVisible=false;clearLines(state.statusRow,state.statusRow,state.background);break;
    case 114:if(x>=25)return fail(StringBounds,op);if(!message(state.logic,y,state.strings[x],41))return false;break;
    case 115:case 118:{if(op==115&&x>=25)return fail(StringBounds,op);
      if(!message(state.logic,op==115?y:x,msg,sizeof(msg)))return false;
      state.modal=op==115?StringInput:NumberInput;state.modalString=op==115?x:y;
      state.modalMaximum=op==115?clamp(a[4],0,40):3;state.modalRow=op==115?a[2]:state.inputRow;
      state.modalColumn=op==115?a[3]:0;textAt(state.modalRow,state.modalColumn,msg);
      state.modalColumn=clamp(state.modalColumn+strlen(msg),0,39);state.inputLength=0;state.input[0]=0;
      state.key=state.keyScan=0;drawInput();break;}
    case 116:if(x>=25)return fail(StringBounds,op);state.strings[x][0]=0;
      if(y<state.wordCount){unsigned n=state.wordLength[y];if(n>40)n=40;
        memcpy(state.strings[x],state.parsedText+state.wordOffset[y],n);state.strings[x][n]=0;}break;
    case 117:if(x>=25)return fail(StringBounds,op);return parse(state.strings[x]);
    case 119:state.inputEnabled=false;clearLines(state.inputRow,state.inputRow,state.background);break;
    case 120:state.inputEnabled=true;drawInput();break;
    case 121:{unsigned i=0;while(i<state.bindingCount&&(state.bindings[i].ascii!=x||state.bindings[i].scan!=y))i++;
      if(i>=32)return fail(ResourceBounds,op);if(i==state.bindingCount)state.bindingCount++;
      state.bindings[i]={x,y,a[2]};break;}
    case 122:case 123:{uint8_t b[7];for(int i=0;i<7;i++)b[i]=op==122?a[i]:v[a[i]];
      if(!host.addToPicture(host.context,b[0],b[1],b[2],b[3],b[4],b[5],b[6]))return fail(HostFailure,op);
      state.frameDirty=true;return script(122,b,7);}
    case 124:inventoryMenu();break;
    case 125:if(!host.save||!host.save(host.context,&state,sizeof(state)))
      showMessage(gameSaveFailedText);break;
    case 126:{const uint32_t liveRandom=state.random;
      if(!host.restore||!host.restore(host.context,&state,sizeof(state))){
      showMessage(gameRestoreFailedText);break;}
      if(state.signature!=0x3153344d||state.callDepth>16||state.wordCount>20||state.objectCount>256||state.visualLength>768)
        return fail(BadSave,op);
      // AGI save data restores the game, not the interpreter's random source.
      // Retaining the live generator also lets an ordinary restore retry a
      // random hazard instead of reproducing the same roll indefinitely.
      memset(queuedControllers,0,sizeof(queuedControllers));
      state.random=liveRandom;state.error=Okay;state.running=true;setFlag(12,true);state.restorePending=true;
      if(host.stopSound)host.stopSound(host.context);state.soundActive=false;return restoreVisuals();}
    case 127:showMessage("Save storage is supplied by TeensyROM.");break;
    case 128:if(flag(16))return restartGame();
      if(host.stopSound)host.stopSound(host.context);
      if(state.soundActive)setFlag(state.soundFlag,true);state.soundActive=false;
      showMessage(gameRestartText);state.modal=Restart;state.modalTicks=0;break;
    case 129:case 162:{state.showObjectView=op==129?x:v[x];uint8_t p[2];
      if(!read(View,state.showObjectView,3,p,2))return false;
      uint32_t off=le16(p);unsigned at=0;msg[0]=0;
      if(off)while(at+1<sizeof(msg)){uint8_t c;if(!read(View,state.showObjectView,off++,&c,1))return false;
        if(!c)break;if(c=='%'){uint8_t kind;if(!read(View,state.showObjectView,off++,&kind,1))return false;
          if(kind=='v'){unsigned id=0;while(true){if(!read(View,state.showObjectView,off,&c,1))return false;
            if(c<'0'||c>'9')break;id=id*10+c-'0';off++;if(id>255)return fail(BadLogic,op);}
            char number[4];unsignedText(number,sizeof(number),v[id]);
            for(unsigned i=0;number[i]&&at+1<sizeof(msg);i++)msg[at++]=number[i];continue;}
          msg[at++]='%';c=kind;}
        msg[at++]=c;}msg[at]=0;
      if(!at)copyText(msg,"Press Enter to continue.",sizeof(msg));
      showMessage(msg,2,4,32);state.showObject=true;break;}
    case 130:state.random=state.random*1664525u+1013904223u;
      v[a[2]]=x<=y?x+(state.random>>16)%(unsigned(y)-x+1):x;break;
    case 131:state.playerControl=false;if(state.objects[0].motionMode==4){state.objects[0].motionMode=0;state.objects[0].direction=0;v[6]=0;}break;
    case 132:state.playerControl=true;state.objects[0].flags|=Motion;state.objects[0].motionMode=0;break;
    case 133:{uint8_t id=v[x];if(id>=32)return fail(ObjectBounds,op);const Object &b=state.objects[id];
      copyText(msg,"Object ",sizeof(msg));appendUnsigned(msg,sizeof(msg),id);appendText(msg,sizeof(msg),"  View ");appendUnsigned(msg,sizeof(msg),b.view);
      appendText(msg,sizeof(msg),"\nPosition ");appendUnsigned(msg,sizeof(msg),b.x);appendText(msg,sizeof(msg),",");appendUnsigned(msg,sizeof(msg),b.y);
      appendText(msg,sizeof(msg),"  Loop ");appendUnsigned(msg,sizeof(msg),b.loop);appendText(msg,sizeof(msg)," Cel ");appendUnsigned(msg,sizeof(msg),b.cel);showMessage(msg);break;}
    case 134:showMessage("Quit game? Enter to quit, Escape to return.");state.modal=Quit;break;
    case 135:copyText(msg,"Native AGI state: ",sizeof(msg));appendUnsigned(msg,sizeof(msg),sizeof(State));appendText(msg,sizeof(msg)," bytes");showMessage(msg);break;
    case 136:showMessage(gamePausedText);state.modal=Pause;break;
    case 137:copyText(state.input,state.previousInput,sizeof(state.input));state.inputLength=strlen(state.input);drawInput();break;
    case 138:state.inputLength=0;state.input[0]=0;drawInput();break;
    case 139:break; // Joystick sampling belongs to the physical terminal.
    case 140:showMessage("The C64 uses its fixed color monitor palette.");break;
    case 141:showMessage("Native AGI 2.426 compatible interpreter");break;
    case 142:state.scriptLimit=x;state.visualLength=0;break;
    case 143:if(!message(state.logic,x,msg,sizeof(msg)))return false;break; // Save identity is package-owned.
    case 144:if(!message(state.logic,x,msg,sizeof(msg)))return false;break; // Debug log has no game-state effect.
    case 145:state.scanStart[state.logic]=state.calls[state.callDepth-1].ip;break;
    case 146:state.scanStart[state.logic]=0;break;
    case 147:case 148:o.x=op==147?y:v[y];o.y=op==147?a[2]:v[a[2]];
      o.flags|=Repositioned;if(!fixPosition(x))return false;state.frameDirty=true;break;
    case 149:case 150:break; // Interpreter tracing is intentionally host-side.
    case 154:for(unsigned r=x;r<=a[2]&&r<25;r++)for(unsigned c=y;c<=a[3]&&c<40;c++){
      state.text[r*40+c]=state.graphics?0:' ';state.attributes[r*40+c]=(a[4]<<4)|state.foreground;}state.frameDirty=true;break;
    case 155:state.graphicsTop=clamp(x,0,4)*8;break;
    case 156:if(state.menuCount>=8)return fail(ResourceBounds,op);
      if(!message(state.logic,x,state.menuTitles[state.menuCount++],16))return false;break;
    case 157:if(!state.menuCount||state.menuItemCount>=40)return fail(ResourceBounds,op);{
      MenuItem &item=state.menuItems[state.menuItemCount++];item.menu=state.menuCount-1;item.controller=y;item.enabled=1;
      if(!message(state.logic,x,item.text,sizeof(item.text)))return false;}break;
    case 158:break; // Menu definitions become visible only during menu.input.
    case 159:case 160:for(auto &item:state.menuItems)if(item.controller==x)item.enabled=op==159;break;
    case 161:if(state.menuAllowed&&state.menuCount&&state.menuItemCount){state.modal=Menu;state.menuColumn=0;state.menuSelection=0;
      state.key=state.keyScan=0;renderMenu();}break;
    case 163:state.dialogue=true;break;case 164:state.dialogue=false;break;
    case 165:v[x]*=y;break;case 166:v[x]*=v[y];break;
    case 167:if(!y)return fail(BadLogic,op);v[x]/=y;break;case 168:if(!v[y])return fail(BadLogic,op);v[x]/=v[y];break;
    case 169:closeModal();break;
    case 170:break; // Simple-save mode uses the same host slot contract.
    case 171:state.scriptSavedLengthLow=state.visualLength;state.scriptSavedLengthHigh=state.visualLength>>8;break;
    case 172:state.visualLength=state.scriptSavedLengthLow|(uint16_t(state.scriptSavedLengthHigh)<<8);return restoreVisuals();
    case 173:state.holdKey=true;break;case 174:state.priorityBase=x;break;
    case 175:break;case 176:break;case 177:state.menuAllowed=x!=0;break;
    case 181:state.holdKey=false;break;
    default:return fail(UnsupportedAction,op);
  }return true;
}

MPE4_CODE bool Game::run(uint32_t budget) {
  while(state.callDepth&&state.modal==NoModal&&state.running&&budget--){
    state.logic=state.calls[state.callDepth-1].logic;
    uint8_t op;if(!fetch(op))return false;
    if(op==0xff){bool pass;uint16_t skip;if(!expression(pass)||!fetch16(skip))return false;
      if(!pass){Call &c=state.calls[state.callDepth-1];uint32_t next=c.ip+skip;
        if(next>=c.end)return fail(BadLogic,op);c.ip=next;}continue;}
    if(op==0xfe){uint16_t jump;if(!fetch16(jump))return false;Call &c=state.calls[state.callDepth-1];
      int32_t next=int32_t(c.ip)+(int16_t)jump;if(next<0||next>=c.end)return fail(BadLogic,op);c.ip=next;continue;}
    if(op>=sizeof(arity))return fail(UnsupportedAction,op);
    uint8_t a[7]={};for(unsigned i=0;i<arity[op];i++)if(!fetch(a[i]))return false;
    if(!action(op,a))return false;if(state.restorePending)return true;
  }return state.error==Okay;
}

MPE4_CODE bool Game::passable(uint8_t id,int16_t x,int16_t y,bool &water,bool moving) {
  const Object &o=state.objects[id];water=false;
  if(x<0||y<0||x+o.width>160||y>167||y+1<o.height||(!(o.flags&IgnoreHorizon)&&y<=state.horizon))return false;
  if(moving&&state.blockActive&&!(o.flags&IgnoreBlocks)){
    bool was=o.x>state.block[0]&&o.x<state.block[2]&&o.y>state.block[1]&&o.y<state.block[3];
    bool now=x>state.block[0]&&x<state.block[2]&&y>state.block[1]&&y<state.block[3];if(was!=now)return false;}
  bool trigger=false;
  if(!((o.flags&FixedPriority)&&o.priority==15)){
    water=true;
    for(unsigned n=0;n<(o.width?o.width:1);n++){uint8_t pri=host.priorityAt(host.context,x+n,y);
      if(pri==0||(pri==1&&!(o.flags&IgnoreBlocks)))return false;
      if(pri!=3)water=false;if(pri==2)trigger=true;}
    if((o.flags&OnWater)&&!water)return false;if((o.flags&OnLand)&&water)return false;
  }
  if(!(o.flags&IgnoreObjects))for(unsigned i=0;i<32;i++)if(i!=id){const Object &b=state.objects[i];
    if((b.flags&(Animated|Drawn))!=(Animated|Drawn)||(b.flags&IgnoreObjects))continue;
    const bool overlap=x<=int(b.x)+b.width&&x+o.width>=b.x;
    if(overlap&&(y==b.y||(moving&&((o.y<b.y&&y>b.y)||(o.y>b.y&&y<b.y)))))return false;}
  if(id==0){setFlag(0,water);setFlag(3,trigger);}return true;
}
MPE4_CODE bool Game::fixPosition(uint8_t id) {
  Object &o=state.objects[id];int x=o.x,y=o.y;
  if(!(o.flags&IgnoreHorizon)&&y<=state.horizon)y=state.horizon+1;
  // Search the original AGI west/south/east/north expanding square. Bound it
  // to cover the complete 160x168 scene even from a byte-coordinate outside it.
  static const int8_t sx[] MPE4_RODATA={-1,0,1,0},sy[] MPE4_RODATA={0,1,0,-1};
  unsigned side=1,left=1,direction=0;
  for(unsigned checked=0;checked<262144;checked++){
    bool water;if(passable(id,x,y,water,false)){
      if(o.x!=x||o.y!=y)state.frameDirty=true;o.x=x;o.y=y;
      if(!id)updateEgoControls();return true;
    }
    x+=sx[direction];y+=sy[direction];
    if(!--left){direction=(direction+1)&3;if(!(direction&1))side++;left=side;}
  }
  return fail(NoPosition);
}
MPE4_CODE void Game::finishMove(uint8_t id) {
  Object &o=state.objects[id];setFlag(o.motionFlag,true);o.motionMode=0;o.direction=0;
  o.stepSize=o.moveStep?o.moveStep:1;if(!id){state.playerControl=true;state.vars[6]=0;}
}
MPE4_CODE void Game::updateEgoControls() {
  const Object &ego=state.objects[0];bool water=false,trigger=false;
  if(state.pictureVisible&&(ego.flags&Drawn)&&!((ego.flags&FixedPriority)&&ego.priority==15)&&
     ego.x<160&&ego.y<168&&unsigned(ego.x)+ego.width<=160){
    water=true;
    for(unsigned x=0;x<(ego.width?ego.width:1);x++){
      const uint8_t p=host.priorityAt(host.context,ego.x+x,ego.y);
      if(p!=3)water=false;if(p==2)trigger=true;
    }
  }
  setFlag(0,water);setFlag(3,trigger);
}
MPE4_CODE void Game::moveObjects() {
  static const int8_t dx[] MPE4_RODATA={0,0,1,1,1,0,-1,-1,-1};
  static const int8_t dy[] MPE4_RODATA={0,-1,-1,0,1,1,1,0,-1};
  for(unsigned id=0;id<32&&state.running;id++){
    Object &o=state.objects[id];if((o.flags&(Animated|Drawn|Updating))!=(Animated|Drawn|Updating))continue;
    CelInfo c={};if(!host.viewCelInfo(host.context,o.view,o.loop,o.cel,&c)){fail(HostFailure);return;}
    o.width=c.width;o.height=c.height;
    const bool skipCycle=(o.flags&SkipCycle)!=0;o.flags&=~SkipCycle;
    if(!skipCycle&&(o.flags&Cycling)&&++o.cycleCounter>=o.cycleTime){o.cycleCounter=0;
      if(o.cycleMode<=1){if(o.cel+1<c.cels)o.cel++;else if(o.cycleMode==0)o.cel=0;
        if(o.cycleMode==1&&o.cel+1>=c.cels){setFlag(o.cycleFlag,true);o.flags&=~Cycling;o.cycleMode=0;}}
      else{if(o.cel)o.cel--;else if(o.cycleMode==2)o.cel=c.cels-1;
        if(o.cycleMode==3&&!o.cel){setFlag(o.cycleFlag,true);o.flags&=~Cycling;o.cycleMode=0;}}
      if(!celInfo(id))return;state.frameDirty=true;
    }
    if(++o.stepCounter<o.stepTime)continue;o.stepCounter=0;
    if(!(o.flags&Motion))o.direction=0;
    else if(o.motionMode==1){o.direction=directionTo(o.x,o.y,o.targetX,o.targetY,(o.stepSize?o.stepSize:1)-1);
      if(!o.direction)finishMove(id);}
    else if(o.motionMode==4){o.direction=directionTo(o.x,o.y,o.targetX,o.targetY,(o.stepSize?o.stepSize:1)-1);
      if(!o.direction)o.motionMode=0;}
    else if(o.motionMode==2){const Object &ego=state.objects[0];
      o.direction=directionTo(o.x+o.width/2,o.y,ego.x+ego.width/2,ego.y,o.followDistance);
      if(!o.direction){setFlag(o.motionFlag,true);o.motionMode=0;}}
    else if(o.motionMode==3){if(!o.wanderCounter){state.random=state.random*1664525u+1013904223u;
      o.direction=(state.random>>16)%9;o.wanderCounter=6+(state.random>>24)%45;}else o.wanderCounter--;}
    if(o.direction>8)o.direction=0;
    if(!(o.flags&FixedLoop)&&o.direction){uint8_t loop=o.loop;
      if(c.loops>=4)loop=o.direction==1?3:o.direction==5?2:o.direction<=4?0:1;
      else if(c.loops==2){if(o.direction>=2&&o.direction<=4)loop=0;else if(o.direction>=6)loop=1;}
      if(loop!=o.loop){o.loop=loop;CelInfo next={};
        if(!host.viewCelInfo(host.context,o.view,o.loop,0,&next)){fail(HostFailure);return;}
        if(o.cel>=next.cels)o.cel=0;if(!celInfo(id))return;state.frameDirty=true;}}
    const unsigned steps=o.stepSize?o.stepSize:1;const bool repositioned=(o.flags&Repositioned)!=0;
    int nx=o.x,ny=o.y;if(!repositioned){nx+=int(steps)*dx[o.direction];ny+=int(steps)*dy[o.direction];}
    uint8_t edge=0;
    if(nx<0){nx=0;edge=4;}else if(nx+o.width>160){nx=160-o.width;edge=2;}
    if(ny+1<o.height){ny=o.height-1;edge=1;}else if(ny>167){ny=167;edge=3;}
    else if(!(o.flags&IgnoreHorizon)&&ny<=state.horizon){ny=state.horizon+1;edge=1;}
    bool water;const bool blocked=!passable(id,nx,ny,water,!repositioned);
    const bool moved=!blocked&&(o.x!=nx||o.y!=ny);
    if(blocked){edge=0;if(!fixPosition(id))return;}
    else{o.x=nx;o.y=ny;}
    if(edge){if(!id)state.vars[2]=edge;else{state.vars[4]=id;state.vars[5]=edge;}
      if(o.motionMode==1)finishMove(id);}
    if((blocked||edge)&&o.motionMode==4){o.motionMode=0;o.direction=0;}
    o.flags&=~Repositioned;
    if(blocked&&o.motionMode==3)o.wanderCounter=0;
    if(!(o.flags&FixedPriority)){int pri=o.y<state.priorityBase?4:5+(o.y-state.priorityBase)*10/(168-state.priorityBase);
      o.priority=clamp(pri,4,15);}
    if(!id)state.vars[6]=o.direction;if(moved)state.frameDirty=true;
  }
  // Stationary/repositioned ego still updates the water and trigger flags.
  updateEgoControls();
}
MPE4_CODE Step Game::tick(const Input &input,uint32_t budget) {
  if(state.error)return Failed;if(!state.running)return Waiting;
  state.frameDirty=false;state.textDirty=false;
  if(input.soundFinished&&state.soundActive){setFlag(state.soundFlag,true);state.soundActive=false;}
  if(state.shakeTicks){state.shakeTicks=input.elapsed60Hz>=state.shakeTicks?0:state.shakeTicks-input.elapsed60Hz;state.frameDirty=true;}
  const uint8_t oldModal=state.modal;keyInput(input);if(state.error)return Failed;
  if(state.modalTicks){if(input.elapsed60Hz>=state.modalTicks)closeModal();else state.modalTicks-=input.elapsed60Hz;}
  if(state.modal!=NoModal)return state.frameDirty?Frame:Waiting;
  if(state.playerControl){Object &ego=state.objects[0];
    if(input.direction||state.direction){if(ego.motionMode==4)ego.motionMode=0;ego.direction=input.direction<=8?input.direction:0;state.vars[6]=ego.direction;}
    if(!oldModal){uint8_t direction=input.key==Up?1:input.key==PageUp?2:input.key==Right?3:input.key==PageDown?4:
      input.key==Down?5:input.key==End?6:input.key==Left?7:input.key==Home?8:0;
      if(direction){if(ego.motionMode==4)ego.motionMode=0;ego.direction=state.holdKey?direction:ego.direction==direction?0:direction;state.vars[6]=ego.direction;}}
  }state.direction=input.direction;
  // Ordinary command editing runs alongside the AGI timer and room logic.
  // Only authored get.string/get.num and modal dialogs suspend interpretation.
  state.clockTicks+=input.elapsed60Hz;
  while(state.clockTicks>=60){state.clockTicks-=60;if(++state.vars[11]>=60){state.vars[11]=0;
    if(++state.vars[12]>=60){state.vars[12]=0;if(++state.vars[13]>=24){state.vars[13]=0;state.vars[14]++;}}}}
  state.scanTicks=clamp(state.scanTicks+input.elapsed60Hz,0,255);
  if(!state.inScan){unsigned interval=(state.vars[10]?state.vars[10]:1)*3;
    if(!state.firstScan&&state.scanTicks<interval)return state.frameDirty?Frame:Idle;
    state.scanTicks=0;state.firstScan=false;state.inScan=true;state.callDepth=0;
    for(unsigned i=0;i<sizeof(queuedControllers);i++){state.controllers[i]|=queuedControllers[i];queuedControllers[i]=0;}
    if(!pushLogic(0))return Failed;}
  if(!run(budget))return Failed;
  if(state.restorePending){state.restorePending=false;return Frame;}
  if(state.modal!=NoModal)return Frame;
  if(state.callDepth)return state.frameDirty?Frame:Yielded;
  state.inScan=false;state.scans++;setFlag(5,false);setFlag(6,false);setFlag(12,false);
  state.vars[2]=state.vars[4]=state.vars[5]=0;moveObjects();if(state.error)return Failed;
  memset(state.controllers,0,sizeof(state.controllers));state.key=state.keyScan=0;
  setFlag(2,false);setFlag(4,false);drawStatus();
  return state.frameDirty?Frame:Idle;
}
}
