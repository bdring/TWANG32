// TWANG Next - audio output task (ESP32 DAC or an I2S amplifier)
#pragma once
#include <Arduino.h>
#include "../core/tw_synth.h"

namespace audio_out {
// Starts the audio task on core 0. It pulls samples from the synth forever.
bool begin(tw::Synth* synth, int sampleRate);
const char* description();
uint32_t underruns();
}  // namespace audio_out
