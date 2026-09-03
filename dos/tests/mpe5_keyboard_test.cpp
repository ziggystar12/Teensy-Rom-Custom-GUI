// Check the actual guest IRQ1 and BIOS buffer, not just the input queue.
#define main unusedVmAcceptance
#include "mpe5_vm_host_test.cpp"
#undef main

static void requireKey(bool yes, const char *why) {
  if (!yes) throw std::runtime_error(why);
}

static void drainKeys(PagedMachine &machine) {
  for (unsigned n=0;n<500;++n) {
    requireKey(machine.run(1000),"keyboard guest stopped");
    if (!machine.keyboard.count() && regs16[REG_CS]==0x1000 &&
        !MPE5KeyboardAwaitResume && !MPE5KeyboardCooldown) return;
  }
  throw std::runtime_error("keyboard IRQs did not return");
}

static void snapshotKey(PagedMachine &machine, uint8_t ascii, uint8_t scan,
                        uint8_t modifiers=0,uint8_t joy=0,bool repeat=false) {
  requireKey(machine.keyboard.acceptSnapshot(ascii,scan,modifiers,joy,repeat),"snapshot queue full");
  drainKeys(machine);
}

int main(int argc,char **argv) {
  try {
    requireKey(argc==3,"usage:keyboard-test BIOS IMAGE");
    const auto bios=readFile(argv[1]); Image image{readFile(argv[2])};
    PagedMachine machine; machine.start(bios,image); machine.until("C:\\>",true);
    machine.program({0xeb,0xfe}); regs8[FLAG_IF]=1;
    // A game's IRQ1 records every raw make/break at1000:0420.
    const uint8_t irq[]={0x50,0x53,0x2e,0x8b,0x1e,0x00,0x04,0xe4,0x60,
      0x2e,0x88,0x87,0x20,0x04,0x43,0x2e,0x89,0x1e,0x00,0x04,0x5b,0x58,0xcf};
    const auto originalIrq=mpe5_detail::readBits(0x24,4);
    requireKey(machine.pager.write(0x10300,irq,sizeof(irq)),"IRQ write failed");
    mpe5_detail::writeBits(0x24,4,0x10000300);
    mpe5_detail::writeBits(0x10400,2,0);
    snapshotKey(machine,0,0x50);
    requireKey(mpe5_detail::readBits(0x10420,1)==0x50,"Down became another key");
    for(unsigned n=0;n<12;++n)requireKey(machine.run(25000),"held-key guest stopped");
    requireKey(mpe5_detail::readBits(0x10400,2)==1,"timer released or repeated held Down");
    snapshotKey(machine,0,0x50,1);
    requireKey(mpe5_detail::readBits(0x10421,1)==0x36 && (mpe5_detail::readBits(0x417,1)&1),"Shift make/BIOS flags missing");
    snapshotKey(machine,0,0,0);
    requireKey(mpe5_detail::readBits(0x10422,1)==0xb6 && mpe5_detail::readBits(0x10423,1)==0xd0,"Shift/Down releases missing");
    snapshotKey(machine,0,0x4b,0,4); // Same left direction from both sources.
    snapshotKey(machine,0,0,0,4);
    requireKey(mpe5_detail::readBits(0x10400,2)==5,"keyboard release cancelled a held joystick cursor");
    snapshotKey(machine,0,0,0,16);
    snapshotKey(machine,0,0);
    const unsigned total=mpe5_detail::readBits(0x10400,2);
    requireKey(total==8 && mpe5_detail::readBits(0x10425,1)==0xcb &&
      mpe5_detail::readBits(0x10426,1)==0x36 && mpe5_detail::readBits(0x10427,1)==0xb6,
      "joystick left/fire/release mapping failed");
    for(unsigned n=0;n<total;++n)requireKey((mpe5_detail::readBits(0x10420+n,1)&0x7f)!=1,"phantom Escape in raw input");

    // Queue a rapid press/release burst before running the CPU. The real
    // IRQ handler records both its scan and a counter updated by the main
    // program. Every transition must reach IRQ1 promptly, with observable
    // execution in the interrupted program between make and break.
    const uint8_t burstIrq[]={0x50,0x53,0x2e,0x8b,0x1e,0x00,0x04,0xe4,0x60,
      0x2e,0x88,0x87,0x20,0x04,0xd1,0xe3,0x2e,0xa1,0x02,0x04,
      0x2e,0x89,0x87,0x80,0x04,0xd1,0xeb,0x43,
      0x2e,0x89,0x1e,0x00,0x04,0x5b,0x58,0xcf};
    requireKey(machine.pager.write(0x10300,burstIrq,sizeof(burstIrq)),"burst IRQ write failed");
    machine.program({0x2e,0xff,0x06,0x02,0x04,0xeb,0xf9});regs8[FLAG_IF]=1;
    mpe5_detail::writeBits(0x10400,4,0);
    for(unsigned n=0;n<12;++n) {
      requireKey(machine.keyboard.acceptSnapshot('a',0x1e,0,0),"burst make queue failed");
      requireKey(machine.keyboard.acceptSnapshot(0,0,0,0),"burst break queue failed");
    }
    const uint32_t burstStart=inst_counter;
    drainKeys(machine);
    const uint32_t burstInstructions=inst_counter-burstStart;
    requireKey(burstInstructions<60000,"raw keys still depend on the 20000-instruction timer cadence");
    requireKey(mpe5_detail::readBits(0x10400,2)==24,"queued raw key transition lost");
    for(unsigned n=0;n<24;++n) {
      requireKey(mpe5_detail::readBits(0x10420+n,1)==(n&1?0x9e:0x1e),"raw burst reordered make/break");
      if(n) {
        const uint16_t previous=mpe5_detail::readBits(0x10480+(n-1)*2,2);
        const uint16_t current=mpe5_detail::readBits(0x10480+n*2,2);
        requireKey(uint16_t(current-previous)>=128,"make/break starved the main program");
      }
    }
    const uint32_t timerBefore=mpe5_detail::readBits(0x46c,4);
    for(unsigned batch=0;batch<5;++batch) {
      for(unsigned n=0;n<12;++n) {
        requireKey(machine.keyboard.acceptSnapshot('a',0x1e,0,0),"timer-load make queue failed");
        requireKey(machine.keyboard.acceptSnapshot(0,0,0,0),"timer-load break queue failed");
      }
      // Reuse the bounded IRQ recorder while sustained input spans timer ticks.
      mpe5_detail::writeBits(0x10400,2,0);drainKeys(machine);
    }
    requireKey(mpe5_detail::readBits(0x46c,4)>timerBefore,"native input starved BIOS timer ticks");

    // Restore the real BIOS handler and verify ASCII/scan words consumed by DOS.
    mpe5_detail::writeBits(0x24,4,originalIrq);
    mpe5_detail::writeBits(0x41a,2,0x1e);mpe5_detail::writeBits(0x41c,2,0x1e);
    snapshotKey(machine,'A',0x1e,1);
    snapshotKey(machine,'A',0x1e,1,0,true);
    snapshotKey(machine,0,0x48);snapshotKey(machine,0,0);
    requireKey(mpe5_detail::readBits(0x41c,2)==0x24 &&
      mpe5_detail::readBits(0x41e,2)==0x1e41 && mpe5_detail::readBits(0x420,2)==0x1e41 &&
      mpe5_detail::readBits(0x422,2)==0x4800,"BIOS uppercase/repeat/arrow scan words incorrect");
    requireKey((mpe5_detail::readBits(0x417,1)&15)==0,"modifier stuck after release");

    // Cold/repeated launch must clear native IRQ pacing just like held keys.
    MPE5KeyboardAwaitResume=true;MPE5KeyboardCooldown=123;
    machine.start(bios,image);
    requireKey(!MPE5KeyboardAwaitResume&&!MPE5KeyboardCooldown,"restart retained raw-key pacing");

    // An absent gameport must not echo OUT values as held fire/abort buttons.
    machine.program({0xba,0x01,0x02,0xb0,0x00,0xee,0xec});
    requireKey(machine.run(4)&&regs8[REG_AL]==255,"absent gameport returned pressed buttons");

    // Queue pressure must reject an entire snapshot, retaining release state.
    mpe5::Keyboard keyboard; keyboard.clear();
    for(unsigned n=0;n<31;++n)requireKey(keyboard.push({'a',0}),"queue fixture failed");
    requireKey(!keyboard.acceptSnapshot('A',0x1e,1,0)&&keyboard.count()==31,"partial snapshot accepted");
    mpe5::Key key;while(keyboard.pop(key)){}
    requireKey(keyboard.acceptSnapshot('A',0x1e,1,0)&&keyboard.count()==2,"rejected snapshot retained hidden held state");
    keyboard.clear();requireKey(keyboard.acceptSnapshot(0,0,0,0)&&!keyboard.count(),"clear retained held keys");
    std::cout<<"Native keyboard PASS: actual IRQ1 arrows/held/release/Shift, joystick merge/fire, BIOS uppercase/repeat, no Escape, atomic queue and absent gameport;24 queued transitions in "<<burstInstructions<<" instructions with main-program observation between events and continued BIOS timer ticks.\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"Native keyboard FAILED: "<<error.what()<<'\n';return 1;
  }
}
