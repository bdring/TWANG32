#include "tw_synth.h"
#include "tw_patches.h"

namespace tw {

static const int CTRL = 16;  // control-rate block (samples)

static inline float polyblep(float t, float dt) {
  if (dt <= 0) return 0;
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0;
}
static inline float expDecay(float x) { return exp2f_(-x * 1.44269504f); }  // e^-x

void Synth::init(int sampleRate) {
  sr_ = clampi(sampleRate, 8000, TW_SYNTH_MAX_RATE);
  invSr_ = 1.0f / (float)sr_;
  memzero(v_, sizeof(v_));
  memzero(pl_, sizeof(pl_));
  memzero(ph_, sizeof(ph_));
  memzero(echo_, sizeof(echo_));
  memzero(layerGain_, sizeof(layerGain_));
  echoPos_ = 0;
  echoDelay_ = clampi((int)(0.33f * sr_), 1, ECHO_LEN - 1);
  echoFb_ = 0.4f;
  echoLp_ = 0;
  dcX_ = dcY_ = 0;
  track_ = 255;
  pendingTrack_ = 255;
  musicFade_ = musicFadeTarget_ = 0;
  intensity_ = intensityTarget_ = 0;
  stepAcc_ = 0;
  step_ = 0;
  musicRng_.seed(0xBADC0DEu);
  controlCount_ = 0;
  __atomic_store_n(&qHead_, (uint16_t)0, __ATOMIC_RELAXED);
  __atomic_store_n(&qTail_, (uint16_t)0, __ATOMIC_RELAXED);
}

// ------------------------------------------------------------------ queue (SPSC)
void Synth::push(const SynthCmd& c) {
  uint16_t h = __atomic_load_n(&qHead_, __ATOMIC_RELAXED);
  uint16_t next = (uint16_t)((h + 1) % QUEUE);
  if (next == __atomic_load_n(&qTail_, __ATOMIC_ACQUIRE)) { dropped_++; return; }
  q_[h] = c;
  __atomic_store_n(&qHead_, next, __ATOMIC_RELEASE);
}

bool Synth::pop(SynthCmd& c) {
  uint16_t t = __atomic_load_n(&qTail_, __ATOMIC_RELAXED);
  if (t == __atomic_load_n(&qHead_, __ATOMIC_ACQUIRE)) return false;
  c = q_[t];
  __atomic_store_n(&qTail_, (uint16_t)((t + 1) % QUEUE), __ATOMIC_RELEASE);
  return true;
}

static SynthCmd mk(uint8_t op, uint8_t tag = 0, uint8_t id = 0, float a = 0, float b = 0, float c = 0) {
  SynthCmd s;
  s.op = op; s.tag = tag; s.id = id; s.flags = 0; s.a = a; s.b = b; s.c = c;
  return s;
}

void Synth::note(uint8_t patch, float freq, float vol, float hold, uint8_t tag) { push(mk(OP_NOTE, tag, patch, freq, vol, hold)); }
void Synth::release(uint8_t tag) { push(mk(OP_RELEASE, tag)); }
void Synth::set(uint8_t tag, float freq, float vol, float glide) { push(mk(OP_SET, tag, 0, freq, vol, glide)); }
void Synth::pluck(float freq, float vol, float decay) { push(mk(OP_PLUCK, 0, 0, freq, vol, decay)); }
void Synth::music(uint8_t track) { push(mk(OP_MUSIC, 0, track)); }
void Synth::intensity(float x) { push(mk(OP_INTENSITY, 0, 0, x)); }
void Synth::musicStop() { push(mk(OP_MUSIC_STOP)); }
void Synth::phrase(uint8_t id, float vol, float transpose) { push(mk(OP_PHRASE, 0, id, vol, transpose)); }
void Synth::volumes(float m, float mu, float s) { push(mk(OP_VOLUMES, 0, 0, m, mu, s)); }
void Synth::duck(float amount) { push(mk(OP_DUCK, 0, 0, amount)); }
void Synth::stopAll() { push(mk(OP_STOP_ALL)); }

int Synth::activeVoices() const {
  int n = 0;
  for (int i = 0; i < VOICES; i++) n += v_[i].active ? 1 : 0;
  return n;
}

// ------------------------------------------------------------------ voices
int Synth::allocVoice(uint8_t bus) {
  int lo = bus == BUS_MUSIC ? 0 : MUSIC_VOICES;
  int hi = bus == BUS_MUSIC ? MUSIC_VOICES : VOICES;
  for (int i = lo; i < hi; i++) if (!v_[i].active) return i;
  // steal: quietest releasing voice, else the oldest untagged one
  int best = -1;
  float bestEnv = 2.0f;
  for (int i = lo; i < hi; i++)
    if (v_[i].stage == 3 && v_[i].env < bestEnv) { bestEnv = v_[i].env; best = i; }
  if (best >= 0) return best;
  uint32_t oldest = 0xFFFFFFFFu;
  for (int i = lo; i < hi; i++)
    if (!v_[i].tag && v_[i].seq < oldest) { oldest = v_[i].seq; best = i; }
  return best >= 0 ? best : lo;
}

void Synth::startVoice(uint8_t patch, float freq, float vol, float hold, uint8_t tag) {
  if (patch >= PATCH_COUNT) return;
  const Patch* p = &PATCHES[patch];
  int idx = -1;
  if (tag) for (int i = 0; i < VOICES; i++) if (v_[i].active && v_[i].tag == tag) { idx = i; break; }
  if (idx < 0) idx = allocVoice(p->bus);
  Voice& v = v_[idx];
  bool retrigger = v.active && v.tag == tag && tag != 0;
  v.p = p;
  v.patchId = patch;
  v.active = 1;
  v.tag = tag;
  v.stage = 0;
  v.baseFreq = v.freq = v.targetFreq = freq;
  v.vol = v.targetVol = vol;
  v.glide = 0;
  if (!retrigger) {
    v.env = 0;
    v.phase = (noise() * 0.5f + 0.5f);
    v.phase2 = (noise() * 0.5f + 0.5f);
    v.phase3 = 0;
    v.low = v.band = 0;
    v.vibPhase = 0;
    v.sh = 0;
    v.shPhase = 0;
  }
  float sr = (float)sr_;
  v.attackInc = 1.0f / (maxf(p->attack, 0.0005f) * sr);
  v.decayCoef = expDecay(4.6f / (maxf(p->decay, 0.001f) * sr));
  v.releaseCoef = expDecay(4.6f / (maxf(p->release, 0.001f) * sr));
  v.hold = hold;
  v.age = 0;
  v.seq = ++seq_;
  v.inc = freq * invSr_;
  v.fcoef = 0.5f;
  v.q = 1.0f;
  control(v, 0);
}

void Synth::control(Voice& v, float dt) {
  const Patch& p = *v.p;
  v.age += dt;
  if (v.stage < 3 && v.hold >= 0) {
    v.hold -= dt;
    if (v.hold <= 0) v.stage = 3;
  }
  if (v.glide > 0.0001f && dt > 0) {
    float k = clampf(dt / v.glide, 0, 1);
    v.baseFreq += (v.targetFreq - v.baseFreq) * k;
    v.vol += (v.targetVol - v.vol) * k;
  } else {
    v.baseFreq = v.targetFreq;
    v.vol = v.targetVol;
  }
  float pitch = 0;
  if (p.pitchFrom != 0 && p.pitchTime > 0) pitch += p.pitchFrom * expDecay(v.age / p.pitchTime);
  float vib = 0;
  if (p.vibDepth > 0) {
    float depth = p.vibDepth;
    if (p.vibDelay > 0) depth *= clampf(v.age / p.vibDelay, 0, 1);
    else if (p.vibDelay < 0) depth *= expDecay(v.age / -p.vibDelay);
    v.vibPhase += p.vibRate * dt;
    if (v.vibPhase > 1000.0f) v.vibPhase -= 1000.0f;
    vib = depth * sin2pi(v.vibPhase);
  }
  float cutMod = 1.0f;
  if (p.wave == W_NOISE) cutMod = exp2f_(vib);  // for noise, vibrato sweeps the filter
  else pitch += vib;
  v.freq = v.baseFreq * exp2f_(pitch * (1.0f / 12.0f));
  v.inc = clampf(v.freq * invSr_, 0, 0.49f);
  if (p.filter != F_OFF) {
    float env = 1.0f;
    if (p.cutoffEnv != 1.0f && p.cutoffTime > 0) env = 1.0f + (p.cutoffEnv - 1.0f) * expDecay(v.age / p.cutoffTime);
    float fc = p.cutoff * env * cutMod * (p.bus == BUS_MUSIC ? bright_ : 1.0f);
    fc = clampf(fc, 30.0f, 0.42f * (float)sr_);
    v.fcoef = clampf(2.0f * sinf_(PI_F * fc / (2.0f * (float)sr_)), 0.0f, 1.0f);
    v.q = clampf(2.0f * (1.0f - p.res * 0.96f), 0.08f, 2.0f);
  }
}

// ------------------------------------------------------------------ commands
void Synth::apply(const SynthCmd& c) {
  switch (c.op) {
    case OP_NOTE: startVoice(c.id, c.a, c.b, c.c, c.tag); break;
    case OP_RELEASE:
      for (int i = 0; i < VOICES; i++) if (v_[i].active && v_[i].tag == c.tag) v_[i].stage = 3;
      break;
    case OP_SET:
      for (int i = 0; i < VOICES; i++) {
        Voice& v = v_[i];
        if (!v.active || v.tag != c.tag || v.stage == 3) continue;
        if (c.a > 0) v.targetFreq = c.a;
        v.targetVol = c.b;
        v.glide = c.c;
        if (c.b <= 0) v.stage = 3;
      }
      break;
    case OP_PLUCK: {
      int k = pl_[0].active ? (pl_[1].active ? (pl_[0].energy < pl_[1].energy ? 0 : 1) : 1) : 0;
      Pluck& p = pl_[k];
      p.len = clampi((int)((float)sr_ / maxf(c.a, 30.0f)), 8, PLUCK_LEN);
      // excite with filtered noise: brighter = more metallic twang
      float lp = 0;
      for (int i = 0; i < p.len; i++) {
        lp += (noise() - lp) * 0.6f;
        p.buf[i] = lp;
      }
      p.idx = 0;
      p.vol = c.b;
      p.decay = clampf(c.c, 0.85f, 0.9995f);
      p.energy = 1.0f;
      p.active = true;
      break;
    }
    case OP_MUSIC: setTrack(c.id); break;
    case OP_INTENSITY: intensityTarget_ = clampf(c.a, 0, 1); break;
    case OP_MUSIC_STOP: musicFadeTarget_ = 0; pendingTrack_ = 255; break;
    case OP_PHRASE: {
      int k = ph_[0].active ? 1 : 0;
      if (ph_[0].active && ph_[1].active) k = 0;
      ph_[k].id = c.id;
      ph_[k].active = c.id < PH_COUNT;
      ph_[k].t = 0;
      ph_[k].next = 0;
      ph_[k].vol = c.a;
      ph_[k].transpose = c.b;
      break;
    }
    case OP_VOLUMES:
      master_ = clampf(c.a, 0, 1);
      musicVol_ = clampf(c.b, 0, 1);
      sfxVol_ = clampf(c.c, 0, 1);
      break;
    case OP_DUCK: duck_ = maxf(duck_, clampf(c.a, 0, 1)); break;
    case OP_STOP_ALL:
      for (int i = 0; i < VOICES; i++) v_[i].active = 0;
      for (int i = 0; i < PLUCKS; i++) pl_[i].active = false;
      ph_[0].active = ph_[1].active = false;
      musicFade_ = musicFadeTarget_ = 0;
      track_ = 255;
      break;
    case OP_BRIGHT: bright_ = clampf(c.a, 0.2f, 3.0f); break;
    default: break;
  }
}

// ------------------------------------------------------------------ render
void Synth::render(int16_t* out, int n) {
  SynthCmd c;
  int guard = 0;
  while (guard++ < QUEUE && pop(c)) apply(c);

  const float dtCtrl = (float)CTRL * invSr_;
  for (int i = 0; i < n; i++) {
    if (controlCount_ <= 0) {
      controlCount_ = CTRL;
      for (int k = 0; k < VOICES; k++) if (v_[k].active) control(v_[k], dtCtrl);
      // music fade, ducking, intensity smoothing
      float fadeStep = dtCtrl / 1.6f;
      if (musicFade_ < musicFadeTarget_) musicFade_ = minf(musicFadeTarget_, musicFade_ + fadeStep);
      else if (musicFade_ > musicFadeTarget_) musicFade_ = maxf(musicFadeTarget_, musicFade_ - fadeStep);
      if (musicFade_ <= 0.0001f && musicFadeTarget_ <= 0 && track_ != 255) {
        track_ = 255;
        for (int k = 0; k < MUSIC_VOICES; k++) v_[k].stage = 3;
      }
      if (track_ == 255 && pendingTrack_ != 255 && musicFade_ <= 0.0001f) setTrack(pendingTrack_);
      duck_ *= expDecay(dtCtrl / 0.45f);
      intensity_ += (intensityTarget_ - intensity_) * clampf(dtCtrl / 1.2f, 0, 1);
      bright_ = 0.55f + intensity_ * 0.9f;
      phraseTick(dtCtrl * 1000.0f);
    }
    controlCount_--;

    if (track_ != 255) {
      stepAcc_ += 1.0f;
      if (stepAcc_ >= samplesPerStep_) { stepAcc_ -= samplesPerStep_; musicStep(); }
    }

    float mus = 0, sfx = 0, musSend = 0, sfxSend = 0;
    for (int k = 0; k < VOICES; k++) {
      Voice& v = v_[k];
      if (!v.active) continue;
      const Patch& p = *v.p;
      float s;
      float inc = v.inc;
      switch (p.wave) {
        case W_SINE: s = sin2pi(v.phase); break;
        case W_TRI: s = 4.0f * absf(v.phase - 0.5f) - 1.0f; break;
        case W_SAW: s = 2.0f * v.phase - 1.0f - polyblep(v.phase, inc); break;
        case W_SQUARE:
        case W_PULSE: {
          float pw = p.wave == W_SQUARE ? 0.5f : p.pw;
          float ph2 = v.phase + 1.0f - pw;
          if (ph2 >= 1.0f) ph2 -= 1.0f;
          s = (v.phase < pw ? 1.0f : -1.0f) + polyblep(v.phase, inc) - polyblep(ph2, inc);
          break;
        }
        case W_NOISE:
          if (v.freq > 1.0f && inc < 0.45f) {
            v.shPhase += inc;
            if (v.shPhase >= 1.0f) { v.shPhase -= 1.0f; v.sh = noise(); }
            s = v.sh;
          } else {
            s = noise();
          }
          break;
        case W_SAW2: {
          float inc2 = inc * exp2f_(p.detune * (1.0f / 1200.0f));
          float s1 = 2.0f * v.phase - 1.0f - polyblep(v.phase, inc);
          float s2 = 2.0f * v.phase2 - 1.0f - polyblep(v.phase2, inc2);
          v.phase2 += inc2;
          if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
          s = 0.5f * (s1 + s2);
          break;
        }
        case W_BELL: {
          float a2 = 0.45f * expDecay(v.age * 3.0f);
          float a3 = 0.22f * expDecay(v.age * 7.0f);
          s = 0.72f * sin2pi(v.phase) + a2 * sin2pi(v.phase2) + a3 * sin2pi(v.phase3);
          v.phase2 += inc * 2.756f;
          if (v.phase2 >= 1.0f) v.phase2 -= floorf_(v.phase2);
          v.phase3 += inc * 5.404f;
          if (v.phase3 >= 1.0f) v.phase3 -= floorf_(v.phase3);
          break;
        }
        default: s = 0; break;
      }
      if (p.noise > 0) s += noise() * p.noise;
      v.phase += inc;
      if (v.phase >= 1.0f) v.phase -= 1.0f;

      if (p.filter != F_OFF) {
        float f = v.fcoef, q = v.q, hp = 0;
        for (int os = 0; os < 2; os++) {  // 2x oversampled state-variable filter
          v.low += f * v.band;
          hp = s - v.low - q * v.band;
          v.band += f * hp;
        }
        s = p.filter == F_LP ? v.low : (p.filter == F_HP ? hp : v.band);
        // keep runaway resonance in check
        if (v.low > 8.0f || v.low < -8.0f || v.band > 8.0f || v.band < -8.0f) { v.low *= 0.5f; v.band *= 0.5f; }
      }

      switch (v.stage) {
        case 0:
          v.env += v.attackInc;
          if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; }
          break;
        case 1:
          v.env = p.sustain + (v.env - p.sustain) * v.decayCoef;
          if (p.sustain <= 0.0001f) { if (v.env < 0.0004f) v.active = 0; }
          else if (v.env - p.sustain < 0.0005f) v.stage = 2;
          break;
        case 2: break;
        default:
          v.env *= v.releaseCoef;
          if (v.env < 0.0004f) v.active = 0;
          break;
      }
      float o = s * v.env * v.vol * p.gain;
      if (p.bus == BUS_MUSIC) { mus += o; musSend += o * p.echo; }
      else { sfx += o; sfxSend += o * p.echo; }
    }

    for (int k = 0; k < PLUCKS; k++) {
      Pluck& p = pl_[k];
      if (!p.active) continue;
      int nx = p.idx + 1 == p.len ? 0 : p.idx + 1;
      float y = p.buf[p.idx];
      p.buf[p.idx] = (y + p.buf[nx]) * 0.5f * p.decay;
      p.idx = nx;
      float o = y * p.vol;
      sfx += o;
      sfxSend += o * 0.25f;
      p.energy = p.energy * 0.9995f + absf(y) * 0.0005f;
      if (p.energy < 0.0006f) p.active = false;
    }

    float musGain = musicVol_ * musicFade_ * (1.0f - 0.6f * duck_);
    mus *= musGain;
    musSend *= musGain;
    sfx *= sfxVol_;
    sfxSend *= sfxVol_;

    // echo (dark, filtered repeats)
    int rp = echoPos_ - echoDelay_;
    if (rp < 0) rp += ECHO_LEN;
    float rd = echo_[rp] * (1.0f / 32768.0f);
    echoLp_ += (rd - echoLp_) * 0.35f;
    float ein = musSend + sfxSend + echoLp_ * echoFb_;
    ein = clampf(ein, -1.0f, 1.0f);
    echo_[echoPos_] = (int16_t)(ein * 32000.0f);
    if (++echoPos_ >= ECHO_LEN) echoPos_ = 0;

    float x = (mus + sfx + echoLp_ * 0.7f) * master_;
    // DC blocker then soft clip
    float y = x - dcX_ + 0.995f * dcY_;
    dcX_ = x;
    dcY_ = y;
    float outv = softclip(y * 1.25f);
    out[i] = (int16_t)(outv * 31000.0f);
  }
}

}  // namespace tw
