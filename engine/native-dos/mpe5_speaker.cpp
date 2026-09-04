#include "mpe5_speaker.h"

#include <string.h>

namespace mpe5 {

void SpeakerSid::reset() {
  previousStart = 0; playing = false;
  memset(previousTandyStart, 0, sizeof(previousTandyStart));
  memset(tandyPlaying, 0, sizeof(tandyPlaying));
}

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
  render(speaker, nullptr, payload, sidClockHz);
}

uint16_t SpeakerSid::tandyFrequencyRegister(uint16_t period, uint32_t sidClockHz) {
  if (!period || !sidClockHz) return 0;
  const uint64_t denominator = uint64_t(period) * 32u * sidClockHz;
  const uint64_t numerator = uint64_t(TandyPsg::ClockHz) << 24u;
  const uint64_t value = (numerator + denominator / 2u) / denominator;
  return uint16_t(value > 65535u ? 65535u : value);
}

void SpeakerSid::render(const PcSpeaker &speaker, const TandyPsg *tandy,
                        uint8_t payload[PayloadBytes], uint32_t sidClockHz) {
  memset(payload, 0, PayloadBytes);
  if (tandy && tandy->active() && sidClockHz) {
    for (uint8_t voice = 0; voice < 3u; ++voice) {
      const uint16_t frequency = tandy->active(voice) ?
          tandyFrequencyRegister(tandy->period(voice), sidClockHz) : 0;
      if (!frequency) { tandyPlaying[voice] = false; continue; }
      const uint8_t base = uint8_t(1u + voice * 7u);
      payload[0] |= (!tandyPlaying[voice] ||
          previousTandyStart[voice] != tandy->restartToken(voice)) ? uint8_t(1u << voice) : 0u;
      payload[base] = uint8_t(frequency);
      payload[base + 1u] = uint8_t(frequency >> 8u);
      payload[base + 3u] = 0x08; // 50% pulse width.
      payload[base + 4u] = 0x41; // Pulse waveform and gate.
      payload[base + 6u] = uint8_t((15u - tandy->attenuation(voice)) << 4u);
      previousTandyStart[voice] = tandy->restartToken(voice);
      tandyPlaying[voice] = true;
    }
    previousStart = speaker.restartToken(); playing = false;
    payload[25] = 0x0f;
    return;
  }
  memset(tandyPlaying, 0, sizeof(tandyPlaying));
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
