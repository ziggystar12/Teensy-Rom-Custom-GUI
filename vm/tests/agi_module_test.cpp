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
 assert(argc==3||argc==4);base=argv[2];assert(base.string().find("agi-sandbox-")!=std::string::npos);
 fs::create_directories(base/"VMS");fs::copy(fs::path(argv[1])/"VMS/AGIVM",base/"VMS/AGIVM",fs::copy_options::recursive);
 wire.open(base/"wire.bin",std::ios::binary);
 auto original=base/"VMS/AGIVM/GAMES/AGITEST.AGI";
 if(argc==3)for(unsigned n=0;n<40;n++){char name[32];snprintf(name,sizeof name,"GAME%02u.AGI",n);fs::copy_file(original,original.parent_path()/name);}
 std::string selected=argc==4?argv[3]:"";if(!selected.empty()){fs::create_directories(base/"PRIVATE");fs::copy_file(selected,base/"PRIVATE/SELECTED.AGI");}
 memset(support,0xa5,sizeof support);memset(guest,0x5a,sizeof guest);
 VmHost host{VM_ABI,agivm::RequiredHostBytes,VM_SERVICES,support+32,VM_DATA_BYTES,"/VMS/AGIVM",selected.empty()?"":"/PRIVATE/SELECTED.AGI",now,VmFiles::openFile,VmFiles::readFile,VmFiles::nextFile,VmFiles::closeFile,
  guest+32,VM_RAM_BYTES,VmFiles::openFlags,VmFiles::writeFile,VmFiles::fileOp,yield,failed};
 auto shortHost=host;shortHost.bytes--;assert(!vm_entry(&shortHost));
 module=vm_entry(&host);check();assert(module);auto &r=*agivm::runtime;auto &s=r.session.game.state;
 assert((uint8_t *)&s==guest+32);assert((uint8_t *)&r>=support+32&&(uint8_t *)&r+sizeof r<=support+32+VM_DATA_BYTES);
 VmRegistry::Launch request{};assert(VmRegistry::find("agi",nullptr,request)==1&&VmRegistry::preflight(request));VmRegistry::refresh(true);assert(VmRegistry::associated("Game.AGI"));
 assert(VmRegistry::tryLaunch(rmtSD,"/VMS/AGIVM/GAMES","AGITEST.AGI"));assert(rebooted);VmRegistry::Launch launch{};assert(VmRegistry::consume(launch));assert(!strcmp(launch.content,"/VMS/AGIVM/GAMES/AGITEST.AGI"));
 if(selected.empty()){
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
