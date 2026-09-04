#ifndef MPE5_SPEAKER_H
#define MPE5_SPEAKER_H

#include "mpe5_platform.h"

namespace mpe5 {

// Existing M3 SID payload: retrigger mask, then the 25 D400-D418 registers.
// The C64 receiver writes these bytes directly; it does not convert clocks.
class SpeakerSid {
 public:
  static constexpr uint8_t PayloadBytes = 26;
  static constexpr uint32_t NtscClockHz = 1022727u, PalClockHz = 985248u;
  MPE5_CODE void reset();
  MPE5_CODE void render(const PcSpeaker &speaker, uint8_t payload[PayloadBytes],
                       uint32_t sidClockHz = NtscClockHz);
  // A live Tandy PSG owns all three SID voices. PC speaker remains the
  // voice-one fallback when no Tandy tone is audible.
  MPE5_CODE void render(const PcSpeaker &speaker, const TandyPsg *tandy,
                       uint8_t payload[PayloadBytes], uint32_t sidClockHz = NtscClockHz);
  MPE5_CODE static uint16_t frequencyRegister(uint32_t pitCount, uint32_t sidClockHz);
  MPE5_CODE static uint16_t tandyFrequencyRegister(uint16_t period, uint32_t sidClockHz);
 private:
  uint32_t previousStart = 0;
  uint32_t previousTandyStart[3]{};
  bool tandyPlaying[3]{};
  bool playing = false;
};

}  // namespace mpe5
#endif
