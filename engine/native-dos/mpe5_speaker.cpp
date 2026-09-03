#include "mpe5_speaker.h"

#include <string.h>

namespace mpe5 {

void SpeakerSid::reset() { previousStart = 0; playing = false; }

uint16_t SpeakerSid::frequencyRegister(uint32_t pitCount, uint32_t sidClockHz) {
  if (!pitCount || !sidClockHz) return 0;
  // Convert the original PIT ratio without truncating to whole Hz first:
  // SID Fout = register * clock / 2^24. Saturate above the SID's range;
  // wrapping its 16-bit register would turn high PC tones into low notes.
  const uint64_t denominator = uint64_t(pitCount) * sidClockHz;
  const uint64_t numerator = uint64_t(PcSpeaker::ClockHz) << 24;
  const uint64_t value = (numerator + denominator / 2u) / denominator;
  return uint16_t(value > 65535u ? 65535u : value);
}

void SpeakerSid::render(const PcSpeaker &speaker, uint8_t payload[PayloadBytes],
                        uint32_t sidClockHz) {
  memset(payload, 0, PayloadBytes);
  const uint16_t frequency = speaker.active() ?
      frequencyRegister(speaker.effectiveReload(), sidClockHz) : 0;
  if (frequency) {
    payload[0] = !playing || speaker.restartToken() != previousStart ? 1u : 0u;
    payload[1] = uint8_t(frequency);
    payload[2] = uint8_t(frequency >> 8);
    payload[4] = 0x08; // Voice 1 pulse width $800: 50% square wave.
    payload[5] = 0x41; // Pulse waveform and envelope gate.
    payload[7] = 0xf0; // Full sustain; fastest attack/decay/release.
    payload[25] = 0x0f; // Master volume; filters and other voices stay off.
  }
  // A stopped speaker clears volume as well as gate, silencing it without
  // waiting for a SID envelope release. Held notes do not retrigger ADSR.
  playing = frequency != 0;
  previousStart = speaker.restartToken();
}

}  // namespace mpe5
