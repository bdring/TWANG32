#include "audio_out.h"
#include "../../config.h"

#if defined(AUDIO_OUT_DAC)
#include "driver/dac_continuous.h"
#elif defined(AUDIO_OUT_I2S)
#include <ESP_I2S.h>
#endif

namespace audio_out {

static tw::Synth* s_synth = nullptr;
static uint32_t s_underruns = 0;
static const int BLOCK = 256;

#if defined(AUDIO_OUT_DAC)
static dac_continuous_handle_t s_dac = nullptr;

static void task(void*) {
  static int16_t pcm[BLOCK];
  static uint8_t out[BLOCK];
  int32_t err = 0;
  for (;;) {
    s_synth->render(pcm, BLOCK);
    // 16 -> 8 bit with first-order noise shaping: pushes the quantisation hiss up out of the way
    for (int i = 0; i < BLOCK; i++) {
      int32_t v = (int32_t)pcm[i] + err;
      int32_t q = (v + 128) >> 8;
      if (q > 127) q = 127;
      if (q < -128) q = -128;
      err = v - (q << 8);
      out[i] = (uint8_t)(q + 128);
    }
    size_t loaded = 0;
    if (dac_continuous_write(s_dac, out, BLOCK, &loaded, 100) != ESP_OK) s_underruns++;
  }
}

bool begin(tw::Synth* synth, int rate) {
  s_synth = synth;
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = DAC_CHANNEL_MASK_CH0;  // GPIO25
  cfg.desc_num = 4;
  cfg.buf_size = 1024;
  cfg.freq_hz = (uint32_t)rate;
  cfg.offset = 0;
  cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
  if (dac_continuous_new_channels(&cfg, &s_dac) != ESP_OK) return false;
  if (dac_continuous_enable(s_dac) != ESP_OK) return false;
  xTaskCreatePinnedToCore(task, "audio", 4096, nullptr, 5, nullptr, 0);
  return true;
}

const char* description() { return "ESP32 DAC on GPIO25 (8-bit)"; }

#elif defined(AUDIO_OUT_I2S)
static I2SClass s_i2s;

static void task(void*) {
  static int16_t pcm[BLOCK];
  for (;;) {
    s_synth->render(pcm, BLOCK);
    size_t w = s_i2s.write((const uint8_t*)pcm, sizeof(pcm));
    if (w < sizeof(pcm)) s_underruns++;
  }
}

bool begin(tw::Synth* synth, int rate) {
  s_synth = synth;
  s_i2s.setPins(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
  if (!s_i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) return false;
  xTaskCreatePinnedToCore(task, "audio", 4096, nullptr, 5, nullptr, 0);
  return true;
}

const char* description() { return "I2S amplifier (16-bit)"; }
#endif

uint32_t underruns() { return s_underruns; }

}  // namespace audio_out
