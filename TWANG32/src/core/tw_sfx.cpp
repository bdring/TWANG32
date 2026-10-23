/*
  TWANG Next - sound director.
  Turns game events and continuous game state into synth commands.
*/
#include "tw_game.h"
#include "tw_synth.h"

namespace tw {

enum : uint8_t { TAG_HUM = 1, TAG_LAVA = 2 };

float SoundDirector::jitter() {
  rnd_ ^= rnd_ << 13;
  rnd_ ^= rnd_ >> 17;
  rnd_ ^= rnd_ << 5;
  return ((rnd_ >> 8) & 0xFFFF) / 32767.5f - 1.0f;
}

void SoundDirector::onEvent(const Event& e) {
  if (!s_) return;
  Synth& s = *s_;
  switch (e.type) {
    case EV_ATTACK: {
      // the twang: a springy boing over a plucked metal string, never quite the same twice
      float semi = jitter() * 0.8f;
      s.noteMidi(S_BOING, 55.0f + semi, 1.0f, 0.3f);
      s.pluck(midiToHz(45.0f + semi), 0.42f, 0.95f);
      break;
    }
    case EV_KILL:
      if (e.value > 0) {
        int c = clampi(e.value, 1, 8);
        s.noteMidi(S_BLIP, 76.0f + 2.0f * (c - 1), 1.0f, 0.08f);  // each combo step climbs a whole tone
        s.note(S_CRUNCH, 0, 0.9f, 0.05f);
        if (c >= 3) s.noteMidi(S_GEM, 86.0f + 2.0f * c, 0.35f, 0.2f);
      } else {
        s.note(S_LAVAKILL, 0, 0.8f, 0.1f);
      }
      break;
    case EV_ARMOR_HIT:
      s.noteMidi(S_CLANK, 84, 1.0f, 0.1f);
      s.note(S_CRUNCH, 0, 0.5f, 0.03f);
      break;
    case EV_SPLIT:
      s.noteMidi(S_SPLIT, 79, 0.9f, 0.05f);
      s.noteMidi(S_SPLIT, 72, 0.7f, 0.05f);
      break;
    case EV_SPAWN:
      s.noteMidi(S_SPAWN, 50, 0.7f, 0.05f);
      break;
    case EV_DIE:
      s.note(S_DIE, 0, 1.0f, 0.5f);
      s.noteMidi(S_THUD, 40, 1.0f, 0.3f);
      s.duck(1.0f);
      break;
    case EV_GAME_OVER:
      if (e.kind == 0) {  // kind 1 = victory, handled by EV_CAMPAIGN_DONE
        s.musicStop();
        s.phrase(PH_GAME_OVER, 1.0f);
      }
      lastTrack_ = 255;
      break;
    case EV_NEW_HIGHSCORE:
      pendingHigh_ = true;
      break;
    case EV_CAMPAIGN_DONE:
      s.musicStop();
      s.phrase(PH_VICTORY, 1.0f);
      lastTrack_ = 255;
      break;
    case EV_LEVEL_CLEAR:
      s.phrase(PH_LEVEL_CLEAR, 1.0f);
      s.duck(0.8f);
      break;
    case EV_BOSS_HIT:
      s.note(S_BOSSHIT, 0, 1.0f, 0.2f);
      s.noteMidi(S_ROAR, 33.0f + e.value * 2.0f, 1.0f, 0.4f);
      s.duck(0.9f);
      break;
    case EV_BOSS_KILLED:
      s.musicStop();
      lastTrack_ = 255;
      s.note(S_DIE, 0, 1.0f, 0.6f);
      s.noteMidi(S_THUD, 36, 1.0f, 0.5f);
      s.phrase(PH_BOSS_KILLED, 1.0f);
      break;
    case EV_BOSS_RAGE_WARN:
      s.noteMidi(S_RAGE, 45, 0.9f, 0.7f);
      break;
    case EV_LAVA_WARN:
      s.noteMidi(S_TICK, 88, 0.6f, 0.02f);
      break;
    case EV_LAVA_IGNITE:
      s.note(S_IGNITE, 0, e.kind ? 1.0f : 0.7f, 0.2f);
      break;
    case EV_PICKUP_GEM: s.phrase(PH_GEM, 1.0f); break;
    case EV_PICKUP_LIFE: s.phrase(PH_LIFE, 1.0f); break;
    case EV_CHECKPOINT: s.phrase(PH_CHECKPOINT, 1.0f); break;
    case EV_STAGE:
      if (e.kind == ST_BOOT) {
        s.musicStop();
        lastTrack_ = 255;
        s.noteMidi(S_STARTUP, 67, 1.0f, 1.2f);  // the power-on swell
      } else if (e.kind == ST_SCORE && pendingHigh_) {
        s.phrase(PH_NEW_HIGH, 1.0f);
        pendingHigh_ = false;
      }
      if (e.kind != ST_PLAY) {
        if (humOn_) { s.set(TAG_HUM, 0, 0, 0.05f); humOn_ = false; }
        if (lavaOn_) { s.set(TAG_LAVA, 0, 0, 0.2f); lavaOn_ = false; }
      }
      lastStage_ = e.kind;
      break;
    default:
      break;
  }
}

void SoundDirector::update(const AudioState& a) {
  if (!s_) return;
  Synth& s = *s_;
  tick_++;

  // ---- music
  bool inLevel = a.stage == ST_PLAY || a.stage == ST_INTRO || a.stage == ST_DEAD || a.stage == ST_WIN;
  bool wantMusic = musicOn && !a.paused && (inLevel || (a.stage == ST_ATTRACT && attractAudio));
  uint8_t track = a.stage == ST_ATTRACT ? (uint8_t)MUSIC_ABYSS : a.music;
  if (wantMusic && track < MUSIC_TRACK_COUNT) {
    if (track != lastTrack_ || tick_ % 120 == 0) { s.music(track); lastTrack_ = track; }
  } else if (lastTrack_ != 255) {
    s.musicStop();
    lastTrack_ = 255;
  }

  float target = a.tension * 0.6f + a.danger * 0.55f + (a.boss ? 0.25f : 0) + (a.dark ? 0.12f : 0) + a.lavaNear * 0.15f;
  if (a.stage != ST_PLAY) target *= 0.6f;
  if (a.stage == ST_ATTRACT) target = 0.12f;
  target = clampf(target, 0, 1);
  smoothedIntensity_ += (target - smoothedIntensity_) * 0.08f;
  if (tick_ % 6 == 0) s.intensity(smoothedIntensity_);

  // ---- continuous sounds
  bool playing = a.stage == ST_PLAY && !a.paused;
  if (moveSound && playing && a.speed > 0.04f && !a.attacking) {
    float freq = (55.0f + 120.0f * a.speed) * (1.0f + 0.25f * a.conveyor);
    float vol = 0.15f + 0.6f * a.speed;
    if (!humOn_ || tick_ % 30 == 0) {
      s.note(S_HUM, freq, vol, -1.0f, TAG_HUM);
      humOn_ = true;
    } else if (tick_ % 2 == 0) {
      s.set(TAG_HUM, freq, vol, 0.06f);
    }
  } else if (humOn_) {
    s.set(TAG_HUM, 0, 0, 0.05f);
    humOn_ = false;
  }

  if (playing && a.lavaNear > 0.02f) {
    float k = a.lavaNear;
    float vol = 0.9f * k * sqrtf_(k);
    if (!lavaOn_ || tick_ % 30 == 0) {
      s.note(S_SIZZLE, 0, vol, -1.0f, TAG_LAVA);
      lavaOn_ = true;
    } else if (tick_ % 3 == 0) {
      s.set(TAG_LAVA, 0, vol, 0.1f);
    }
  } else if (lavaOn_) {
    s.set(TAG_LAVA, 0, 0, 0.2f);
    lavaOn_ = false;
  }
}

void SoundDirector::testSound(int id) {
  if (!s_) return;
  Event e;
  e.kind = 0;
  e.pos = 500;
  e.value = 1;
  static const uint8_t MAP[] = {EV_ATTACK, EV_KILL, EV_ARMOR_HIT, EV_SPLIT, EV_DIE, EV_LEVEL_CLEAR, EV_BOSS_HIT,
                                EV_BOSS_KILLED, EV_PICKUP_GEM, EV_PICKUP_LIFE, EV_CHECKPOINT, EV_LAVA_IGNITE,
                                EV_BOSS_RAGE_WARN, EV_GAME_OVER};
  if (id >= 0 && id < (int)sizeof(MAP)) {
    e.type = MAP[id];
    if (e.type == EV_KILL) e.value = 1 + (int)((jitter() * 0.5f + 0.5f) * 5);
    onEvent(e);
  } else if (id == 100) {
    e.type = EV_STAGE;
    e.kind = ST_BOOT;
    onEvent(e);
  } else if (id >= 200 && id < 200 + MUSIC_TRACK_COUNT) {
    s_->music((uint8_t)(id - 200));
    s_->intensity(0.7f);
  } else if (id == 299) {
    s_->musicStop();
  }
}

}  // namespace tw
