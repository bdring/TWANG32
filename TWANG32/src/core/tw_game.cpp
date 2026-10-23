#include "tw_game.h"

namespace tw {

// ------------------------------------------------------------------ tuning
static const int MAX_PLAYER_SPEED = 10;   // world units per tick (original)
static const int ATTACK_DURATION = 500;   // ms (original)
static const int BOOT_DUR = 1500;
static const int WIN_DUR = 1200;
static const int GAMEOVER_DUR = 1500;
static const int SCORE_DUR = 3200;
static const int DEATH_FLASH_DUR = 200;
static const int COMBO_WINDOW = 900;
static const int PHANTOM_CYCLE = 2600;
static const int PHANTOM_SOLID = 1600;
static const int LAVA_WARN_MS = 600;
static const int SPAWN_WARN_MS = 250;
static const int DARK_RADIUS = 95;

struct Palette {
  RGB player, exit, enemy, armored, chaser, splitter, phantom, mini, boss, attack, gem, life, checkpoint, checkpointOn, ice;
};
static const Palette PALETTES[2] = {
    // classic: the original TWANG colours
    {{0, 255, 0}, {0, 0, 255}, {255, 0, 0}, {255, 0, 140}, {255, 50, 0}, {255, 190, 0}, {130, 0, 255}, {255, 20, 0},
     {139, 0, 0}, {0, 0, 255}, {255, 230, 160}, {120, 255, 120}, {0, 40, 70}, {0, 200, 255}, {0, 22, 30}},
    // colour-blind friendly: player cyan, exit white
    {{0, 190, 255}, {255, 255, 255}, {255, 80, 0}, {255, 0, 150}, {255, 40, 0}, {255, 200, 0}, {170, 0, 255}, {255, 60, 0},
     {150, 30, 0}, {0, 60, 255}, {255, 230, 160}, {0, 255, 190}, {50, 50, 0}, {255, 255, 0}, {0, 22, 30}},
};
static inline const Palette& pal(uint8_t p) { return PALETTES[p > 1 ? 0 : p]; }

// ------------------------------------------------------------------ colour helpers
RGB hsv(uint8_t h, uint8_t s, uint8_t v) {
  // smooth six-sector spectrum, close enough to FastLED's rainbow for screensavers
  uint16_t region = h / 43;
  uint16_t rem = (h - region * 43) * 6;
  uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
  uint8_t q = (uint8_t)((v * (255 - ((s * rem) >> 8))) >> 8);
  uint8_t t = (uint8_t)((v * (255 - ((s * (255 - rem)) >> 8))) >> 8);
  switch (region) {
    case 0: return RGB{v, t, p};
    case 1: return RGB{q, v, p};
    case 2: return RGB{p, v, t};
    case 3: return RGB{p, q, v};
    case 4: return RGB{t, p, v};
    default: return RGB{v, p, q};
  }
}

RGB heatColor(uint8_t temperature) {
  uint8_t t192 = (uint8_t)(((uint16_t)temperature * 191) >> 8);
  if (temperature) t192++;
  uint8_t heatramp = (uint8_t)((t192 & 0x3F) << 2);
  if (t192 & 0x80) return RGB{255, 255, heatramp};
  if (t192 & 0x40) return RGB{255, heatramp, 0};
  return RGB{heatramp, 0, 0};
}

// ------------------------------------------------------------------ setup
void Game::init(const Campaign* campaign, uint32_t seed) {
  campaign_ = campaign;
  rng_.seed(seed);
  fxRng_.seed(seed ^ 0xA5A5F00Du);
  runSeed_ = seed;
  memzero(enemies_, sizeof(enemies_));
  memzero(particles_, sizeof(particles_));
  memzero(spawners_, sizeof(spawners_));
  memzero(lava_, sizeof(lava_));
  memzero(&boss_, sizeof(boss_));
  memzero(fireHeat_, sizeof(fireHeat_));
  L_.clear();
  evHead_ = evTail_ = 0;
  boot();
}

void Game::setCampaign(const Campaign* campaign) {
  campaign_ = campaign;
  if ((mode_ == MODE_CAMPAIGN || mode_ == MODE_PRACTICE) && stage_ != ST_ATTRACT && stage_ != ST_BOOT) {
    if (!campaign_ || levelIndex_ >= campaign_->count) boot();
  }
}

void Game::emit(uint8_t type, uint8_t kind, int pos, int32_t value) {
  int next = (evHead_ + 1) % EVENT_QUEUE;
  if (next == evTail_) return;  // full: drop
  Event& e = events_[evHead_];
  e.type = type;
  e.kind = kind;
  e.pos = (int16_t)pos;
  e.value = value;
  evHead_ = next;
}

bool Game::pollEvent(Event& e) {
  if (evTail_ == evHead_) return false;
  e = events_[evTail_];
  evTail_ = (evTail_ + 1) % EVENT_QUEUE;
  return true;
}

void Game::setStage(Stage s) {
  stage_ = s;
  stageStart_ = clock_;
  emit(EV_STAGE, (uint8_t)s);
}

int Game::firstEnabled() const { return nextEnabled(-1); }

int Game::nextEnabled(int from) const {
  if (!campaign_) return -1;
  for (int i = from + 1; i < campaign_->count; i++)
    if (campaign_->levels[i].enabled()) return i;
  return -1;
}

int Game::livesForLevel() const {
  int l = opt.livesPerLevel + (opt.difficulty == 0 ? 1 : (opt.difficulty == 2 ? -1 : 0));
  return clampi(l, 1, 9);
}

bool Game::resolveLevel(int index, LevelDef& out) {
  if (mode_ == MODE_TEST) {
    if (!testValid_) return false;
    memcopy(&out, &testLevel_, sizeof(LevelDef));
    return true;
  }
  if (mode_ == MODE_ENDLESS) {
    generateLevel(index, runSeed_, out);
    return true;
  }
  if (!campaign_ || index < 0 || index >= campaign_->count) return false;
  memcopy(&out, &campaign_->levels[index], sizeof(LevelDef));
  return true;
}

void Game::boot() {
  mode_ = defaultMode_;
  finalVictory_ = false;
  paused_ = false;
  setStage(ST_BOOT);
}

void Game::goAttract() {
  memzero(fireHeat_, sizeof(fireHeat_));
  setStage(ST_ATTRACT);
  emit(EV_ATTRACT);
}

void Game::startRun(Mode m, int levelIndex) {
  mode_ = m;
  score_ = 0;
  combo_ = 0;
  furthest_ = 0;
  newHigh_ = false;
  finalVictory_ = false;
  paused_ = false;
  runSeed_ = rng_.next();
  if (m == MODE_TEST && !testValid_) mode_ = MODE_CAMPAIGN;
  if (mode_ == MODE_ENDLESS) {
    levelIndex_ = levelIndex > 0 ? levelIndex : 0;
  } else if (mode_ == MODE_CAMPAIGN || mode_ == MODE_PRACTICE) {
    levelIndex_ = (levelIndex >= 0 && campaign_ && levelIndex < campaign_->count) ? levelIndex : firstEnabled();
    if (levelIndex_ < 0) {  // nothing playable: fall back to endless so the strip is never dead
      mode_ = MODE_ENDLESS;
      levelIndex_ = 0;
    }
  } else {
    levelIndex_ = 0;
  }
  lives_ = livesForLevel();
  lastInput_ = realTicks_;
  emit(EV_RUN_START, (uint8_t)mode_);
  beginLevelIntro(false);
}

void Game::testLevel(const LevelDef& L, bool spectate) {
  memcopy(&testLevel_, &L, sizeof(LevelDef));
  testValid_ = true;
  spectate_ = spectate;
  startRun(MODE_TEST, 0);
}

void Game::skipLevel() {
  if (stage_ == ST_PLAY || stage_ == ST_INTRO || stage_ == ST_DEAD) nextLevel();
}

void Game::beginLevelIntro(bool retry) {
  if (!retry) {
    if (!resolveLevel(levelIndex_, L_)) {
      boot();
      return;
    }
    if (!opt.livesPerRun || mode_ == MODE_TEST || mode_ == MODE_PRACTICE) lives_ = livesForLevel();
    respawnPos_ = -1;
    for (int i = 0; i < MAX_PICKUPS; i++) pickupAlive_[i] = i < L_.nPickups;
    for (int i = 0; i < MAX_CHECKPOINTS; i++) checkpointOn_[i] = false;
    if (levelIndex_ > furthest_) furthest_ = levelIndex_;
  }
  // clear the field so the intro only shows the lives bar
  for (int i = 0; i < ENEMY_POOL; i++) enemies_[i].alive = 0;
  for (int i = 0; i < PARTICLE_POOL; i++) particles_[i].alive = 0;
  for (int i = 0; i < SPAWNER_POOL; i++) spawners_[i].alive = false;
  for (int i = 0; i < MAX_LAVA; i++) lava_[i].alive = false;
  boss_.alive = false;
  attacking_ = false;
  introRetry_ = retry;
  emit(EV_LEVEL_START, retry ? 1 : 0, 0, levelIndex_);
  setStage(ST_INTRO);
}

static float speedScaleFor(uint8_t d) { return d == 0 ? 0.8f : (d == 2 ? 1.2f : 1.0f); }
static float rateScaleFor(uint8_t d) { return d == 0 ? 1.25f : (d == 2 ? 0.8f : 1.0f); }

void Game::loadLevel(bool retry) {
  attackWidth_ = L_.attackWidth;
  ppos_ = (float)((retry && respawnPos_ >= 0) ? respawnPos_ : L_.playerStart);
  pvel_ = 0;
  conveyorMod_ = 0;
  attacking_ = false;
  freeze_ = 0;

  int p = (int)ppos_;
  for (int i = 0; i < L_.nEnemies; i++) {
    const EnemyDef& e = L_.enemies[i];
    if (retry && respawnPos_ >= 0 && e.wobble == 0 && absi(e.pos - p) < 50) continue;  // no spawn-kills at checkpoints
    spawnEnemy(e.pos, e.dir, e.speed, e.wobble, e.kind);
  }
  float rs = rateScaleFor(opt.difficulty);
  for (int i = 0; i < L_.nSpawners && i < MAX_SPAWNERS; i++) {
    Spawner& s = spawners_[i];
    s.d = L_.spawners[i];
    s.d.rate = (uint16_t)clampi((int)(s.d.rate * rs), 250, 60000);
    s.activateAt = clock_ + s.d.delay;
    s.lastSpawned = 0;
    s.fired = false;
    s.alive = true;
  }
  for (int i = 0; i < L_.nLava; i++) {
    Lava& l = lava_[i];
    l.d = L_.lava[i];
    l.left = l.d.left;
    l.right = l.d.right;
    l.width = l.d.right - l.d.left;
    l.growth = 0;
    l.flow = 0;
    l.lastToggle = clock_ - l.d.offset;
    l.on = l.d.startOn;
    l.alive = true;
    l.warned = false;
  }
  if (L_.boss.present) {
    boss_.alive = true;
    boss_.maxLives = boss_.lives = L_.boss.hp;
    boss_.pos = L_.boss.positions[0];
    boss_.immune = 0;
    boss_.flash = 0;
    boss_.rageState = 0;
    boss_.rageAt = clock_ + 2500;
    spawnBossMinions();
  }
  levelStart_ = clock_;
}

// ------------------------------------------------------------------ spawning
bool Game::Enemy::solid(uint32_t t) const {
  if (kind != EK_PHANTOM) return true;
  return ((t + phase) % PHANTOM_CYCLE) < (uint32_t)PHANTOM_SOLID;
}

void Game::spawnEnemy(int pos, int dir, int speed, int wobble, uint8_t kind) {
  for (int i = 0; i < ENEMY_POOL; i++) {
    Enemy& e = enemies_[i];
    if (e.alive) continue;
    e.pos16 = pos << 4;
    e.origin = (int16_t)pos;
    e.dir = (int8_t)(dir > 0 ? 1 : -1);
    e.speed16 = (int16_t)(speed * 16 * speedScaleFor(opt.difficulty) + 0.5f);
    e.wobble = (int16_t)wobble;
    e.kind = kind;
    e.hp = kind == EK_ARMORED ? 2 : 1;
    e.alive = 1;
    e.immune = 0;
    e.flash = 0;
    e.phase = (uint16_t)rng_.below(PHANTOM_CYCLE);
    e.stunUntil = 0;
    e.playerSide = pos > (int)ppos_ ? 1 : -1;
    return;
  }
}

void Game::spawnParticles(int pos, int count, uint8_t type, RGB color) {
  for (int i = 0; i < PARTICLE_POOL && count > 0; i++) {
    Particle& p = particles_[i];
    if (p.alive) continue;
    p.pos = pos;
    p.sp = type == 0 ? rng_.range(-200, 200) : rng_.range(-90, 90);
    p.power = 255;
    p.alive = 1;
    p.life = type == 0 ? 220 - absi(p.sp) : 60 - absi(p.sp) / 3;
    p.type = type;
    p.color = color;
    count--;
  }
}

void Game::spawnBossMinions() {
  int phase = boss_.maxLives - boss_.lives;
  phase = clampi(phase, 0, MAX_BOSS_HP - 1);
  int rate = clampi((int)(L_.boss.rates[phase] * rateScaleFor(opt.difficulty)), 250, 60000);
  for (int k = 0; k < 2; k++) {
    Spawner& s = spawners_[MAX_SPAWNERS + k];
    bool wasFired = s.alive && s.fired;
    uint32_t last = s.lastSpawned;
    s.d.pos = (int16_t)boss_.pos;
    s.d.rate = (uint16_t)rate;
    s.d.speed = L_.boss.speed;
    s.d.dir = k == 0 ? -1 : 1;
    s.d.kind = EK_BASIC;
    s.d.delay = 0;
    s.activateAt = clock_;
    s.alive = true;
    s.fired = wasFired;  // keep the cadence through a phase change, like the original
    s.lastSpawned = last;
  }
}

void Game::killEnemy(Enemy& e, bool byAttack) {
  const Palette& P = pal(opt.palette);
  e.alive = 0;
  int pos = e.pos();
  RGB c = e.kind == EK_ARMORED ? P.armored : e.kind == EK_CHASER ? P.chaser : e.kind == EK_SPLITTER ? P.splitter
          : e.kind == EK_PHANTOM ? P.phantom : P.enemy;
  if (byAttack) {
    if (clock_ - lastKill_ <= (uint32_t)COMBO_WINDOW && combo_ > 0) combo_ = mini(combo_ + 1, 8);
    else combo_ = 1;
    lastKill_ = clock_;
    if (!L_.safe()) score_ += 10 * combo_;
    emit(EV_KILL, e.kind, pos, combo_);
    if (opt.juice) {
      freeze_ = 2;
      flashPos_ = pos;
      flashTicks_ = 7;
      spawnParticles(pos, 10, 1, c);
    }
    if (e.kind == EK_SPLITTER) {
      int away = pos >= (int)ppos_ ? 1 : -1;
      for (int k = 1; k <= 2; k++) {
        int sp = pos + away * (60 + 70 * k);
        if (sp > 5 && sp < WORLD - 5) spawnEnemy(sp, -away, 2, 0, EK_MINI);
      }
      emit(EV_SPLIT, 0, pos);
    }
  } else {
    emit(EV_KILL, e.kind, pos, 0);
    if (opt.juice) spawnParticles(pos, 4, 1, RGB{255, 120, 0});
  }
}

// ------------------------------------------------------------------ queries
bool Game::inLava(int pos) const {
  for (int i = 0; i < MAX_LAVA; i++) {
    const Lava& l = lava_[i];
    if (l.alive && l.on && (int)l.left <= pos && (int)l.right >= pos) return true;
  }
  if (boss_.alive && boss_.rageState == 2) {
    int r = L_.boss.width / 2 + 75;
    if (absi(pos - boss_.pos) <= r) return true;
  }
  return false;
}

bool Game::inZone(int pos, uint8_t type) const {
  for (int i = 0; i < L_.nZones; i++)
    if (L_.zones[i].type == type && pos >= L_.zones[i].start && pos <= L_.zones[i].end) return true;
  return false;
}

int Game::conveyorPush(int pos) const {
  int push = 0;
  for (int i = 0; i < L_.nConveyors; i++) {
    const ConveyorDef& c = L_.conveyors[i];
    if (pos > c.start && pos < c.end) push = clampi(c.speed, -MAX_PLAYER_SPEED + 1, MAX_PLAYER_SPEED - 1);
  }
  return push;
}

int Game::enemiesAlive() const {
  int n = 0;
  for (int i = 0; i < ENEMY_POOL; i++) n += enemies_[i].alive ? 1 : 0;
  return n;
}

AudioState Game::audio() const {
  AudioState a;
  memzero(&a, sizeof(a));
  a.stage = stage_;
  a.paused = paused_;
  a.music = (stage_ == ST_ATTRACT || stage_ == ST_BOOT) ? (uint8_t)MUSIC_ABYSS : L_.resolvedMusic();
  a.tension = L_.tension / 100.0f;
  a.boss = boss_.alive;
  a.attacking = attacking_;
  if (stage_ != ST_PLAY) return a;
  int p = (int)ppos_;
  int best = 10000;
  for (int i = 0; i < ENEMY_POOL; i++) {
    const Enemy& e = enemies_[i];
    if (!e.alive || !e.solid(clock_)) continue;
    best = mini(best, absi(e.pos() - p));
  }
  if (boss_.alive) best = mini(best, maxi(0, absi(boss_.pos - p) - L_.boss.width / 2));
  a.danger = clampf(1.0f - best / 300.0f, 0, 1);
  int lavaD = 10000;
  for (int i = 0; i < MAX_LAVA; i++) {
    const Lava& l = lava_[i];
    if (!l.alive || !l.on) continue;
    int d = p < l.left ? (int)l.left - p : (p > l.right ? p - (int)l.right : 0);
    lavaD = mini(lavaD, d);
  }
  a.lavaNear = clampf(1.0f - lavaD / 220.0f, 0, 1);
  a.speed = clampf(absf(pvel_) / (float)MAX_PLAYER_SPEED, 0, 1);
  a.conveyor = conveyorMod_ / 9.0f;
  a.dark = inZone(p, ZONE_DARK);
  return a;
}

// ------------------------------------------------------------------ main tick
void Game::tick(const Input& in) {
  realTicks_++;
  if (in.active) lastInput_ = realTicks_;
  if (paused_) return;
  if (flashTicks_ > 0) flashTicks_--;
  if (screenTintTicks_ > 0) screenTintTicks_--;
  if (freeze_ > 0) {  // hit-stop: the world holds its breath for a couple of frames
    freeze_--;
    return;
  }
  clockFrac_ += 1000;
  clock_ += clockFrac_ / TICK_HZ;
  clockFrac_ %= TICK_HZ;
  uint32_t t = clock_ - stageStart_;

  bool idle = (realTicks_ - lastInput_) > (uint32_t)opt.idleTimeoutSec * TICK_HZ;
  if (opt.attract && idle && mode_ != MODE_TEST && stage_ != ST_ATTRACT && stage_ != ST_BOOT &&
      stage_ != ST_SCORE && stage_ != ST_GAMEOVER) {
    if (score_ > 0 && mode_ != MODE_PRACTICE) emit(EV_GAME_OVER, 0, furthest_, score_);  // abandoned run still counts
    goAttract();
    return;
  }

  switch (stage_) {
    case ST_ATTRACT:
      if (in.active) boot();
      break;
    case ST_BOOT:
      if (t > (uint32_t)BOOT_DUR) startRun(mode_ == MODE_TEST ? MODE_CAMPAIGN : mode_, -1);
      break;
    case ST_INTRO: {
      uint32_t dur = 70u * (uint32_t)lives_ + 450u;
      if (t > dur) {
        loadLevel(introRetry_);
        setStage(ST_PLAY);
        levelStart_ = clock_;
      }
      break;
    }
    case ST_PLAY:
      updatePlay(in);
      break;
    case ST_DEAD:
      if (!tickParticles() && t > (uint32_t)DEATH_FLASH_DUR) beginLevelIntro(true);
      break;
    case ST_WIN:
      tickParticles();
      if (t > (uint32_t)WIN_DUR) nextLevel();
      break;
    case ST_BOSS_KILLED:
      if (t > (finalVictory_ ? 7000u : 2600u)) {
        if (finalVictory_) setStage(ST_SCORE);
        else nextLevel();
      }
      break;
    case ST_GAMEOVER:
      if (t > (uint32_t)GAMEOVER_DUR) setStage(ST_SCORE);
      break;
    case ST_SCORE:
      if (t > (uint32_t)SCORE_DUR) boot();
      break;
    default:
      break;
  }
}

void Game::updatePlay(const Input& in) {
  uint32_t now = clock_;
  int prev = (int)ppos_;

  if (attacking_ && attackStart_ + ATTACK_DURATION < now) attacking_ = false;
  if (!attacking_ && in.attack) {
    attacking_ = true;
    attackStart_ = now;
    attackId_++;
    if (attackId_ == 0) attackId_ = 1;
    emit(EV_ATTACK, 0, prev);
  }

  ppos_ += conveyorMod_;
  if (!attacking_) {
    int move = (int)(in.tilt / 6.0f);  // 6.0 is the original's tuning at 60 fps
    move = clampi(move, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
    if (inZone((int)ppos_, ZONE_ICE)) {
      pvel_ += ((float)move - pvel_) * 0.045f;  // slippery: momentum carries you
      ppos_ += pvel_;
    } else {
      pvel_ = (float)move;
      ppos_ += (float)move;
    }
  } else if (inZone((int)ppos_, ZONE_ICE)) {
    ppos_ += pvel_;  // sliding twang
    pvel_ *= 0.985f;
  }
  if (ppos_ < 0) { ppos_ = 0; pvel_ = 0; }
  if (boss_.alive && ppos_ >= WORLD) ppos_ = WORLD - 1;
  if (ppos_ >= WORLD && !boss_.alive) {
    levelComplete();
    return;
  }

  int p = (int)ppos_;
  if (!spectate_ && inLava(p)) { die(); return; }

  conveyorMod_ = conveyorPush(p);
  updateSpawners();
  updateBoss();
  if (stage_ != ST_PLAY) return;
  updateLava();
  updateEnemies();
  if (stage_ != ST_PLAY) return;
  updatePickups(prev);
  tickParticles();
  if (combo_ > 0 && clock_ - lastKill_ > (uint32_t)COMBO_WINDOW) combo_ = 0;
}

void Game::updateSpawners() {
  uint32_t now = clock_;
  for (int i = 0; i < SPAWNER_POOL; i++) {
    Spawner& s = spawners_[i];
    if (!s.alive || (int32_t)(now - s.activateAt) < 0) continue;
    // never drop an enemy right on top of the player (boss minions excepted)
    if (i < MAX_SPAWNERS && absi(s.d.pos - (int)ppos_) < 40) continue;
    if (!s.fired || s.lastSpawned + s.d.rate < now) {
      spawnEnemy(s.d.pos, s.d.dir, s.d.speed, 0, s.d.kind);
      s.lastSpawned = now;
      s.fired = true;
      if (absi(s.d.pos - (int)ppos_) < 450) emit(EV_SPAWN, s.d.kind, s.d.pos);
    }
  }
}

void Game::updateBoss() {
  if (!boss_.alive) return;
  if (boss_.flash) boss_.flash--;
  int p = (int)ppos_;
  int half = L_.boss.width / 2;
  if (!spectate_ && absi(p - boss_.pos) < half) { die(); return; }

  if (attacking_ && boss_.immune != attackId_) {
    int a0 = p - attackWidth_ / 2, a1 = p + attackWidth_ / 2;
    if (a0 <= boss_.pos + half && a1 >= boss_.pos - half) {
      boss_.lives--;
      boss_.immune = attackId_;
      boss_.flash = 10;
      boss_.rageState = 0;
      boss_.rageAt = clock_ + 2000;
      if (!L_.safe()) score_ += 100;
      if (opt.juice) {
        freeze_ = 5;
        screenTint_ = RGB{40, 0, 0};
        screenTintTicks_ = 10;
        spawnParticles(boss_.pos, 18, 1, RGB{255, 40, 0});
      }
      emit(EV_BOSS_HIT, 0, boss_.pos, boss_.lives);
      if (boss_.lives <= 0) {
        boss_.alive = false;
        spawners_[MAX_SPAWNERS].alive = false;
        spawners_[MAX_SPAWNERS + 1].alive = false;
        // chain reaction: the boss's minions go with it
        for (int i = 0; i < ENEMY_POOL; i++) {
          if (!enemies_[i].alive) continue;
          spawnParticles(enemies_[i].pos(), 3, 1, RGB{255, 60, 0});
          enemies_[i].alive = 0;
          if (!L_.safe()) score_ += 10;
        }
        if (!L_.safe()) score_ += 1000;
        if (opt.juice) {
          screenTint_ = RGB{90, 30, 0};
          screenTintTicks_ = 24;
          spawnParticles(boss_.pos, 40, 0, RGB{255, 255, 255});
        }
        emit(EV_BOSS_KILLED, 0, boss_.pos, score_);
      } else {
        int phase = clampi(boss_.maxLives - boss_.lives, 0, MAX_BOSS_HP - 1);
        boss_.pos = L_.boss.positions[phase];
        spawnBossMinions();
      }
      return;
    }
  }

  // rage: final phase erupts lava around itself, always telegraphed
  if (L_.boss.rage && boss_.lives == 1) {
    uint32_t now = clock_;
    if (boss_.rageState == 0 && (int32_t)(now - boss_.rageAt) >= 0) {
      boss_.rageState = 1;
      boss_.rageAt = now + 750;
      emit(EV_BOSS_RAGE_WARN, 0, boss_.pos);
    } else if (boss_.rageState == 1 && (int32_t)(now - boss_.rageAt) >= 0) {
      boss_.rageState = 2;
      boss_.rageAt = now + 650;
      emit(EV_LAVA_IGNITE, 1, boss_.pos);
    } else if (boss_.rageState == 2 && (int32_t)(now - boss_.rageAt) >= 0) {
      boss_.rageState = 0;
      boss_.rageAt = now + 1900;
    }
  }
}

void Game::updateLava() {
  uint32_t now = clock_;
  int p = (int)ppos_;
  for (int i = 0; i < MAX_LAVA; i++) {
    Lava& l = lava_[i];
    if (!l.alive) continue;
    // grow and flow, exactly like the original (integer steps)
    if (l.d.grow != 0) {
      l.growth += l.d.grow;
      if (l.growth >= 1.0f) {
        if (l.left > 0) l.left -= 1;
        if (l.right < WORLD) l.right += 1;
        l.growth = 0;
      }
    }
    if (l.d.flow != 0) {
      l.flow += l.d.flow;
      if (absf(l.flow) >= 1) {
        int step = (int)l.flow;
        if (l.left > 1 && l.left < WORLD - l.width) l.left += step;
        if (l.right > l.width && l.right < WORLD) l.right += step;
        l.flow = 0;
      }
    }
    uint32_t elapsed = now - l.lastToggle;
    bool near = (p > l.left - 300) && (p < l.right + 300);
    if (!l.on) {
      if (!l.warned && elapsed + LAVA_WARN_MS > l.d.off) {
        l.warned = true;
        if (near) emit(EV_LAVA_WARN, 0, (int)((l.left + l.right) / 2));
      }
      if (elapsed > l.d.off) {
        l.on = true;
        l.lastToggle = now;
        if (near) emit(EV_LAVA_IGNITE, 0, (int)((l.left + l.right) / 2));
      }
    } else if (elapsed > l.d.on) {
      l.on = false;
      l.warned = false;
      l.lastToggle = now;
    }
  }
}

void Game::updateEnemies() {
  uint32_t now = clock_;
  uint32_t lt = now - levelStart_;
  int p = (int)ppos_;
  int half = attackWidth_ / 2;
  for (int i = 0; i < ENEMY_POOL; i++) {
    Enemy& e = enemies_[i];
    if (!e.alive) continue;
    if (e.flash) e.flash--;

    // ---- move
    if ((int32_t)(e.stunUntil - now) > 0) {
      // reeling from a twang: holds position long enough for you to twang again
    } else if (e.kind == EK_CHASER) {
      int d = p - e.pos();
      int step = mini(absi(d) * 16, e.speed16);
      e.pos16 += d > 0 ? step : -step;
    } else if (e.wobble > 0) {
      float sp = e.speed16 / 16.0f;
      e.pos16 = (int32_t)((e.origin + sinf_(((float)lt / 3000.0f) * sp) * e.wobble) * 16.0f);
    } else {
      e.pos16 += e.dir > 0 ? e.speed16 : -e.speed16;
      if (e.pos() > WORLD || e.pos() <= 0) { e.alive = 0; continue; }
    }
    int ep = e.pos();

    if (!e.solid(now)) {  // faded phantoms drift through you harmlessly
      e.playerSide = ep > p ? 1 : (ep < p ? -1 : e.playerSide);
      continue;
    }

    // ---- twang
    if (attacking_ && e.immune != attackId_ && ep > p - half && ep < p + half) {
      if (e.kind == EK_ARMORED && e.hp > 1) {
        e.hp--;
        e.immune = attackId_;
        e.flash = 8;
        int away = ep >= p ? 1 : -1;
        e.pos16 = clampi(ep + away * 70, 1, WORLD - 1) << 4;
        e.origin = (int16_t)e.pos();
        e.stunUntil = now + ATTACK_DURATION + 250;
        if (!L_.safe()) score_ += 5;
        emit(EV_ARMOR_HIT, 0, e.pos());
        if (opt.juice) { freeze_ = 2; spawnParticles(ep, 5, 1, RGB{255, 255, 255}); }
        continue;
      }
      killEnemy(e, true);
      continue;
    }
    // ---- lava burns enemies too
    if (inLava(ep)) { killEnemy(e, false); continue; }
    // ---- hit the player (crossing check, so fast things can't tunnel through)
    if (!spectate_ && ((e.playerSide == 1 && ep <= p) || (e.playerSide == -1 && ep >= p))) {
      die();
      return;
    }
    if (spectate_) e.playerSide = ep > p ? 1 : -1;
  }
}

void Game::updatePickups(int prevPos) {
  int p = (int)ppos_;
  int lo = mini(prevPos, p) - 4, hi = maxi(prevPos, p) + 4;
  for (int i = 0; i < L_.nPickups; i++) {
    if (!pickupAlive_[i]) continue;
    int pp = L_.pickups[i].pos;
    if (pp < lo || pp > hi) continue;
    pickupAlive_[i] = false;
    if (L_.pickups[i].type == PK_LIFE) {
      lives_ = mini(lives_ + 1, 9);
      emit(EV_PICKUP_LIFE, 0, pp, lives_);
      if (opt.juice) spawnParticles(pp, 12, 2, pal(opt.palette).life);
    } else {
      if (!L_.safe()) score_ += 50;
      emit(EV_PICKUP_GEM, 0, pp, 50);
      if (opt.juice) spawnParticles(pp, 10, 2, pal(opt.palette).gem);
    }
  }
  for (int i = 0; i < L_.nCheckpoints; i++) {
    if (checkpointOn_[i]) continue;
    int cp = L_.checkpoints[i];
    if (cp < lo || cp > hi) continue;
    checkpointOn_[i] = true;
    if (cp > respawnPos_) respawnPos_ = cp;
    emit(EV_CHECKPOINT, 0, cp);
    if (opt.juice) spawnParticles(cp, 8, 2, pal(opt.palette).checkpointOn);
  }
}

bool Game::tickParticles() {
  bool active = false;
  for (int i = 0; i < PARTICLE_POOL; i++) {
    Particle& p = particles_[i];
    if (!p.alive) continue;
    p.life++;
    if (p.sp > 0) p.sp -= p.life / 10;
    else p.sp += p.life / 10;
    if (opt.gravity && p.pos > opt.bendPoint) p.sp -= 10;
    p.power = (p.type == 0 ? 100 : 70) - p.life;
    if (p.power <= 0) { p.alive = 0; continue; }
    p.pos += (int)(p.sp / 7.0f);
    if (p.pos > WORLD) { p.pos = WORLD; p.sp = -(p.sp / 2); }
    else if (p.pos < 0) { p.pos = 0; p.sp = -(p.sp / 2); }
    active = true;
  }
  return active;
}

// ------------------------------------------------------------------ outcomes
void Game::die() {
  if (spectate_ || stage_ != ST_PLAY) return;
  deathPos_ = (int)ppos_;
  combo_ = 0;
  emit(EV_DIE, 0, deathPos_, levelIndex_);
  if (opt.juice) { screenTint_ = RGB{60, 0, 0}; screenTintTicks_ = 12; }
  bool costsLife = !(L_.safe() || mode_ == MODE_PRACTICE || mode_ == MODE_TEST);
  if (costsLife) lives_--;
  if (lives_ <= 0) {
    gameOver();
    return;
  }
  for (int i = 0; i < ENEMY_POOL; i++) enemies_[i].alive = 0;  // stop the world, just the explosion
  spawnParticles(deathPos_, 96, 0, RGB{255, 0, 0});
  setStage(ST_DEAD);
}

void Game::gameOver() {
  scoreAtGameOver_ = score_;
  newHigh_ = score_ > highScore && score_ > 0;
  emit(EV_GAME_OVER, 0, furthest_, score_);
  if (newHigh_) emit(EV_NEW_HIGHSCORE, 0, 0, score_);
  setStage(ST_GAMEOVER);
}

void Game::levelComplete() {
  uint32_t secs = levelTime() / 1000;
  int32_t bonus = 0;
  if (!L_.safe() && mode_ != MODE_TEST) {
    bonus = 100 * lives_;
    if (L_.par > 0 && secs < L_.par) bonus += (int32_t)(L_.par - secs) * 20;
    if (mode_ == MODE_ENDLESS) bonus += 50 * levelIndex_;
  }
  score_ += bonus;
  emit(EV_LEVEL_CLEAR, (uint8_t)(levelIndex_ & 0xFF), (int)bonus, (int32_t)levelTime());
  bool wasBoss = L_.boss.present;
  bool last = mode_ == MODE_CAMPAIGN && nextEnabled(levelIndex_) < 0;
  if (last) {
    finalVictory_ = true;
    scoreAtGameOver_ = score_;
    newHigh_ = score_ > highScore;
    emit(EV_CAMPAIGN_DONE, 0, furthest_, score_);
    emit(EV_GAME_OVER, 1, furthest_, score_);
    if (newHigh_) emit(EV_NEW_HIGHSCORE, 0, 0, score_);
    setStage(ST_BOSS_KILLED);
  } else if (wasBoss) {
    setStage(ST_BOSS_KILLED);
  } else {
    setStage(ST_WIN);
  }
}

void Game::nextLevel() {
  if (mode_ == MODE_TEST) {
    beginLevelIntro(false);
    return;
  }
  if (mode_ == MODE_ENDLESS) {
    levelIndex_++;
    beginLevelIntro(false);
    return;
  }
  int n = nextEnabled(levelIndex_);
  if (n < 0) {
    if (mode_ == MODE_PRACTICE) n = firstEnabled();
    if (n < 0) { boot(); return; }
  }
  levelIndex_ = n;
  beginLevelIntro(false);
}

// ------------------------------------------------------------------ rendering
void Game::clearFb() { memzero(fb_, sizeof(RGB) * (size_t)n_); }

void Game::plotSmooth(int pos, RGB c) {
  if (!opt.smooth || n_ < 2) { add(led(pos), c); return; }
  float f = (float)pos * (float)(n_ - 1) / (float)WORLD;
  int i = (int)floorf_(f);
  float frac = f - (float)i;
  // perceptual split so a dot between two pixels doesn't look dimmer
  uint8_t a = (uint8_t)(sqrtf_(1.0f - frac) * 255.0f);
  uint8_t b = (uint8_t)(sqrtf_(frac) * 255.0f);
  add(clampi(i, 0, n_ - 1), scale(c, a));
  if (i + 1 < n_) add(i + 1, scale(c, b));
}

void Game::render(RGB* fb, int n) {
  if (!fb || n <= 0) return;
  fb_ = fb;
  n_ = n > WORLD ? WORLD : n;
  hue_++;
  switch (stage_) {
    case ST_BOOT: renderBoot(); break;
    case ST_INTRO: renderIntro(); break;
    case ST_PLAY: renderPlay(); break;
    case ST_DEAD: renderDead(); break;
    case ST_WIN: renderWin(); break;
    case ST_BOSS_KILLED: renderBossKilled(); break;
    case ST_GAMEOVER: renderGameover(); break;
    case ST_SCORE: renderScore(); break;
    case ST_ATTRACT: renderAttract(); break;
    default: clearFb(); break;
  }
  if (screenTintTicks_ > 0 && stage_ != ST_ATTRACT) {
    uint8_t s = (uint8_t)clampi(screenTintTicks_ * 20, 0, 255);
    RGB t = scale(screenTint_, s);
    for (int i = 0; i < n_; i++) fb_[i] = addc(fb_[i], t);
  }
  if (paused_) {  // slow breathing dim
    uint8_t s = (uint8_t)(60 + 40 * (sin2pi((float)realTicks_ / 120.0f) * 0.5f + 0.5f));
    for (int i = 0; i < n_; i++) fb_[i] = scale(fb_[i], s);
  }
}

void Game::renderBoot() {  // the original startup wipe, sparkle and fade
  clearFb();
  uint32_t t = clock_ - stageStart_;
  if (t < 200) {
    int top = mini((int)mapl(t, 0, 200, 0, n_), n_);
    for (int i = 0; i < top; i++) fb_[i] = RGB{0, 255, 0};
  } else if (t < 1300) {
    for (int i = 0; i < n_; i++) {
      if (fxRng_.below(30) < 28) fb_[i] = RGB{0, 255, 0};
      else { uint8_t f = (uint8_t)fxRng_.below(250); fb_[i] = RGB{f, 150, f}; }
    }
  } else {
    int lo = maxi((int)mapl(t, 1300, 1500, 0, n_), 0);
    uint8_t b = (uint8_t)clampi((int)mapl(t, 1300, 1500, 255, 0), 0, 255);
    for (int i = lo; i < n_; i++) fb_[i] = RGB{0, b, 0};
  }
}

void Game::renderIntro() {
  clearFb();
  uint32_t t = clock_ - stageStart_;
  int shown = mini(lives_, (int)(t / 70) + 1);
  int pos = 0;
  RGB c = pal(opt.palette).player;
  for (int i = 0; i < shown; i++) {
    for (int j = 0; j < 4; j++) put(pos++, c);
    pos++;
  }
  // a faint glimpse of the exit so you know which way is forward
  put(n_ - 1, scale(pal(opt.palette).exit, 60));
}

void Game::renderPlay() {
  const Palette& P = pal(opt.palette);
  clearFb();
  uint32_t now = clock_;

  // ice: cold shimmer
  for (int z = 0; z < L_.nZones; z++) {
    const ZoneDef& Z = L_.zones[z];
    if (Z.type != ZONE_ICE) continue;
    for (int i = led(Z.start); i <= led(Z.end); i++) {
      if (fxRng_.below(90) == 0) put(i, RGB{40, 110, 140});
      else put(i, P.ice);
    }
  }

  // conveyors: chevrons that scroll in the push direction, speed proportional to the push
  for (int c = 0; c < L_.nConveyors; c++) {
    const ConveyorDef& C = L_.conveyors[c];
    int sp = clampi(C.speed, -9, 9);
    if (sp == 0) continue;
    uint32_t phase = (now * (uint32_t)absi(sp)) / 160;
    int ss = led(C.start), ee = led(C.end);
    for (int i = ss; i < ee; i++) {
      int k = sp > 0 ? (int)((phase + 100000 - (uint32_t)i) % 5) : (int)((phase + (uint32_t)i) % 5);
      int b = (int)mapl(k, 5, 0, 0, opt.conveyorBrightness);
      if (b > 0) put(i, RGB{0, 0, (uint8_t)b});
    }
  }

  // lava
  for (int i = 0; i < MAX_LAVA; i++) {
    const Lava& l = lava_[i];
    if (!l.alive) continue;
    int A = led((int)l.left), B = led((int)l.right);
    uint32_t elapsed = now - l.lastToggle;
    if (!l.on) {
      bool warn = opt.telegraph && elapsed + LAVA_WARN_MS > l.d.off;
      int heat = warn ? (int)mapl(clampi((int)(elapsed + LAVA_WARN_MS - l.d.off), 0, LAVA_WARN_MS), 0, LAVA_WARN_MS, 0, 70) : 0;
      for (int p = A; p <= B; p++) {
        int base = opt.lavaOffBrightness + heat / 3;
        int fl = (int)fxRng_.below((uint32_t)(opt.lavaOffBrightness + 1 + heat / 2));
        RGB c = RGB{(uint8_t)clampi(base + fl, 0, 255), (uint8_t)clampi((base + fl) * 2 / 3, 0, 255), 0};
        if (warn && fxRng_.below(12) == 0) c = RGB{(uint8_t)(90 + heat), (uint8_t)(30 + heat / 2), 0};
        put(p, c);
      }
    } else {
      for (int p = A; p <= B; p++) put(p, fxRng_.below(30) < 29 ? RGB{150, 0, 0} : RGB{180, 100, 0});
    }
  }

  // boss rage zone
  if (boss_.alive && boss_.rageState) {
    int r = L_.boss.width / 2 + 75;
    for (int i = led(boss_.pos - r); i <= led(boss_.pos + r); i++) {
      if (boss_.rageState == 1) put(i, fxRng_.below(4) == 0 ? RGB{120, 40, 0} : RGB{25, 6, 0});
      else put(i, fxRng_.below(30) < 28 ? RGB{170, 10, 0} : RGB{220, 120, 0});
    }
  }

  // checkpoints
  for (int i = 0; i < L_.nCheckpoints; i++) put(led(L_.checkpoints[i]), checkpointOn_[i] ? P.checkpointOn : P.checkpoint);

  // pickups
  for (int i = 0; i < L_.nPickups; i++) {
    if (!pickupAlive_[i]) continue;
    int idx = led(L_.pickups[i].pos);
    if (L_.pickups[i].type == PK_GEM) {
      uint8_t s = (uint8_t)(110 + fxRng_.below(146));
      put(idx, scale(P.gem, s));
    } else {
      uint8_t s = (uint8_t)(90 + 165 * (sin2pi((float)now / 900.0f) * 0.5f + 0.5f));
      put(idx, scale(P.life, s));
    }
  }

  // spawner telegraph: a dim ember swells just before each spawn
  if (opt.telegraph) {
    for (int i = 0; i < SPAWNER_POOL; i++) {
      const Spawner& s = spawners_[i];
      if (!s.alive || !s.fired) continue;
      int32_t until = (int32_t)(s.lastSpawned + s.d.rate - now);
      if (until > 0 && until < SPAWN_WARN_MS) {
        uint8_t b = (uint8_t)mapl(SPAWN_WARN_MS - until, 0, SPAWN_WARN_MS, 5, 70);
        add(led(s.d.pos), RGB{b, 0, 0});
      }
    }
  }

  // boss
  if (boss_.alive) {
    int half = L_.boss.width / 2;
    float rate = 1200.0f / (float)(boss_.maxLives - boss_.lives + 1);
    uint8_t pulse = (uint8_t)(70 + 60 * (sin2pi((float)now / rate) * 0.5f + 0.5f));
    RGB c = boss_.flash ? RGB{255, 255, 255} : scale(P.boss, pulse);
    for (int i = led(boss_.pos - half); i <= led(boss_.pos + half); i++) put(i, c);
  }

  // enemies
  for (int i = 0; i < ENEMY_POOL; i++) {
    const Enemy& e = enemies_[i];
    if (!e.alive) continue;
    RGB c;
    switch (e.kind) {
      case EK_ARMORED: c = e.hp > 1 ? P.armored : blend(P.armored, P.enemy, 150); break;
      case EK_CHASER: c = scale(P.chaser, (uint8_t)(150 + 105 * (sin2pi((float)now / 300.0f) * 0.5f + 0.5f))); break;
      case EK_SPLITTER: c = P.splitter; break;
      case EK_PHANTOM:
        if (e.solid(now)) c = P.phantom;
        else c = fxRng_.below(3) == 0 ? scale(P.phantom, 26) : RGB{0, 0, 0};
        break;
      case EK_MINI: c = P.mini; break;
      default: c = P.enemy; break;
    }
    if (e.flash) c = RGB{255, 255, 255};
    put(led(e.pos()), c);
  }

  // particles (additive)
  for (int i = 0; i < PARTICLE_POOL; i++) {
    const Particle& p = particles_[i];
    if (!p.alive) continue;
    if (p.type == 0) {
      if (p.power < 5) { uint8_t b = (uint8_t)((5 - p.power) * 10); add(led(p.pos), RGB{b, (uint8_t)(b / 2), (uint8_t)(b / 2)}); }
      else add(led(p.pos), RGB{(uint8_t)clampi(p.power, 0, 255), 0, 0});
    } else {
      uint8_t s = (uint8_t)clampi(p.power * 4, 0, 255);
      add(led(p.pos), scale(p.color, s));
    }
  }

  // player + attack (original drawAttack)
  int pp = (int)ppos_;
  if (attacking_) {
    int n = (int)mapl((long)(now - attackStart_), 0, ATTACK_DURATION, 100, 5);
    int a0 = led(pp - attackWidth_ / 2), a1 = led(pp + attackWidth_ / 2);
    for (int i = a0 + 1; i <= a1 - 1; i++) put(i, RGB{0, 0, (uint8_t)clampi(n, 0, 255)});
    uint8_t e = n > 90 ? 255 : 0;
    put(led(pp), n > 90 ? RGB{255, 255, 255} : P.player);
    put(a0, RGB{e, e, 255});
    put(a1, RGB{e, e, 255});
    if (P.attack.r || P.attack.g) { put(a0, P.attack); put(a1, P.attack); }
  } else {
    plotSmooth(pp, P.player);
  }

  // exit
  if (!boss_.alive) {
    uint8_t s = (uint8_t)(170 + 85 * (sin2pi((float)now / 1400.0f) * 0.5f + 0.5f));
    put(n_ - 1, scale(P.exit, s));
  }

  applyDarkness();

  // kill flash
  if (flashTicks_ > 0 && flashPos_ >= 0) {
    uint8_t s = (uint8_t)clampi(flashTicks_ * 36, 0, 255);
    int c = led(flashPos_);
    RGB w = RGB{s, s, s};
    put(c, w);
    add(c - 1, scale(w, 120));
    add(c + 1, scale(w, 120));
  }
}

void Game::applyDarkness() {
  int pp = (int)ppos_;
  for (int z = 0; z < L_.nZones; z++) {
    const ZoneDef& Z = L_.zones[z];
    if (Z.type != ZONE_DARK) continue;
    for (int i = led(Z.start); i <= led(Z.end); i++) {
      int w = (int)mapl(i, 0, n_ - 1, 0, WORLD);
      int d = absi(w - pp);
      if (d >= DARK_RADIUS) {
        // in the dark only burning lava glows, faintly
        RGB c = fb_[i];
        fb_[i] = (c.r > 120 && c.b == 0 && c.g < 120) ? scale(c, 30) : RGB{0, 0, 0};
      } else {
        float k = 1.0f - (float)(d * d) / (float)(DARK_RADIUS * DARK_RADIUS);
        fb_[i] = scale(fb_[i], (uint8_t)clampi((int)(k * 255.0f), 18, 255));
      }
    }
  }
}

void Game::renderDead() {
  clearFb();
  uint32_t t = clock_ - stageStart_;
  if (t < (uint32_t)DEATH_FLASH_DUR) {
    int b = (int)mapl(t, 0, DEATH_FLASH_DUR, 255, 150);
    int c = led(deathPos_);
    int w = 20;
    int up = (int)mapl(t, 0, DEATH_FLASH_DUR, c, c + w);
    int dn = (int)mapl(t, 0, DEATH_FLASH_DUR, c, c - w);
    for (int i = maxi(dn, 0); i <= mini(up, n_ - 1); i++) fb_[i] = RGB{255, (uint8_t)b, (uint8_t)b};
  }
  for (int i = 0; i < PARTICLE_POOL; i++) {
    const Particle& p = particles_[i];
    if (!p.alive) continue;
    if (p.power < 5) { uint8_t b = (uint8_t)((5 - p.power) * 10); add(led(p.pos), RGB{b, (uint8_t)(b / 2), (uint8_t)(b / 2)}); }
    else add(led(p.pos), p.type == 0 ? RGB{(uint8_t)clampi(p.power, 0, 255), 0, 0} : scale(p.color, (uint8_t)clampi(p.power * 4, 0, 255)));
  }
}

void Game::renderWin() {  // original level-complete wipe
  clearFb();
  uint32_t t = clock_ - stageStart_;
  RGB g = scale(pal(opt.palette).player, 200);
  if (t < 500) {
    int lo = maxi((int)mapl(t, 0, 500, n_, 0), 0);
    for (int i = n_ - 1; i >= lo; i--) fb_[i] = g;
  } else if (t < 1000) {
    int hi = maxi((int)mapl(t, 500, 1000, n_, 0), 0);
    for (int i = 0; i < hi && i < n_; i++) fb_[i] = g;
  } else {
    fb_[0] = g;
  }
}

void Game::renderBossKilled() {
  uint32_t t = clock_ - stageStart_;
  uint32_t dur = finalVictory_ ? 7000 : 2600;
  clearFb();
  if (t + 500 < dur) {
    for (int i = 0; i < n_; i++) fb_[i] = hsv((uint8_t)(hue_ * 2 + i * 7), 240, 255);
    if (fxRng_.below(256) < 200) fb_[fxRng_.below((uint32_t)n_)] = RGB{255, 255, 255};
  } else {
    int hi = maxi((int)mapl(t, dur - 500, dur, n_, 0), 0);
    for (int i = 0; i < hi && i < n_; i++) fb_[i] = hsv((uint8_t)((i * 10 + t / 2) & 0xFF), 255, 50);
  }
}

void Game::renderGameover() {
  clearFb();
  uint32_t t = clock_ - stageStart_;
  int c = led(deathPos_);
  if (t < 1000) {
    int up = mini((int)mapl(t, 0, 1000, c, n_), n_ - 1);
    int dn = maxi((int)mapl(t, 0, 1000, c, 0), 0);
    for (int i = dn; i <= up; i++) fb_[i] = RGB{255, 0, 0};
  } else {
    uint8_t b = (uint8_t)clampi((int)mapl(t, 1000, GAMEOVER_DUR, 200, 0), 0, 255);
    for (int i = 0; i < n_; i++) fb_[i] = RGB{b, 0, 0};
  }
}

void Game::renderScore() {
  // A gold bar shows your score against the high score (white marker).
  clearFb();
  uint32_t t = clock_ - stageStart_;
  int32_t ref = maxi((int)highScore, (int)scoreAtGameOver_);
  if (ref <= 0) ref = 1;
  int span = (n_ * 9) / 10;
  int target = (int)((int64_t)scoreAtGameOver_ * span / ref);
  int hiMark = (int)((int64_t)highScore * span / ref);
  int shown = (int)mapl(mini((int)t, 1400), 0, 1400, 0, target);
  for (int i = 0; i < shown && i < n_; i++) {
    RGB c = RGB{255, 140, 0};
    if (newHigh_ && t > 1400) c = hsv((uint8_t)(hue_ * 3 + i * 5), 255, 255);
    fb_[i] = c;
  }
  if (!newHigh_ && highScore > 0 && hiMark < n_) {
    uint8_t s = (uint8_t)(140 + 115 * (sin2pi((float)t / 400.0f) * 0.5f + 0.5f));
    fb_[clampi(hiMark, 0, n_ - 1)] = RGB{s, s, s};
  }
  if (newHigh_ && t > 1400 && fxRng_.below(3) == 0) fb_[fxRng_.below((uint32_t)maxi(target, 1))] = RGB{255, 255, 255};
}

void Game::renderAttract() {
  uint32_t ms = realTicks_ * 1000 / TICK_HZ;
  int modeSel = (int)((ms / 30000) % 5);
  if (modeSel == 0) {  // Fire2012 by Mark Kriegsman
    const int COOLING = 75, SPARKING = 40;
    for (int i = 0; i < n_; i++) fireHeat_[i] = qsub8(fireHeat_[i], (uint8_t)fxRng_.below((uint32_t)((COOLING * 10) / n_ + 2)));
    for (int k = n_ - 1; k >= 2; k--) fireHeat_[k] = (uint8_t)((fireHeat_[k - 1] + fireHeat_[k - 2] + fireHeat_[k - 2]) / 3);
    if (fxRng_.u8() < SPARKING) { int y = (int)fxRng_.below(7); fireHeat_[y] = qadd8(fireHeat_[y], (uint8_t)fxRng_.range(160, 255)); }
    for (int j = 0; j < n_; j++) fb_[j] = heatColor(fireHeat_[j]);
  } else if (modeSel == 1) {  // sinelon
    for (int i = 0; i < n_; i++) fb_[i] = scale(fb_[i], 235);
    int p = beatsin(ms, 13, 0, n_ - 1);
    add(p, hsv(hue_, 255, 192));
  } else if (modeSel == 2) {  // juggle
    for (int i = 0; i < n_; i++) fb_[i] = scale(fb_[i], 235);
    uint8_t dh = 0;
    for (int i = 0; i < 4; i++) {
      int p = beatsin(ms, (float)(i + 7), 0, n_ - 1);
      RGB c = hsv(dh, 200, 255);
      fb_[p] = RGB{(uint8_t)(fb_[p].r | c.r), (uint8_t)(fb_[p].g | c.g), (uint8_t)(fb_[p].b | c.b)};
      dh += 64;
    }
  } else if (modeSel == 3) {  // amber flashes
    for (int i = 0; i < n_; i++) fb_[i] = scale(fb_[i], 250);
    for (int i = 0; i < n_; i++) if (fxRng_.below(20) == 0) fb_[i] = hsv(25, 255, 100);
  } else {  // embers: slow breathing coals with the odd rising spark
    float tt = (float)ms / 1000.0f;
    for (int i = 0; i < n_; i++) {
      float x = (float)i / (float)n_;
      float v = sin2pi(x * 3.1f + tt * 0.07f) * 0.5f + sin2pi(x * 7.3f - tt * 0.11f) * 0.3f + sin2pi(x * 17.0f + tt * 0.23f) * 0.2f;
      v = clampf(v * 0.5f + 0.5f, 0, 1);
      v = v * v * v;
      fb_[i] = RGB{(uint8_t)(10 + v * 120), (uint8_t)(v * 22), 0};
    }
    if (fxRng_.below(40) == 0) fb_[fxRng_.below((uint32_t)n_)] = RGB{255, 90, 10};
  }
}

}  // namespace tw
