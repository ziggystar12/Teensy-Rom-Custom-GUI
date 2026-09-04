// Execute genuine 8086 OUT instructions through the production core hooks.
#define main existingVmAcceptance
#include "mpe5_vm_host_test.cpp"
#undef main
#include "../../engine/native-dos/mpe5_speaker.cpp"

static uint32_t speakerBefore;
static bool speakerYield(void *context) {
  const auto &machine = *static_cast<PagedMachine *>(context);
  return machine.speaker.revision() != speakerBefore;
}
int main(int argc, char **argv) {
  try {
    if(argc != 2) throw std::runtime_error("speaker core test requires BIOS");
    const auto bios = readFile(argv[1]);
    Image image{std::vector<uint8_t>(512)};
    PagedMachine machine; machine.start(bios, image);
    MPE5Host.memory.shouldYield = speakerYield;
    machine.program({
      0xb0,0xb6,0xe6,0x43, // MOV AL,B6 / OUT 43,AL: channel2 lo/hi, mode3
      0xb0,0xa9,0xe6,0x42, // Low byte of 1193: approximately 1 kHz
      0xb0,0x04,0xe6,0x42, // High byte commits the complete divisor
      0xb0,0x03,0xe6,0x61, // Open PIT gate and speaker data-enable
      0xb0,0x00,0xe6,0x61, // Close gate; a separate yield must expose silence
      0xeb,0xfe
    });
    speakerBefore = machine.speaker.revision();
    if(!machine.run(1000) || !machine.speaker.active() || machine.speaker.reload() != 1193 ||
       machine.speaker.revision() != speakerBefore+1)
      throw std::runtime_error("8086 PIT/gate OUT did not yield an audible tone");
    mpe5::SpeakerSid sid; uint8_t payload[26]{};
    sid.render(machine.speaker, payload);
    if(payload[5] != 0x41 || payload[25] != 15 || !(payload[1] | payload[2]))
      throw std::runtime_error("8086 speaker activity did not generate SID pulse registers");
    speakerBefore = machine.speaker.revision();
    if(!machine.run(1000) || machine.speaker.active() || machine.speaker.revision() != speakerBefore+1)
      throw std::runtime_error("8086 gate-off OUT did not yield a separate silence event");
    sid.render(machine.speaker, payload);
    if(std::any_of(payload, payload+26, [](uint8_t value){return value != 0;}))
      throw std::runtime_error("8086 gate-off left an audible SID register");
    // The PSG uses its own write-only C0h port. Exercise real DX-port OUT
    // opcodes rather than directly calling the device so this covers the
    // production adapter and its three SID voice allocation.
    mpe5::TandyPsg tandy; MPE5Host.tandy=&tandy; MPE5Host.memory.shouldYield=nullptr;
    machine.program({
      0xba,0xc0,0x00,
      0xb0,0x8a,0xee, 0xb0,0x10,0xee, 0xb0,0x94,0xee,
      0xb0,0xa8,0xee, 0xb0,0x12,0xee, 0xb0,0xb8,0xee,
      0xb0,0xc7,0xee, 0xb0,0x15,0xee, 0xb0,0xd2,0xee, 0xeb,0xfe
    });
    if(!machine.run(1000) || !tandy.active(0) || !tandy.active(1) || !tandy.active(2))
      throw std::runtime_error("8086 OUT C0 did not program three Tandy tones");
    sid.reset(); sid.render(machine.speaker,&tandy,payload);
    if(payload[0]!=7 || payload[5]!=0x41 || payload[12]!=0x41 || payload[19]!=0x41 ||
       payload[7]!=0xb0 || payload[14]!=0x70 || payload[21]!=0xd0 || payload[25]!=15)
      throw std::runtime_error("8086 Tandy PSG did not publish all three SID voices");
    speakerBefore = machine.speaker.revision();
    if(!machine.run(1000) || machine.speaker.revision() != speakerBefore)
      throw std::runtime_error("idle core invented a speaker transition");
    std::cout << "MPE5 x86 speaker regression passed: real OUT42/43/61 and C0, separate tone/off yields, PC and three-voice Tandy SID packets.\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
