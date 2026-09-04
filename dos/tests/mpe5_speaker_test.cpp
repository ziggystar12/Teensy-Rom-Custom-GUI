#include "../../engine/native-dos/mpe5_platform.h"
#include "../../engine/native-dos/mpe5_speaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
static void load(mpe5::PcSpeaker &speaker, uint8_t control, uint16_t count) {
  speaker.write(0x43, control);
  speaker.write(0x42, uint8_t(count));
  speaker.write(0x42, uint8_t(count >> 8));
}
static void pitProgramming() {
  mpe5::PcSpeaker speaker;
  require(!speaker.active() && !speaker.frequencyHz(), "cold speaker must be silent");
  require(!speaker.write(0x61, 3), "gate without loaded count must be silent");
  require(!speaker.write(0x43, 0xb6), "control word alone must not start a tone");
  require(!speaker.write(0x42, 0x34) && !speaker.active(), "first low byte must not commit");
  require(speaker.write(0x42, 0x12), "complete reload must start a tone");
  require(speaker.reload() == 0x1234 && speaker.active(), "initial lo/hi reload");
  require(speaker.revision() == 1 && speaker.restartToken() == 1, "initial audible transition");
  require(!speaker.write(0x3d9, 0x30) && speaker.revision() == 1, "CGA palette writes do not touch sound");
  require(!speaker.write(0x42, 0x78) && speaker.reload() == 0x1234, "running reload retains old pitch until high byte");
  require(!speaker.write(0x43, 0x80), "counter latch must not disturb partial reload");
  require(!speaker.write(0x43, 0x36) && !speaker.write(0x43, 0xf6), "other channel/readback must not disturb channel two");
  require(!speaker.write(0x61, 0x83), "unrelated port61 bits must not change tone");
  require(speaker.write(0x61, 0x82) && !speaker.active(), "PIT gate low must stop tone");
  require(speaker.write(0x61, 0x83) && speaker.reload() == 0x1234, "gate high restarts retained count");
  require(speaker.write(0x42, 0x56) && speaker.reload() == 0x5678, "gate writes preserve partial reload");
  require(speaker.revision() == 4 && speaker.restartToken() == 2, "only completed audible changes count");
  require(speaker.write(0x61, 1) && !speaker.active(), "speaker data-enable low must silence");
  speaker.write(0x61, 3);
  require(speaker.write(0x43, 0x96) && !speaker.active(), "new control waits for programmed count");
  speaker.write(0x42, 0x34);
  require(speaker.reload() == 0x34, "LSB-only access clears the high count byte");
  speaker.write(0x43, 0xa6); speaker.write(0x42, 0x12);
  require(speaker.reload() == 0x1200, "MSB-only access clears the low count byte");
  load(speaker, 0xb6, 0);
  require(speaker.active() && speaker.effectiveReload() == 65536 && speaker.frequencyHz() == 18,
          "binary zero reload means 65536, not silence");
  load(speaker, 0xb7, 0x1234);
  require(speaker.active() && speaker.effectiveReload() == 1234, "packed BCD count");
  load(speaker, 0xb7, 0);
  require(speaker.active() && speaker.effectiveReload() == 10000, "BCD zero reload means 10000");
  load(speaker, 0xb7, 0x001a);
  require(!speaker.active(), "invalid BCD must not produce an invented pitch");
  for(uint8_t mode : {0xb4, 0xb6, 0xbc, 0xbe}) {
    load(speaker, mode, 2712);
    require(speaker.active() && speaker.frequencyHz() == 439, "periodic mode and alias tone");
  }
  for(uint8_t mode : {0xb0, 0xb2, 0xb8, 0xba}) {
    load(speaker, mode, 2712);
    require(!speaker.active(), "nonperiodic modes cannot become continuous tones");
  }
  speaker = {};
  require(!speaker.active() && !speaker.revision() && !speaker.restartToken(), "session reset clears speaker state");
}
static void sidRendering() {
  mpe5::PcSpeaker speaker;
  mpe5::SpeakerSid sid;
  std::array<uint8_t,28> guarded{};
  guarded.front() = 0xa5; guarded.back() = 0x5a;
  auto *packet = guarded.data()+1;
  sid.render(speaker, packet);
  require(std::all_of(packet, packet+26, [](uint8_t value){return value == 0;}), "silent packet clears all SID registers");
  speaker.write(0x61, 3); load(speaker, 0xb6, 2712);
  sid.render(speaker, packet);
  require(packet[0] == 1 && packet[3] == 0 && packet[4] == 8 && packet[5] == 0x41 &&
          packet[6] == 0 && packet[7] == 0xf0 && packet[25] == 15, "M3 pulse/gate/envelope/volume layout");
  require(std::all_of(packet+8, packet+25, [](uint8_t value){return value == 0;}), "voices two/three and filter remain off");
  const auto held = guarded;
  sid.render(speaker, packet);
  require(packet[0] == 0 && !memcmp(packet+1, held.data()+2, 25), "held note must not restart its envelope");
  speaker.write(0x42, 0x00); speaker.write(0x42, 0x10);
  sid.render(speaker, packet);
  require(packet[0] == 0 && packet[5] == 0x41, "pitch update preserves continuous envelope");
  speaker.write(0x61, 0); speaker.write(0x61, 3);
  sid.render(speaker, packet);
  require(packet[0] == 1, "gate restart between publications must retrigger");
  speaker.write(0x61, 0); sid.render(speaker, packet);
  require(std::all_of(packet, packet+26, [](uint8_t value){return value == 0;}), "speaker off cannot leave a hanging SID tone");
  require(guarded.front() == 0xa5 && guarded.back() == 0x5a, "SID adapter writes exactly 26 bytes");
  speaker.write(0x61, 3); sid.reset(); sid.render(speaker, packet);
  require(packet[0] == 1, "fresh session retriggers a live tone");
  sid.render(speaker, packet, 0);
  require(std::all_of(packet, packet+26, [](uint8_t value){return value == 0;}), "invalid clock fails silent");
  for(uint32_t clock : {mpe5::SpeakerSid::NtscClockHz, mpe5::SpeakerSid::PalClockHz}) {
    for(uint32_t count : {320u, 1000u, 2712u, 10000u, 65536u}) {
      const uint16_t frequency = mpe5::SpeakerSid::frequencyRegister(count, clock);
      const double actual = double(frequency)*clock/16777216.0;
      const double expected = double(mpe5::PcSpeaker::ClockHz)/count;
      require(std::abs(actual-expected) <= double(clock)/33554432.0+0.000001, "PAL/NTSC frequency rounds within half a SID step");
    }
  }
  require(mpe5::SpeakerSid::frequencyRegister(2, mpe5::SpeakerSid::NtscClockHz) == 65535,
          "out-of-range pitch saturates instead of wrapping");
  require(mpe5::SpeakerSid::frequencyRegister(0, mpe5::SpeakerSid::NtscClockHz) == 0,
          "invalid divisor fails silent");
}
static void tandyProgramming() {
  mpe5::TandyPsg tandy;
  require(!tandy.active() && !tandy.revision(), "cold Tandy PSG must be silent");
  require(!tandy.write(0x8a) && !tandy.write(0x10), "muted Tandy tone setup must stay quiet");
  require(tandy.period(0)==0x10a && !tandy.active(0), "Tandy low/high period latching failed");
  require(tandy.write(0x94) && tandy.active(0) && tandy.attenuation(0)==4 &&
          tandy.restartToken(0)==1, "Tandy volume latch did not start voice one");
  require(!tandy.write(0x94), "unchanged Tandy volume invented a sound event");
  tandy.write(0xa8); tandy.write(0x12); tandy.write(0xb8);
  tandy.write(0xc7); tandy.write(0x15); tandy.write(0xd2);
  require(tandy.active(0) && tandy.active(1) && tandy.active(2) &&
          tandy.period(1)==0x128 && tandy.period(2)==0x157,
          "Tandy three tone latches did not retain periods");
  mpe5::PcSpeaker pc;
  mpe5::SpeakerSid sid;
  uint8_t payload[26]{};
  sid.render(pc,&tandy,payload);
  require(payload[0]==7 && payload[5]==0x41 && payload[12]==0x41 && payload[19]==0x41 &&
          payload[7]==0xb0 && payload[14]==0x70 && payload[21]==0xd0 && payload[25]==15,
          "Tandy tones did not map to all three SID voices with inverse attenuation");
  sid.render(pc,&tandy,payload);
  require(payload[0]==0 && payload[1] && payload[8] && payload[15],
          "held Tandy tones retriggered or lost SID pitches");
  const uint16_t registerValue=mpe5::SpeakerSid::tandyFrequencyRegister(0x10a,mpe5::SpeakerSid::NtscClockHz);
  const double actual=double(registerValue)*mpe5::SpeakerSid::NtscClockHz/16777216.0;
  const double expected=double(mpe5::TandyPsg::ClockHz)/(32.0*0x10a);
  require(std::abs(actual-expected)<=double(mpe5::SpeakerSid::NtscClockHz)/33554432.0+0.000001,
          "Tandy SID frequency conversion is outside half a SID step");
  require(tandy.write(0x9f) && !tandy.active(0), "Tandy mute latch did not silence voice one");
  sid.render(pc,&tandy,payload);
  require(!(payload[1]|payload[2]|payload[5]) && payload[12]==0x41 && payload[19]==0x41,
          "Tandy mute did not clear only its SID voice");
}
int main() {
  try {
    pitProgramming(); sidRendering(); tandyProgramming();
    std::cout << "MPE5 speaker regression passed: atomic PIT reload, gate/restart/latch/BCD, "
                 "PAL/NTSC M3 SID pulse conversion, three Tandy PSG voices, sustained/retriggered/silent packets; "
              << sizeof(mpe5::PcSpeaker) << " speaker bytes + " << sizeof(mpe5::SpeakerSid) << " adapter bytes.\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
