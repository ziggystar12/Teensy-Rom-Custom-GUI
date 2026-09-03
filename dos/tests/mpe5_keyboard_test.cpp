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
    if (!machine.keyboard.count() && regs16[REG_CS]==0x1000) return;
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
    std::cout<<"Native keyboard PASS: actual IRQ1 arrows/held/release/Shift, joystick merge/fire, BIOS uppercase/repeat, no Escape, atomic queue and absent gameport.\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"Native keyboard FAILED: "<<error.what()<<'\n';return 1;
  }
}
