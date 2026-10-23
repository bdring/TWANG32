/*
  TWANG Next - audio engine.

  A small polyphonic synthesizer that renders 16-bit mono at 22.05 kHz:
    - 20 voices (12 music, 8 effects) with sine/tri/saw/square/pulse/noise/bell
      oscillators, ADSR, pitch envelopes, vibrato and a resonant filter
    - 2 Karplus-Strong plucked-string voices for the twang itself
    - an adaptive music sequencer whose layers fade in with danger
    - short phrase "stingers" for level clear, game over, boss kills
    - tempo-synced echo, music ducking, DC blocking and soft clipping

  Threading: the game thread only calls the post-style methods (note, set,
  music...). They go through a lock-free single-producer/single-consumer
  queue. The audio thread calls render(). In the browser both run on one
  thread, which is fine too.
*/
#pragma once
#include "tw_base.h"

#ifndef TW_SYNTH_MAX_RATE
#define TW_SYNTH_MAX_RATE 24000
#endif

namespace tw {

enum Wave : uint8_t { W_SINE, W_TRI, W_SAW, W_SQUARE, W_PULSE, W_NOISE, W_SAW2, W_BELL };
enum FilterMode : uint8_t { F_OFF, F_LP, F_HP, F_BP };
enum Bus : uint8_t { BUS_SFX = 0, BUS_MUSIC = 1 };

struct Patch {
  uint8_t wave, filter, bus;
  float pw;                          // pulse width (W_PULSE)
  float attack, decay, sustain, release;
  float pitchFrom, pitchTime;        // semitone offset at note-on, gliding to 0 (time constant, s)
  float vibDepth, vibRate, vibDelay; // semitones, Hz; delay > 0 fades in, < 0 decays
  float cutoff, cutoffEnv, cutoffTime, res;
  float detune;                      // cents (W_SAW2)
  float gain;
  float echo;                        // echo send
  float noise;                       // noise blended into tonal waves
};

// Patch ids (see tw_patches.h)
enum PatchId : uint8_t {
  P_DRONE, P_PAD, P_HEART, P_BASS, P_BELL, P_PLUCK, P_HAT, P_KICK, P_SNARE, P_TOM, P_TENSION, P_LEAD, P_ANVIL, P_WIND,
  S_BOING, S_BLIP, S_CRUNCH, S_CLANK, S_DIE, S_THUD, S_HUM, S_SIZZLE, S_SPAWN, S_GEM, S_IGNITE, S_TICK, S_ROAR,
  S_BOSSHIT, S_RAGE, S_STARTUP, S_STING, S_STING_B, S_UI, S_LAVAKILL, S_SPLIT, S_THRUM,
  PATCH_COUNT
};

enum PhraseId : uint8_t {
  PH_LEVEL_CLEAR, PH_GAME_OVER, PH_BOSS_KILLED, PH_NEW_HIGH, PH_VICTORY, PH_CHECKPOINT, PH_LIFE, PH_GEM, PH_COUNT
};

enum SynthOp : uint8_t {
  OP_NOTE = 1, OP_RELEASE, OP_SET, OP_PLUCK, OP_MUSIC, OP_INTENSITY, OP_MUSIC_STOP, OP_PHRASE, OP_VOLUMES, OP_DUCK,
  OP_STOP_ALL, OP_BRIGHT
};

struct SynthCmd {
  uint8_t op, tag, id, flags;
  float a, b, c;
};

class Synth {
 public:
  static const int VOICES = 20;
  static const int MUSIC_VOICES = 12;  // voices [0, 12) belong to music, the rest to effects
  static const int PLUCKS = 2;
  static const int PLUCK_LEN = 512;
  static const int QUEUE = 256;
  static const int ECHO_LEN = TW_SYNTH_MAX_RATE / 2;  // up to 0.5 s of echo

  void init(int sampleRate);
  int sampleRate() const { return sr_; }

  // audio thread
  void render(int16_t* out, int n);

  // game thread (all thread safe)
  void note(uint8_t patch, float freq, float vol, float hold, uint8_t tag = 0);
  void noteMidi(uint8_t patch, float midi, float vol, float hold, uint8_t tag = 0) { note(patch, midiToHz(midi), vol, hold, tag); }
  void release(uint8_t tag);
  void set(uint8_t tag, float freq, float vol, float glideSec);
  void pluck(float freq, float vol, float decay);
  void music(uint8_t track);
  void intensity(float x);
  void musicStop();
  void phrase(uint8_t id, float vol, float transpose = 0);
  void volumes(float master, float music, float sfx);
  void duck(float amount);
  void stopAll();

  // diagnostics (read from any thread, approximate)
  int activeVoices() const;
  uint8_t currentTrack() const { return track_; }
  float currentIntensity() const { return intensity_; }
  uint32_t droppedCommands() const { return dropped_; }

 private:
  struct Voice {
    const Patch* p;
    uint8_t patchId, active, tag, stage;  // stage 0 attack 1 decay 2 sustain 3 release
    float baseFreq, freq, targetFreq, glide;
    float vol, targetVol;
    float phase, phase2, phase3, inc;
    float env, attackInc, decayCoef, releaseCoef;
    float hold;  // seconds until auto-release (< 0 = until released)
    float age;
    float low, band;   // filter state
    float fcoef, q;
    float vibPhase;
    float sh;          // noise sample & hold value
    float shPhase;
    uint32_t seq;      // allocation order for voice stealing
  };
  struct Pluck {
    float buf[PLUCK_LEN];
    int len, idx;
    float decay, vol;
    bool active;
    float energy;
  };
  struct PhrasePlayer {
    uint8_t id;
    bool active;
    float t;  // ms
    int next;
    float vol, transpose;
  };

  bool pop(SynthCmd& c);
  void push(const SynthCmd& c);
  void apply(const SynthCmd& c);
  void startVoice(uint8_t patch, float freq, float vol, float hold, uint8_t tag);
  int allocVoice(uint8_t bus);
  void control(Voice& v, float dt);
  float noise() { noise_ ^= noise_ << 13; noise_ ^= noise_ >> 17; noise_ ^= noise_ << 5; return (int32_t)noise_ * (1.0f / 2147483648.0f); }

  // music (tw_music.cpp)
  void musicStep();
  void phraseTick(float ms);
  void setTrack(uint8_t t);

  int sr_ = 22050;
  float invSr_ = 1.0f / 22050;
  Voice v_[VOICES];
  Pluck pl_[PLUCKS];
  PhrasePlayer ph_[2];
  uint32_t seq_ = 0;
  uint32_t noise_ = 0x12345678u;

  // queue
  SynthCmd q_[QUEUE];
  uint16_t qHead_ = 0, qTail_ = 0;  // accessed only through __atomic builtins
  uint32_t dropped_ = 0;

  // mix
  float master_ = 0.8f, musicVol_ = 0.6f, sfxVol_ = 0.9f;
  float duck_ = 0;
  float musicFade_ = 0, musicFadeTarget_ = 0;
  float bright_ = 1.0f;
  int16_t echo_[ECHO_LEN];
  int echoPos_ = 0, echoDelay_ = 1000;
  float echoFb_ = 0.4f, echoLp_ = 0;
  float dcX_ = 0, dcY_ = 0;

  // sequencer
  uint8_t track_ = 255;
  uint8_t pendingTrack_ = 255;
  float intensity_ = 0, intensityTarget_ = 0;
  float samplesPerStep_ = 3000, stepAcc_ = 0;
  uint32_t step_ = 0;
  float layerGain_[10];
  Rng musicRng_;
  int controlCount_ = 0;
};

// Maps game events and state to sound. Lives on the game thread.
struct Event;
struct AudioState;

class SoundDirector {
 public:
  bool moveSound = true;      // the original's movement buzz (now a soft thrum)
  bool musicOn = true;
  bool attractAudio = false;  // play ambience during the screensaver

  void attach(Synth* s) { s_ = s; }
  void onEvent(const Event& e);
  void update(const AudioState& a);  // once per game tick
  void testSound(int id);            // UI "test" buttons

 private:
  Synth* s_ = nullptr;
  bool humOn_ = false, lavaOn_ = false;
  uint8_t lastTrack_ = 255;
  uint8_t lastStage_ = 255;
  float smoothedIntensity_ = 0;
  int tick_ = 0;
  bool pendingHigh_ = false;
  uint32_t rnd_ = 0xC0FFEEu;
  float jitter();
};

}  // namespace tw
