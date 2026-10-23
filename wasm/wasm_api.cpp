// TWANG Next - WebAssembly bindings for the browser simulator and level editor.
// The exact same engine and synth code that runs on the ESP32.
#include "../TWANG32/src/core/tw_game.h"
#include "../TWANG32/src/core/tw_synth.h"

using namespace tw;

#define EXPORT extern "C" __attribute__((visibility("default")))

static Game g_game;
static Synth g_synth;
static SoundDirector g_sound;
static Campaign g_campaign;
static Campaign g_scratch;
static char g_text[192 * 1024];
static RGB g_fb[1000];
static int16_t g_pcm[4096];
static float g_pcmF[4096];
static Event g_events[64];
static int g_nEvents = 0;
static ParseResult g_parse;
static Input g_input;

EXPORT char* tw_text_buffer() { return g_text; }
EXPORT int tw_text_capacity() { return (int)sizeof(g_text); }

EXPORT void tw_init(int sampleRate, unsigned seed) {
  g_synth.init(sampleRate);
  g_synth.volumes(0.8f, 0.6f, 0.9f);
  g_sound.attach(&g_synth);
  g_campaign.count = 0;
  g_game.init(&g_campaign, seed);
}

EXPORT int tw_load_campaign(int len) {
  g_parse = parseCampaign(g_text, (size_t)len, g_scratch);
  if (!g_parse.ok) return -1;
  memcopy(&g_campaign, &g_scratch, sizeof(Campaign));
  g_game.setCampaign(&g_campaign);
  return g_campaign.count;
}

// Validate without loading (editor uses this to show errors as you type)
EXPORT int tw_validate_campaign(int len) {
  g_parse = parseCampaign(g_text, (size_t)len, g_scratch);
  return g_parse.ok ? g_scratch.count : -1;
}

EXPORT const char* tw_parse_message() { return g_parse.message; }
EXPORT int tw_parse_warnings() { return g_parse.warnings; }

EXPORT int tw_test_level(int len, int spectate) {
  static LevelDef L;
  g_parse = parseLevel(g_text, (size_t)len, L);
  if (!g_parse.ok) return -1;
  g_game.testLevel(L, spectate != 0);
  return 1;
}

EXPORT void tw_start(int mode, int level) { g_game.startRun((Mode)mode, level); }
EXPORT void tw_boot() { g_game.boot(); }
EXPORT void tw_attract() { g_game.goAttract(); }
EXPORT void tw_pause(int p) { g_game.setPaused(p != 0); }
EXPORT void tw_skip() { g_game.skipLevel(); }

EXPORT void tw_set_input(float tilt, int attack, int active) {
  g_input.tilt = tilt;
  g_input.attack = attack != 0;
  g_input.active = active != 0;
}

// One 60 Hz game step. Returns the number of events produced (read them with tw_event_*).
EXPORT int tw_tick() {
  g_game.tick(g_input);
  g_nEvents = 0;
  Event e;
  while (g_game.pollEvent(e)) {
    g_sound.onEvent(e);
    if (g_nEvents < 64) g_events[g_nEvents++] = e;
  }
  g_sound.update(g_game.audio());
  return g_nEvents;
}
EXPORT int tw_event_type(int i) { return i < g_nEvents ? g_events[i].type : 0; }
EXPORT int tw_event_kind(int i) { return i < g_nEvents ? g_events[i].kind : 0; }
EXPORT int tw_event_pos(int i) { return i < g_nEvents ? g_events[i].pos : 0; }
EXPORT int tw_event_value(int i) { return i < g_nEvents ? g_events[i].value : 0; }

EXPORT unsigned char* tw_render(int n) {
  g_game.render(g_fb, n < 1 ? 1 : (n > 1000 ? 1000 : n));
  return (unsigned char*)g_fb;
}

EXPORT float* tw_audio(int n) {
  if (n > 4096) n = 4096;
  g_synth.render(g_pcm, n);
  for (int i = 0; i < n; i++) g_pcmF[i] = g_pcm[i] * (1.0f / 32768.0f);
  return g_pcmF;
}

EXPORT int tw_stage() { return g_game.stage(); }
EXPORT int tw_mode() { return g_game.mode(); }
EXPORT int tw_level() { return g_game.levelIndex(); }
EXPORT int tw_lives() { return g_game.lives(); }
EXPORT int tw_score() { return g_game.score(); }
EXPORT int tw_combo() { return g_game.combo(); }
EXPORT int tw_player_pos() { return g_game.playerPos(); }
EXPORT int tw_level_time() { return (int)g_game.levelTime(); }
EXPORT int tw_boss_hp() { return g_game.bossHp(); }
EXPORT int tw_enemies() { return g_game.enemiesAlive(); }
EXPORT int tw_paused() { return g_game.paused() ? 1 : 0; }
EXPORT int tw_level_count() { return g_campaign.count; }
EXPORT const char* tw_level_name() { return g_game.level().name; }
EXPORT int tw_music_track() { return g_synth.currentTrack(); }
EXPORT float tw_music_intensity() { return g_synth.currentIntensity(); }
EXPORT int tw_voices() { return g_synth.activeVoices(); }

EXPORT void tw_set_option(int id, int v) {
  GameOptions& o = g_game.opt;
  switch (id) {
    case 0: o.livesPerLevel = (uint8_t)clampi(v, 1, 9); break;
    case 1: o.livesPerRun = v != 0; break;
    case 2: o.idleTimeoutSec = (uint16_t)clampi(v, 5, 3600); break;
    case 3: o.juice = v != 0; break;
    case 4: o.smooth = v != 0; break;
    case 5: o.palette = (uint8_t)clampi(v, 0, 1); break;
    case 6: o.difficulty = (uint8_t)clampi(v, 0, 2); break;
    case 7: o.gravity = v != 0; break;
    case 8: o.telegraph = v != 0; break;
    case 9: o.attract = v != 0; break;
    case 10: g_sound.moveSound = v != 0; break;
    case 11: g_sound.musicOn = v != 0; break;
    case 12: g_game.highScore = v; break;
    case 13: g_game.setDefaultMode((Mode)clampi(v, 0, MODE_COUNT - 1)); break;
    case 14: g_game.setSpectate(v != 0); break;
    case 15: g_sound.attractAudio = v != 0; break;
    case 16: o.conveyorBrightness = (uint8_t)clampi(v, 1, 255); break;
    case 17: o.lavaOffBrightness = (uint8_t)clampi(v, 1, 255); break;
    default: break;
  }
}

EXPORT void tw_set_volumes(float master, float music, float sfx) { g_synth.volumes(master, music, sfx); }
EXPORT void tw_test_sound(int id) { g_sound.testSound(id); }

// Re-initialise only the synth (the browser learns its real sample rate after a user gesture)
EXPORT void tw_audio_init(int sampleRate) {
  g_synth.init(sampleRate);
  g_synth.volumes(0.8f, 0.6f, 0.9f);
}
EXPORT void tw_music(int track, float intensity) {
  if (track < 0) { g_synth.musicStop(); return; }
  g_synth.music((uint8_t)track);
  g_synth.intensity(intensity);
}
EXPORT void tw_stop_sound() { g_synth.stopAll(); }
