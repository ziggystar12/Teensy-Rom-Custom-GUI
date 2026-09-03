#define main existingDosAcceptanceMain
#include "mpe5_vm_host_test.cpp"
#undef main
#include <iomanip>

namespace {
// These offsets describe the exact BOULDER.EXE pinned by the image builder.
// Its IRQ1 handler at CS:00A0 loads the data segment from CS:0000. The game
// updates X/Y at DS:2544/253A in its actual movement routine, and DS:272E is
// the life count. Assertions observe guest state; input only enters through
// the same keyboard snapshot queue used by the firmware.
void check(bool value,const char *message) {
  if(!value) throw std::runtime_error(message);
}
void instructions(PagedMachine &m, uint32_t count) {
  const uint32_t begin=inst_counter;
  for(unsigned slice=0;slice<1000000 && uint32_t(inst_counter-begin)<count;++slice)
    if(!m.run(std::min(25000u,count-uint32_t(inst_counter-begin))))
      throw std::runtime_error("Boulder CPU stopped");
  if(uint32_t(inst_counter-begin)<count) throw std::runtime_error("Boulder instruction bound");
}
uint32_t dataBase() {
  const uint16_t gameCS=mpe5_detail::readBits(0x26,2);
  return mpe5_detail::readBits(uint32_t(gameCS)*16u,2)*16u;
}
void report(PagedMachine &m,const char *label,uint32_t base) {
  const auto byte=[base](unsigned offset){return mpe5_detail::readBits(base+offset,1);};
  std::cout<<label<<" inst="<<inst_counter<<" CS:IP="<<std::hex<<regs16[REG_CS]<<':'<<reg_ip
    <<" DS="<<(base>>4)<<" key="<<byte(0x272c)<<" shift="<<byte(0x6e9)<<std::dec
    <<" life="<<byte(0x272e)<<" pos="<<byte(0x2544)<<','<<byte(0x253a)
    <<" joy="<<byte(0x12c0)<<" direction="<<byte(0x12c1)<<" fire="<<byte(0x12c2)<<" abort="<<byte(0x12c3)
    <<" queue="<<unsigned(m.keyboard.count());
  std::cout<<std::endl;
}
void input(PagedMachine &m,uint8_t ascii=0,uint8_t scan=0,uint8_t modifiers=0,uint8_t joystick=0) {
  check(m.keyboard.acceptSnapshot(ascii,scan,modifiers,joystick),"keyboard state queue overflowed");
}
unsigned byteAt(uint32_t base,unsigned offset) {return mpe5_detail::readBits(base+offset,1);}
unsigned position(uint32_t base) {return byteAt(base,0x253a)*256u+byteAt(base,0x2544);}
void release(PagedMachine &m,uint32_t base) {
  input(m); instructions(m,1000000);
  check(!byteAt(base,0x6e9)&&!byteAt(base,0x12c1),"released direction or Shift remained held");
}
}

int main(int argc,char **argv) {
  try {
    if(argc!=3) throw std::runtime_error("usage: controls BIOS IMAGE");
    const auto bios=readFile(argv[1]); Image image{readFile(argv[2])};
    PagedMachine m; m.start(bios,image); m.until("C:\\>",true);
    queue(m.keyboard,"BOULDER\r"); instructions(m,12500000);
    const uint32_t base=dataBase();
    check(mpe5_detail::readBits(0x24,2)==0xa0 && base>0x10000 && base<0x80000,
          "expected Boulder IRQ1/data signature missing");
    report(m,"title",base);
    // Space skips the introduction; Shift/fire starts the selected cave.
    // Space pauses during gameplay and must never be synthesized as fire.
    input(m,' ',0x39); instructions(m,500000); input(m); instructions(m,500000);
    input(m,0,0,1); instructions(m,1000000); input(m); instructions(m,25000000);
    report(m,"cave",base);
    check(byteAt(base,0x272e)==3 && position(base)==0x0203,
          "first cave did not spawn with three lives at (3,2)");
    for(unsigned phase=0;phase<4;++phase) {
      instructions(m,2500000);
      check(byteAt(base,0x272e)==3 && position(base)==0x0203 &&
            !byteAt(base,0x12c0)&&!byteAt(base,0x12c1)&&!byteAt(base,0x12c2)&&!byteAt(base,0x12c3),
            "neutral input caused phantom movement/fire/ESC or restarted the cave");
    }
    const auto move=[&](uint8_t scan,unsigned axis,bool positive,const char *label) {
      const unsigned before=byteAt(base,axis);
      input(m,0,scan); instructions(m,1000000); report(m,label,base);
      const unsigned after=byteAt(base,axis);
      check(positive ? after>before : after<before,"held cursor key did not move the guest player");
      release(m,base); const unsigned stopped=position(base); instructions(m,1000000);
      check(position(base)==stopped,"player continued moving after cursor release");
    };
    move(0x50,0x253a,true,"Down");
    move(0x4b,0x2544,false,"Left");
    move(0x48,0x253a,false,"Up");
    const unsigned beforeGrab=position(base);
    input(m,0,0x4d,1); instructions(m,1000000); report(m,"Shift+Right grab",base);
    check(byteAt(base,0x6e9)==1 && byteAt(base,0x12c1)==2 && position(base)==beforeGrab,
          "Shift+cursor did not preserve the player's position while grabbing");
    input(m,0,0x4d); instructions(m,1000000); report(m,"release Shift, hold Right",base);
    check(!byteAt(base,0x6e9)&&byteAt(base,0x2544)>(beforeGrab&255u),
          "releasing Shift while holding Right did not resume movement");
    release(m,base);
    const unsigned beforeJoystick=byteAt(base,0x253a);
    input(m,0,0,0,1); instructions(m,1000000); report(m,"port2 joystick Up",base);
    check(byteAt(base,0x253a)<beforeJoystick,"port2 joystick snapshot did not become a working cursor key");
    release(m,base);
    input(m,0,0,0,16); instructions(m,500000);
    check(byteAt(base,0x6e9)==1,"port2 fire did not map to Shift");
    release(m,base); instructions(m,5000000); report(m,"final neutral",base);
    check(byteAt(base,0x272e)==3 && !byteAt(base,0x12c3),"controls lost lives or left ESC/abort held");
    check(mpe5::coreDiagnostic().reason==mpe5::CoreStop::None,"Boulder core diagnostic failed");
    mpe5::coreReset();
    std::cout<<"Boulder controls PASS: exact game, stable three lives, held arrows and releases, Shift grab, port2 cursor/fire mapping.\n";
    return 0;
  } catch(const std::exception &e) {std::cerr<<"Boulder controls FAILED: "<<e.what()<<std::endl;return 1;}
}
