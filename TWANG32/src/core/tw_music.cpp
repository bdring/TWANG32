/*
  TWANG Next - adaptive music.

  Every track is a loop of layered patterns over a chord progression in a dark
  mode. Layers have an intensity threshold: the drone is always there, the
  heartbeat comes in when something is near, drums and the lead arrive as the
  danger rises, and a dissonant cluster shimmers on top when you are about to
  die. Intensity is driven by the game (enemy proximity, lava, bosses, the
  level's own "tension" value).

  Note events: { step, degree, length (16ths), velocity }.
  Layer modes: 0 = degree relative to the current chord root
               1 = absolute scale degree (melodies)
               2 = fixed MIDI note + semitone offset (drums, effects)
*/
#include "tw_synth.h"
#include "tw_patches.h"

namespace tw {

namespace {

struct N { uint8_t step; int8_t deg; uint8_t len; uint8_t vel; };
struct Layer {
  uint8_t patch;
  int8_t octave;
  uint8_t threshold;  // intensity (0-100) where the layer starts to fade in
  uint8_t prob;       // chance (%) each note plays - keeps loops from feeling mechanical
  uint8_t steps;      // loop length in 16ths
  int8_t harmony;     // extra scale degree stacked on every note (0 = none)
  uint8_t mode;
  int8_t fixed;
  uint8_t n;
  const N* notes;
};
struct Track {
  float bpm;
  uint8_t root;
  const int8_t* scale;
  uint8_t bars;
  const int8_t* prog;
  float echoBeats, echoFb;
  uint8_t nLayers;
  const Layer* layers;
};

const int8_t PHRYGIAN[7] = {0, 1, 3, 5, 7, 8, 10};
const int8_t HARM_MINOR[7] = {0, 2, 3, 5, 7, 8, 11};
const int8_t PHRYG_DOM[7] = {0, 1, 4, 5, 7, 8, 10};
const int8_t LOCRIAN[7] = {0, 1, 3, 5, 6, 8, 10};

#define CNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

// ======================================================= DESCENT (D Phrygian, 92 bpm)
const int8_t DESC_PROG[4] = {0, 0, 1, -1};  // D  D  Eb  C
const N DESC_DRONE[] = {{0, 0, 16, 85}};
const N DESC_HEART[] = {{0, 0, 2, 115}, {3, 0, 2, 70}, {8, 0, 2, 105}, {11, 0, 2, 62}};
const N DESC_BELLS[] = {{0, 0, 4, 90}, {3, 2, 3, 60}, {6, 4, 3, 70}, {10, 1, 4, 60}, {13, 2, 3, 52},
                        {16, 4, 4, 80}, {19, 3, 3, 55}, {22, 1, 3, 62}, {26, 0, 4, 58}, {29, -1, 3, 48}};
const N DESC_HATS[] = {{2, 0, 1, 55}, {6, 0, 1, 45}, {7, 0, 1, 25}, {10, 0, 1, 55}, {14, 0, 1, 45}, {15, 0, 1, 22}};
const N DESC_KICK[] = {{0, 0, 1, 110}, {8, 0, 1, 100}, {11, 0, 1, 60}};
const N DESC_SNARE[] = {{4, 0, 1, 90}, {12, 0, 1, 95}};
const N DESC_LEAD[] = {{0, 4, 6, 90}, {6, 3, 2, 70}, {8, 1, 8, 85}, {16, 0, 10, 80}, {28, -1, 3, 60},
                       {32, 1, 6, 85}, {38, 3, 2, 70}, {40, 2, 8, 80}, {48, 1, 6, 75}, {54, 0, 10, 85}};
const N DESC_TENSION[] = {{0, 0, 16, 70}};
const N DESC_BASS[] = {{0, 0, 1, 100}, {2, 0, 1, 60}, {4, 0, 1, 80}, {6, 0, 1, 60},
                       {8, 0, 1, 95}, {10, 0, 1, 60}, {12, 7, 1, 70}, {14, 0, 1, 60}};
const Layer DESC_LAYERS[] = {
    {P_DRONE, -1, 0, 100, 16, 4, 0, 0, CNT(DESC_DRONE), DESC_DRONE},
    {P_HEART, -1, 12, 100, 16, 0, 0, 0, CNT(DESC_HEART), DESC_HEART},
    {P_BELL, 1, 26, 80, 32, 0, 0, 0, CNT(DESC_BELLS), DESC_BELLS},
    {P_BASS, -1, 36, 100, 16, 0, 0, 0, CNT(DESC_BASS), DESC_BASS},
    {P_HAT, 0, 42, 92, 16, 0, 2, 0, CNT(DESC_HATS), DESC_HATS},
    {P_LEAD, 1, 48, 100, 64, 0, 1, 0, CNT(DESC_LEAD), DESC_LEAD},
    {P_KICK, 0, 55, 100, 16, 0, 2, 33, CNT(DESC_KICK), DESC_KICK},
    {P_SNARE, 0, 62, 100, 16, 0, 2, 0, CNT(DESC_SNARE), DESC_SNARE},
    {P_TENSION, 1, 75, 100, 16, 1, 0, 0, CNT(DESC_TENSION), DESC_TENSION},
};

// ======================================================= FURNACE (C harmonic minor, 104 bpm)
const int8_t FURN_PROG[4] = {0, 5, 3, 4};  // Cm  Ab  Fm  G
const N FURN_DRONE[] = {{0, 0, 16, 80}};
const N FURN_BASS[] = {{0, 0, 1, 110}, {2, 0, 1, 70}, {3, 0, 1, 60}, {4, 7, 1, 90}, {6, 0, 1, 70},
                       {8, 0, 1, 105}, {10, 0, 1, 70}, {11, 0, 1, 55}, {12, 7, 1, 85}, {14, 4, 1, 75}};
const N FURN_ANVIL[] = {{4, 0, 1, 80}, {12, 0, 1, 85}, {14, 7, 1, 40}};
const N FURN_ARP[] = {{0, 0, 1, 90}, {1, 2, 1, 60}, {2, 4, 1, 70}, {3, 7, 1, 60}, {4, 4, 1, 65}, {5, 2, 1, 55},
                      {6, 0, 1, 70}, {7, 2, 1, 50}, {8, 4, 1, 80}, {9, 7, 1, 60}, {10, 9, 1, 65}, {11, 7, 1, 55},
                      {12, 4, 1, 70}, {13, 2, 1, 50}, {14, 4, 1, 60}, {15, 7, 1, 55}};
const N FURN_TOMS[] = {{0, 0, 1, 100}, {3, -3, 1, 80}, {6, 0, 1, 85}, {10, -5, 1, 90}, {13, -3, 1, 80}};
const N FURN_HATS[] = {{2, 0, 1, 55}, {3, 0, 1, 30}, {6, 0, 1, 55}, {10, 0, 1, 55}, {11, 0, 1, 30}, {14, 0, 1, 55}};
const N FURN_KICK[] = {{0, 0, 1, 115}, {6, 0, 1, 80}, {8, 0, 1, 110}, {14, 0, 1, 70}};
const N FURN_SNARE[] = {{4, 0, 1, 95}, {12, 0, 1, 100}};
const N FURN_TENSION[] = {{0, 0, 16, 65}};
const N FURN_LEAD[] = {{0, 4, 3, 85}, {3, 3, 1, 60}, {4, 4, 2, 75}, {6, 7, 4, 90}, {12, 6, 4, 70},
                       {16, 4, 3, 80}, {19, 2, 1, 60}, {20, 4, 2, 70}, {22, 0, 6, 85}, {28, -1, 4, 65}};
const Layer FURN_LAYERS[] = {
    {P_DRONE, -1, 0, 100, 16, 4, 0, 0, CNT(FURN_DRONE), FURN_DRONE},
    {P_BASS, -1, 8, 100, 16, 0, 0, 0, CNT(FURN_BASS), FURN_BASS},
    {P_ANVIL, 0, 28, 100, 16, 0, 2, 84, CNT(FURN_ANVIL), FURN_ANVIL},
    {P_PLUCK, 1, 34, 90, 16, 0, 0, 0, CNT(FURN_ARP), FURN_ARP},
    {P_HAT, 0, 44, 95, 16, 0, 2, 0, CNT(FURN_HATS), FURN_HATS},
    {P_TOM, 0, 50, 100, 16, 0, 2, 43, CNT(FURN_TOMS), FURN_TOMS},
    {P_LEAD, 1, 54, 100, 32, 0, 0, 0, CNT(FURN_LEAD), FURN_LEAD},
    {P_KICK, 0, 58, 100, 16, 0, 2, 33, CNT(FURN_KICK), FURN_KICK},
    {P_SNARE, 0, 64, 100, 16, 0, 2, 0, CNT(FURN_SNARE), FURN_SNARE},
    {P_TENSION, 1, 78, 100, 16, 1, 0, 0, CNT(FURN_TENSION), FURN_TENSION},
};

// ======================================================= RECKONING (E Phrygian dominant, 128 bpm) - bosses
const int8_t RECK_PROG[4] = {0, 1, 0, 6};  // E  F  E  D
const N RECK_DRONE[] = {{0, 0, 16, 80}};
const N RECK_BASS[] = {{0, 0, 1, 115}, {2, 0, 1, 75}, {3, 0, 1, 80}, {4, 0, 1, 100}, {6, 0, 1, 75}, {7, 0, 1, 80},
                       {8, 0, 1, 110}, {10, 0, 1, 75}, {11, 0, 1, 80}, {12, 1, 1, 100}, {14, 0, 1, 75}, {15, 0, 1, 80}};
const N RECK_KICK[] = {{0, 0, 1, 110}, {4, 0, 1, 95}, {8, 0, 1, 105}, {12, 0, 1, 95}};
const N RECK_SNARE[] = {{4, 0, 1, 95}, {12, 0, 1, 100}, {15, 0, 1, 45}};
const N RECK_STABS[] = {{0, 0, 2, 100}, {6, 0, 2, 80}, {11, 0, 3, 90}};
const N RECK_HATS[] = {{0, 0, 1, 50}, {2, 0, 1, 40}, {4, 0, 1, 55}, {6, 0, 1, 40}, {8, 0, 1, 50},
                       {10, 0, 1, 40}, {12, 0, 1, 55}, {13, 0, 1, 22}, {14, 0, 1, 40}, {15, 0, 1, 25}};
const N RECK_CHOIR[] = {{0, 0, 16, 75}};
const N RECK_TENSION[] = {{0, 0, 16, 70}};
const N RECK_BELLS[] = {{0, 4, 2, 70}, {3, 3, 2, 55}, {6, 1, 2, 60}, {9, 0, 3, 65}};
const Layer RECK_LAYERS[] = {
    {P_DRONE, -1, 0, 100, 16, 4, 0, 0, CNT(RECK_DRONE), RECK_DRONE},
    {P_BASS, -1, 0, 100, 16, 0, 0, 0, CNT(RECK_BASS), RECK_BASS},
    {P_KICK, 0, 15, 100, 16, 0, 2, 33, CNT(RECK_KICK), RECK_KICK},
    {P_SNARE, 0, 30, 100, 16, 0, 2, 0, CNT(RECK_SNARE), RECK_SNARE},
    {P_LEAD, 1, 38, 100, 16, 2, 0, 0, CNT(RECK_STABS), RECK_STABS},
    {P_HAT, 0, 45, 95, 16, 0, 2, 0, CNT(RECK_HATS), RECK_HATS},
    {P_PAD, 0, 55, 100, 16, 4, 0, 0, CNT(RECK_CHOIR), RECK_CHOIR},
    {P_TENSION, 1, 68, 100, 16, 1, 0, 0, CNT(RECK_TENSION), RECK_TENSION},
    {P_BELL, 2, 75, 70, 16, 0, 0, 0, CNT(RECK_BELLS), RECK_BELLS},
};

// ======================================================= ABYSS (A Locrian, 66 bpm) - darkness and the screensaver
const int8_t ABYSS_PROG[4] = {0, 0, 5, 1};  // A  A  F  Bb
const N ABYSS_PAD[] = {{0, 0, 16, 80}};
const N ABYSS_DRONE[] = {{0, 0, 16, 60}};
const N ABYSS_WIND[] = {{0, 0, 16, 90}};
const N ABYSS_HEART[] = {{0, 0, 2, 105}, {3, 0, 2, 60}};
const N ABYSS_BELLS[] = {{0, 0, 4, 80}, {7, 4, 4, 55}, {11, 2, 4, 60}, {20, 1, 4, 62}, {27, 6, 4, 50}};
const N ABYSS_TENSION[] = {{0, 0, 16, 60}};
const N ABYSS_DRUM[] = {{0, 0, 1, 90}, {10, 0, 1, 60}};
const N ABYSS_LEAD[] = {{0, 0, 12, 70}, {12, 1, 4, 60}, {16, 4, 16, 70}, {32, 3, 8, 65}, {40, 1, 8, 60}, {48, 0, 16, 70}};
const Layer ABYSS_LAYERS[] = {
    {P_PAD, 0, 0, 100, 16, 7, 0, 0, CNT(ABYSS_PAD), ABYSS_PAD},
    {P_DRONE, -1, 0, 100, 16, 0, 0, 0, CNT(ABYSS_DRONE), ABYSS_DRONE},
    {P_WIND, 0, 8, 100, 16, 0, 2, 0, CNT(ABYSS_WIND), ABYSS_WIND},
    {P_BELL, 2, 15, 55, 32, 0, 0, 0, CNT(ABYSS_BELLS), ABYSS_BELLS},
    {P_HEART, -1, 22, 100, 16, 0, 0, 0, CNT(ABYSS_HEART), ABYSS_HEART},
    {P_LEAD, 1, 45, 100, 64, 0, 1, 0, CNT(ABYSS_LEAD), ABYSS_LEAD},
    {P_TOM, 0, 55, 100, 16, 0, 2, 36, CNT(ABYSS_DRUM), ABYSS_DRUM},
    {P_TENSION, 1, 65, 100, 16, 1, 0, 0, CNT(ABYSS_TENSION), ABYSS_TENSION},
};

const Track TRACKS[4] = {
    {92, 50, PHRYGIAN, 4, DESC_PROG, 0.75f, 0.42f, CNT(DESC_LAYERS), DESC_LAYERS},
    {104, 48, HARM_MINOR, 4, FURN_PROG, 0.5f, 0.35f, CNT(FURN_LAYERS), FURN_LAYERS},
    {128, 52, PHRYG_DOM, 4, RECK_PROG, 0.75f, 0.30f, CNT(RECK_LAYERS), RECK_LAYERS},
    {66, 45, LOCRIAN, 4, ABYSS_PROG, 0.5f, 0.55f, CNT(ABYSS_LAYERS), ABYSS_LAYERS},
};

// ======================================================= phrases (stingers)
struct PN { uint16_t t; uint8_t patch; uint8_t midi; uint8_t vel; uint16_t len; };
struct PhraseDef { uint8_t n; const PN* notes; };

const PN PH_CLEAR[] = {{0, S_STING, 74, 110, 120}, {0, S_STING_B, 50, 110, 500}, {90, S_STING, 77, 100, 120},
                       {180, S_STING, 81, 105, 120}, {270, S_STING, 86, 120, 420}, {270, S_STING_B, 57, 100, 450},
                       {270, S_GEM, 98, 70, 400}};
const PN PH_OVER[] = {{0, S_STING, 69, 100, 380}, {0, S_STING_B, 45, 110, 800}, {380, S_STING, 65, 95, 380},
                      {760, S_STING, 62, 95, 380}, {760, S_STING_B, 38, 110, 1500}, {1140, S_STING, 61, 100, 1100}};
const PN PH_BOSS[] = {{0, S_STING, 74, 100, 110}, {0, S_STING_B, 50, 115, 600}, {120, S_STING, 74, 90, 110},
                      {240, S_STING, 74, 95, 110}, {360, S_STING, 78, 105, 200}, {360, S_STING_B, 57, 105, 400},
                      {560, S_STING, 81, 110, 200}, {760, S_STING, 86, 120, 700}, {760, S_STING_B, 62, 110, 900},
                      {760, S_GEM, 98, 80, 400}, {900, S_GEM, 102, 70, 400}, {1040, S_GEM, 105, 65, 600}};
const PN PH_HIGH[] = {{0, S_STING, 74, 90, 80}, {60, S_STING, 76, 92, 80}, {120, S_STING, 78, 95, 80},
                      {180, S_STING, 81, 98, 80}, {240, S_STING, 83, 100, 80}, {300, S_STING, 86, 105, 80},
                      {360, S_STING, 88, 110, 80}, {420, S_STING, 90, 120, 500}, {420, S_GEM, 102, 90, 500}};
const PN PH_VICTORY[] = {{0, S_STING, 74, 105, 150}, {0, S_STING_B, 50, 115, 700}, {150, S_STING, 78, 100, 150},
                         {300, S_STING, 81, 105, 150}, {450, S_STING, 86, 115, 300}, {800, S_STING, 79, 100, 150},
                         {800, S_STING_B, 43, 115, 700}, {950, S_STING, 83, 100, 150}, {1100, S_STING, 86, 105, 150},
                         {1250, S_STING, 91, 115, 300}, {1600, S_STING, 81, 100, 150}, {1600, S_STING_B, 45, 115, 700},
                         {1750, S_STING, 85, 100, 150}, {1900, S_STING, 88, 105, 150}, {2050, S_STING, 93, 115, 300},
                         {2400, S_STING, 86, 120, 900}, {2400, S_STING_B, 38, 120, 1400}, {2400, S_GEM, 98, 90, 600},
                         {2550, S_GEM, 102, 80, 600}, {2700, S_GEM, 105, 75, 900}};
const PN PH_CHECK[] = {{0, S_GEM, 81, 90, 300}, {110, S_GEM, 86, 100, 500}};
const PN PH_LIFE[] = {{0, S_STING, 74, 90, 100}, {90, S_STING, 78, 95, 100}, {180, S_STING, 81, 100, 300}};
const PN PH_GEMS[] = {{0, S_GEM, 93, 90, 250}, {70, S_GEM, 98, 80, 400}};

const PhraseDef PHRASES[PH_COUNT] = {
    {CNT(PH_CLEAR), PH_CLEAR}, {CNT(PH_OVER), PH_OVER},   {CNT(PH_BOSS), PH_BOSS}, {CNT(PH_HIGH), PH_HIGH},
    {CNT(PH_VICTORY), PH_VICTORY}, {CNT(PH_CHECK), PH_CHECK}, {CNT(PH_LIFE), PH_LIFE}, {CNT(PH_GEMS), PH_GEMS},
};

inline int degToSemis(int deg, const int8_t* scale) {
  int oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
  int idx = deg - oct * 7;
  return scale[idx] + 12 * oct;
}

}  // namespace

void Synth::setTrack(uint8_t t) {
  if (t >= 4) { musicFadeTarget_ = 0; pendingTrack_ = 255; return; }
  if (t == track_) { musicFadeTarget_ = 1; pendingTrack_ = 255; return; }
  if (track_ != 255 && musicFade_ > 0.05f) {  // fade the old one out first
    pendingTrack_ = t;
    musicFadeTarget_ = 0;
    return;
  }
  const Track& T = TRACKS[t];
  track_ = t;
  pendingTrack_ = 255;
  step_ = 0;
  samplesPerStep_ = (float)sr_ * 60.0f / (T.bpm * 4.0f);
  stepAcc_ = samplesPerStep_;  // first step fires right away
  musicFadeTarget_ = 1;
  musicFade_ = maxf(musicFade_, 0.35f);
  for (int i = 0; i < 10; i++) layerGain_[i] = 0;
  float beat = 60.0f / T.bpm;
  echoDelay_ = clampi((int)(T.echoBeats * beat * (float)sr_), 1, ECHO_LEN - 1);
  echoFb_ = T.echoFb;
}

void Synth::musicStep() {
  if (track_ >= 4) return;
  const Track& T = TRACKS[track_];
  // tempo leans forward slightly as the danger rises
  samplesPerStep_ = (float)sr_ * 60.0f / (T.bpm * (1.0f + 0.06f * intensity_) * 4.0f);
  int bar = (int)(step_ / 16);
  int chordRoot = T.prog[bar % T.bars];
  float stepSec = samplesPerStep_ * invSr_;
  float I = intensity_ * 100.0f;
  for (int li = 0; li < T.nLayers && li < 10; li++) {
    const Layer& L = T.layers[li];
    float target = L.threshold == 0 ? 1.0f : clampf((I - (float)L.threshold) / 12.0f, 0, 1);
    target = target * target * (3.0f - 2.0f * target);
    layerGain_[li] += (target - layerGain_[li]) * 0.25f;
    if (layerGain_[li] < 0.03f) continue;
    int s = (int)(step_ % L.steps);
    for (int k = 0; k < L.n; k++) {
      const N& nn = L.notes[k];
      if (nn.step != s) continue;
      if (L.prob < 100 && (int)musicRng_.below(100) >= L.prob) continue;
      float vel = (nn.vel / 127.0f) * layerGain_[li] * (0.92f + 0.08f * musicRng_.unit());
      float hold = nn.len * stepSec * 0.92f;
      float freq;
      if (L.mode == 2) {
        bool noisy = PATCHES[L.patch].wave == W_NOISE;
        freq = noisy ? 0.0f : midiToHz((float)(L.fixed + nn.deg));
        startVoice(L.patch, freq, vel, hold, 0);
        continue;
      }
      int deg = L.mode == 0 ? chordRoot + nn.deg : nn.deg;
      int midi = T.root + 12 * L.octave + degToSemis(deg, T.scale);
      startVoice(L.patch, midiToHz((float)midi), vel, hold, 0);
      if (L.harmony) {
        int midi2 = T.root + 12 * L.octave + degToSemis(deg + L.harmony, T.scale);
        startVoice(L.patch, midiToHz((float)midi2), vel * 0.8f, hold, 0);
      }
    }
  }
  step_++;
}

void Synth::phraseTick(float ms) {
  for (int k = 0; k < 2; k++) {
    PhrasePlayer& p = ph_[k];
    if (!p.active) continue;
    const PhraseDef& D = PHRASES[p.id];
    p.t += ms;
    while (p.next < D.n && D.notes[p.next].t <= p.t) {
      const PN& n = D.notes[p.next];
      startVoice(n.patch, midiToHz((float)n.midi + p.transpose), (n.vel / 127.0f) * p.vol, n.len / 1000.0f, 0);
      p.next++;
    }
    if (p.next >= D.n) p.active = false;
  }
}

}  // namespace tw
