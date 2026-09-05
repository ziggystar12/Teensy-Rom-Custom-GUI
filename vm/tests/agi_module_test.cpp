// Exercise the real module and real generic host storage against temporary files.
#include "fake_sd.h"
#include "../../Source/Teensy/MinimalBoot/Common/VMFiles.h"
#include "../agi/agivm.cpp"
#include <iostream>
static uint32_t clockUs;static uint8_t failure;static uint32_t detail;
static uint32_t now(){return clockUs;}
static void failed(uint8_t c,uint32_t d){failure=c;detail=d;}
static bool yield(){return false;}
alignas(32) static uint8_t support[VM_DATA_BYTES+64],guest[VM_RAM_BYTES+64];
static const VmModule *module;
static std::ofstream wire;
static uint8_t sequence;
static unsigned frameCount;
static void record(const VmPacket &p){
 if(frameCount>200)return;uint8_t bytes[240]={'M','3',1,p.type,0,p.flags,p.length,0};sequence=sequence==255?1:sequence+1;bytes[4]=sequence;
 memcpy(bytes+8,p.payload,p.length);uint16_t crc=65535;for(unsigned n=0;n<p.length+8u;n++){crc^=uint16_t(bytes[n])<<8;for(unsigned b=0;b<8;b++)crc=(crc<<1)^((crc&0x8000)?0x1021:0);}
 bytes[p.length+8]=crc;bytes[p.length+9]=crc>>8;uint16_t size=p.length+10;wire.put(size);wire.put(size>>8);wire.write((char *)bytes,size);
 if(p.type==2)frameCount++;
}
static void check(){if(failure){std::cerr<<"AGI failure "<<unsigned(failure)<<" detail "<<detail<<"\n";std::abort();}}
struct Frame{unsigned cells=0,replace=0,sprites=0;};
static Frame frame(){Frame f;clockUs+=16667;for(unsigned n=0;n<100000;n++){
 module->pump();VmPacket p{};if(!module->packet(&p)){check();continue;}assert(p.length<=228);const auto saved=agivm::runtime->packet;
 const auto frozen=std::vector<uint8_t>(agivm::runtime->session.next,agivm::runtime->session.next+10000);
 for(unsigned i=0;i<3;i++)module->pump();assert(!memcmp(&saved,&agivm::runtime->packet,sizeof saved));assert(!memcmp(frozen.data(),agivm::runtime->session.next,10000));
 if(p.type==1){assert(p.length%12==0);f.cells+=p.length/12;f.replace+=(p.flags&16)!=0;}
 if(p.type==5)f.sprites++;record(p);module->ack();check();if(p.type==2)return f;
 }assert(!"AGI frame timeout");return f;}
static void key(uint8_t value){VmInput i{value,0,0,1};module->input(&i);}
static void joystick(uint8_t value){VmInput i{0,0,value,2};module->input(&i);}
static void guards(){for(unsigned i=0;i<32;i++){assert(support[i]==0xa5&&support[VM_DATA_BYTES+32+i]==0xa5);assert(guest[i]==0x5a&&guest[VM_RAM_BYTES+32+i]==0x5a);}}
int main(int argc,char **argv){
 assert(argc==3||argc==4||(argc==5&&(!strcmp(argv[4],"--dialogs")||!strcmp(argv[4],"--saves"))));base=argv[2];assert(base.string().find("agi-sandbox-")!=std::string::npos);
 fs::create_directories(base/"VMS");fs::copy(fs::path(argv[1])/"VMS/AGIVM",base/"VMS/AGIVM",fs::copy_options::recursive);
 wire.open(base/"wire.bin",std::ios::binary);
 auto original=base/"VMS/AGIVM/GAMES/AGITEST.AGI";
 if(argc==3)for(unsigned n=0;n<40;n++){char name[32];snprintf(name,sizeof name,"GAME%02u.AGI",n);fs::copy_file(original,original.parent_path()/name);}
 std::string selected=argc>=4?argv[3]:"";if(!selected.empty()){fs::create_directories(base/"PRIVATE");fs::copy_file(selected,base/"PRIVATE/SELECTED.AGI");}
 memset(support,0xa5,sizeof support);memset(guest,0x5a,sizeof guest);
 VmHost host{VM_ABI,agivm::RequiredHostBytes,VM_SERVICES,support+32,VM_DATA_BYTES,"/VMS/AGIVM",selected.empty()?"":"/PRIVATE/SELECTED.AGI",now,VmFiles::openFile,VmFiles::readFile,VmFiles::nextFile,VmFiles::closeFile,
  guest+32,VM_RAM_BYTES,VmFiles::openFlags,VmFiles::writeFile,VmFiles::fileOp,yield,failed};
 auto shortHost=host;shortHost.bytes--;assert(!vm_entry(&shortHost));
 module=vm_entry(&host);check();assert(module);auto &r=*agivm::runtime;auto &s=r.session.game.state;
 assert((uint8_t *)&s==guest+32);assert((uint8_t *)&r>=support+32&&(uint8_t *)&r+sizeof r<=support+32+VM_DATA_BYTES);
 VmRegistry::Launch request{};assert(VmRegistry::find("agi",nullptr,request)==1&&VmRegistry::preflight(request));VmRegistry::refresh(true);assert(VmRegistry::associated("Game.AGI"));
 assert(VmRegistry::tryLaunch(rmtSD,"/VMS/AGIVM/GAMES","AGITEST.AGI"));assert(rebooted);VmRegistry::Launch launch{};assert(VmRegistry::consume(launch));assert(!strcmp(launch.content,"/VMS/AGIVM/GAMES/AGITEST.AGI"));
 if(argc==5&&!strcmp(argv[4],"--saves")){
  frame();auto &game=r.session.game;
  auto row=[&](unsigned slot){return std::string((char*)s.text+(4+slot)*40+4,32);};
  auto open=[&](bool save){s.vars[210]=save?1:2;
   for(unsigned n=0;n<8&&s.modal!=(save?mpe4::SaveSlots:mpe4::RestoreSlots);n++)frame();
   assert(s.modal==(save?mpe4::SaveSlots:mpe4::RestoreSlots));guards();};
  auto leave=[&](){key(mpe4::Escape);frame();assert(s.modal==mpe4::NoModal);};
  auto select=[&](){key(mpe4::Enter);frame();};
  auto summary=[&](unsigned slot){const auto before=s;const auto info=game.host.saveInfo(game.host.context,slot);
   assert(!memcmp(&s,&before,sizeof s));return info;};
  open(true);for(unsigned n=0;n<12;n++)assert(row(n).find("Empty")!=std::string::npos);
  s.vars[0]=12;s.vars[3]=35;select();assert(s.modal==mpe4::NoModal);
  open(false);assert(row(0).find("> 01  Room 12  Score 35")==0);leave();
  s.vars[0]=255;s.vars[3]=255;open(true);key(mpe4::Up);frame();assert(s.menuSelection==11);select();
  open(false);assert(row(11).find("12  Room 255  Score 255")!=std::string::npos);leave();
  // A second committed generation replaces metadata; a failed save cannot.
  s.vars[0]=0;s.vars[3]=0;open(true);select();
  open(false);assert(row(0).find("01  Room 0  Score 0")!=std::string::npos);leave();
  failWrite=true;s.vars[0]=99;s.vars[3]=99;open(true);select();assert(s.modal==mpe4::Message);failWrite=false;select();
  assert(summary(1).room==0&&summary(1).score==0);
  failFlush=true;open(true);key(mpe4::Down);frame();select();assert(s.modal==mpe4::Message);failFlush=false;select();
  assert(summary(2).status==mpe4::SaveEmpty); // leftover TMP is not a save
  const auto savedPath=base/"VMS/AGIVM/SAVES/AGTEST01.SAV",backupPath=base/"VMS/AGIVM/SAVES/AGTEST01.BAK";
  auto corrupt=[](const fs::path &p){std::fstream f(p,std::ios::binary|std::ios::in|std::ios::out);f.seekg(50);char c;f.get(c);f.seekp(50);f.put(c^1);};
  corrupt(savedPath);assert(summary(1).status==mpe4::SaveReady&&summary(1).room==12&&summary(1).score==35);
  open(false);assert(row(0).find("Room 12  Score 35")!=std::string::npos);select();assert(s.vars[0]==12&&s.vars[3]==35);
  corrupt(backupPath);open(false);assert(row(0).find("Unavailable")!=std::string::npos);leave();
  assert(agivm::saveInfo(nullptr,"AGTEST",2,12).status==mpe4::SaveUnavailable);
  // Reconstruct the module against the same disk: occupancy is persistent.
  module=vm_entry(&host);assert(module);frame();open(false);
  assert(row(0).find("Unavailable")!=std::string::npos&&row(1).find("Empty")!=std::string::npos&&row(11).find("Room 255  Score 255")!=std::string::npos);
  leave();guards();
  puts("PASS AGI save slots: empty/occupied room and score, zero/max values, slots 1/12, overwrite, failed write/flush, backup recovery, corrupt/epoch rejection, cold restart, live state unchanged");
 }else if(argc==5){
  assert(frame().cells==1000&&s.modal==mpe4::NoModal&&r.session.parserSplit);
  std::ofstream noBlank(base/"dialog-frames.json");noBlank<<"[";bool comma=false;
  auto stable=[&](){
   const unsigned index=frameCount;auto f=frame();guards();
   assert(r.session.parserSplit&&!f.replace&&f.cells<300);
   if(comma)noBlank<<",";comma=true;noBlank<<index;return f;
  };
  const auto scene=std::vector<uint8_t>(r.session.current,r.session.current+10000);
  for(unsigned repeat=0;repeat<3;repeat++){
   s.vars[210]=1;assert(stable().cells&&s.modal==mpe4::Message);
   // Opening the dialog cannot change scenery or duplicate the parser at
   // its original AGI row. Only this authored 3-row window may be updated.
   for(unsigned cell=0;cell<1000;cell++)if(cell/40<11||cell/40>13||cell%40<4||cell%40>35){
    assert(!memcmp(scene.data()+cell*8,r.session.current+cell*8,8));
    assert(scene[8000+cell]==r.session.current[8000+cell]&&scene[9000+cell]==r.session.current[9000+cell]);
   }
   assert(!stable().cells&&s.modal==mpe4::Message);
   key(mpe4::Enter);assert(stable().cells&&s.modal==mpe4::NoModal);
  }
  s.vars[210]=3;assert(stable().cells&&s.modal==mpe4::Message);
  unsigned timeout=0;while(s.modal!=mpe4::NoModal&&timeout++<40)stable();assert(timeout<=31&&s.modal==mpe4::NoModal);
  noBlank<<"]\n";noBlank.close();
  // A low/tall window must retain every authored row, including the ones
  // normally reserved for the parser. A real layout change remains valid.
  s.vars[210]=2;assert(frame().cells&&s.modal==mpe4::Message&&!r.session.parserSplit);
  assert((s.text[23*40+4]&0xf0)==mpe4::WindowMarker);
  key(mpe4::Enter);frame();assert(s.modal==mpe4::NoModal&&r.session.parserSplit);
  s.vars[210]=4;frame();assert(s.modal==mpe4::Message&&!s.inputEnabled&&!r.session.parserSplit);
  key(mpe4::Enter);frame();assert(s.inputEnabled&&r.session.parserSplit);
  s.vars[210]=5;assert(frame().replace==1&&!s.graphics&&r.session.hires&&!r.session.parserSplit);guards();
  puts("PASS AGI dialogs: centered print open/hold/dismiss and timed close keep the parser layout, delta-only cells, unchanged surrounding scene; low dialogs and authored mode changes remain intact");
 }else if(selected.empty()){
  assert(r.count==41);auto first=frame();assert(first.cells==1000&&first.replace==1);
  // The C64 samples input between frame-end packets. A static picker still
  // needs frame ends, with no bitmap traffic or replacement/blanking flags.
  for(unsigned n=0;n<4;n++){auto idle=frame();assert(!idle.cells&&!idle.replace&&!idle.sprites&&r.selected==0);}
  key(mpe4::Down);auto down=frame();assert(r.selected==1&&down.cells==80&&down.replace==0);
  joystick(1);assert(frame().cells==80&&r.selected==0);
  joystick(1);assert(frame().cells==0&&r.selected==0); // held stick is not a second press
  joystick(2);assert(frame().cells==80&&r.selected==1);
  joystick(0);assert(frame().cells==0&&r.joy==0);
  joystick(8);assert(frame().replace==0&&r.selected==18);
  joystick(8);assert(frame().cells==0&&r.selected==18);
  joystick(4);assert(frame().replace==0&&r.selected==1);
  joystick(0);assert(frame().cells==0);
  key(mpe4::Right);assert(frame().replace==0&&r.selected==18);key(mpe4::Left);frame();assert(r.selected==1);
  // A corrupt selection reports an error in the picker, not a firmware crash.
  auto bad=original.parent_path()/"GAME00.AGI";std::fstream damage(bad,std::ios::binary|std::ios::in|std::ios::out);damage.seekp(64);damage.put(255);damage.close();
  key(mpe4::Enter);frame();assert(!r.game&&strstr(r.message,"INVALID AGI"));
  key(mpe4::Up);frame();assert(r.selected==0);joystick(16);frame();assert(r.game);for(unsigned n=0;n<15;n++)frame();
  assert(s.vars[200]==1&&s.vars[201]>0);assert(!memcmp(s.text+3*40+5,"AGIVM IS RUNNING!",16));
  for(char c:std::string("look")){key(c);frame();}assert(!strcmp(s.input,"look"));key(mpe4::Backspace);frame();assert(!strcmp(s.input,"loo"));key('k');frame();key(mpe4::Enter);frame();assert(!strcmp(s.parsedText,"look"));
  VmInput pointer{77,88,0,4};module->input(&pointer);frame();assert(s.pointerX==77&&s.pointerY==88);
  VmInput joy{0,0,9,2};module->input(&joy);frame();assert(r.joy==9);joy.overflow=0;module->input(&joy);frame();assert(!r.joy);
  s.vars[202]=37;assert(agivm::save(nullptr,"AGTEST",1,1,&s,sizeof s));s.vars[202]=99;assert(agivm::restore(nullptr,"AGTEST",1,1,&s,sizeof s));assert(s.vars[202]==37);
  failWrite=true;assert(!agivm::save(nullptr,"AGTEST",1,1,&s,sizeof s));failWrite=false;
  failFlush=true;assert(!agivm::save(nullptr,"AGTEST",1,1,&s,sizeof s));failFlush=false;
  auto before=s;assert(!agivm::restore(nullptr,"BADID0",1,1,&s,sizeof s));assert(!memcmp(&s,&before,sizeof s));
  // Cached guest resources may be evicted; live state must remain unchanged.
  assert(!agivm::readRaw(nullptr,r.fileBytes,nullptr,1));guards();
  printf("PASS AGI: idle picker frame ends without cells, keyboard/joystick selection/page/fire/release/no blank, exact generic routing, parser/edit, pointer/held input, disk roundtrip/failures, immutable packets; RAM1 support %zu, RAM2 state %zu\n",sizeof r,sizeof s);
 }else{
  unsigned spritePackets=0;for(unsigned n=0;n<1200;n++){spritePackets+=frame().sprites;if(s.modal==mpe4::Message||s.modal==mpe4::Pause||n==120||n==240){key(mpe4::Enter);}guards();}
  assert(r.game&&!strcmp(r.selectedPath,"/PRIVATE/SELECTED.AGI"));assert(r.session.package.ready&&s.scans);
  printf("PASS AGI real content: 1200 frames, room %u, scans %lu, %u sprite packets, %u cache reads / %u hits; no physical gameplay claim\n",s.vars[0],(unsigned long)s.scans,spritePackets,r.cacheReads,r.cacheHits);
 }
}
