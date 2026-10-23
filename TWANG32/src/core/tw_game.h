/*
  TWANG Next - game engine.

  Hardware agnostic: feed it Input once per 60 Hz tick, ask it to render into
  an RGB buffer of any length, and drain the event queue for sound, stats and
  the web UI. The original TWANG32 feel (movement speed, collision rules, lava
  timing) is preserved; everything else is layered on top.
*/
#pragma once
#include "tw_base.h"
#include "tw_level.h"

namespace tw {

enum Stage : uint8_t {
  ST_BOOT = 0,     // power-on swell animation
  ST_INTRO,        // lives display before a level
  ST_PLAY,
  ST_DEAD,         // explosion + particles, then retry
  ST_WIN,          // level cleared wipe
  ST_BOSS_KILLED,  // rainbow celebration
  ST_GAMEOVER,     // red spread from where you died
  ST_SCORE,        // score bar vs. high score
  ST_ATTRACT,      // screensaver
  ST_COUNT
};

enum Mode : uint8_t {
  MODE_CAMPAIGN = 0,
  MODE_ENDLESS,   // procedurally generated, gets harder forever
  MODE_PRACTICE,  // start anywhere, lives never run out
  MODE_TEST,      // single level from the editor, loops forever
  MODE_COUNT
};

enum EventType : uint8_t {
  EV_NONE = 0,
  EV_RUN_START,       // value = mode
  EV_LEVEL_START,     // value = level index, kind = 1 if retry
  EV_ATTACK,
  EV_KILL,            // kind = enemy kind, value = combo
  EV_ARMOR_HIT,
  EV_SPLIT,
  EV_SPAWN,
  EV_DIE,             // pos = where, value = level index
  EV_LEVEL_CLEAR,     // value = time in ms, kind = level index (low byte)
  EV_BOSS_HIT,        // value = hits remaining
  EV_BOSS_KILLED,
  EV_BOSS_RAGE_WARN,
  EV_PICKUP_GEM,
  EV_PICKUP_LIFE,
  EV_CHECKPOINT,
  EV_LAVA_WARN,
  EV_LAVA_IGNITE,
  EV_GAME_OVER,       // value = score, pos = furthest level
  EV_NEW_HIGHSCORE,   // value = score
  EV_CAMPAIGN_DONE,   // value = score
  EV_ATTRACT,         // entered screensaver
  EV_STAGE,           // kind = new stage
  EV_COUNT
};

struct Event {
  uint8_t type;
  uint8_t kind;
  int16_t pos;
  int32_t value;
};

struct Input {
  float tilt = 0;       // -90..90 after deadzone; + moves toward the exit
  bool attack = false;  // wobble above the attack threshold
  bool active = false;  // any meaningful input (resets the idle timer)
};

struct GameOptions {
  uint8_t livesPerLevel = 3;
  bool livesPerRun = false;     // false = original rules: lives refill every level
  uint16_t idleTimeoutSec = 30;
  bool juice = true;            // hit-stop, kill bursts, flashes
  bool smooth = true;           // sub-pixel motion for the player
  uint8_t palette = 0;          // 0 classic, 1 colour-blind friendly
  uint8_t difficulty = 1;       // 0 easy, 1 normal, 2 hard
  bool gravity = false;         // death particles fall toward the start (strip going up a wall)
  int16_t bendPoint = 550;
  uint8_t conveyorBrightness = 8;  // APA102 defaults; NeoPixels need ~40 / ~15
  uint8_t lavaOffBrightness = 4;
  bool attract = true;
  bool telegraph = true;        // lava flickers before igniting, spawners glow before spawning
};

struct AudioState {
  float danger;     // 0..1, how close the nearest threat is
  float lavaNear;   // 0..1, active lava proximity
  float speed;      // 0..1, player speed
  float tension;    // 0..1, level base tension
  float conveyor;   // -1..1, conveyor push under the player
  uint8_t stage;
  uint8_t music;    // resolved MusicTrack or MUSIC_NONE
  bool attacking;
  bool boss;
  bool dark;
  bool paused;
};

struct GameProbe;  // test harness access

class Game {
  friend struct GameProbe;

 public:
  GameOptions opt;
  int32_t highScore = 0;  // host keeps this up to date for the score readout

  void init(const Campaign* campaign, uint32_t seed);
  void setCampaign(const Campaign* campaign);  // keeps the current stage unless the level vanished
  const Campaign* campaign() const { return campaign_; }

  void boot();                                  // power-on animation, then default run
  void startRun(Mode m, int levelIndex = -1);   // -1 = first enabled level
  void testLevel(const LevelDef& L, bool spectate);
  void goAttract();
  void setPaused(bool p) { paused_ = p; }
  bool paused() const { return paused_; }
  void setDefaultMode(Mode m) { defaultMode_ = m == MODE_TEST ? MODE_CAMPAIGN : m; }
  void setSpectate(bool s) { spectate_ = s; }
  void skipLevel();

  void tick(const Input& in);
  void render(RGB* fb, int n);
  bool pollEvent(Event& e);

  // ---- state for UI / sound / stats
  Stage stage() const { return stage_; }
  Mode mode() const { return mode_; }
  int levelIndex() const { return levelIndex_; }  // campaign index, or endless depth
  int lives() const { return lives_; }
  int32_t score() const { return score_; }
  int combo() const { return combo_; }
  int playerPos() const { return (int)ppos_; }
  uint32_t clock() const { return clock_; }
  uint32_t levelTime() const { return clock_ - levelStart_; }
  const LevelDef& level() const { return L_; }
  bool bossAlive() const { return boss_.alive; }
  int bossHp() const { return boss_.alive ? boss_.lives : 0; }
  int enemiesAlive() const;
  AudioState audio() const;
  int furthestLevel() const { return furthest_; }

 private:
  struct Enemy {
    int32_t pos16;  // position in 1/16 world units
    int16_t origin;
    int16_t wobble;
    int16_t speed16;
    int8_t dir;
    int8_t playerSide;
    uint8_t kind;
    uint8_t alive;
    uint8_t hp;
    uint8_t immune;  // attack id that already hit it
    uint8_t flash;
    uint16_t phase;  // phantom cycle offset
    uint32_t stunUntil;  // armored enemies reel after the first twang
    int pos() const { return pos16 >> 4; }
    bool solid(uint32_t t) const;
  };
  struct Spawner {
    SpawnerDef d;
    uint32_t activateAt;
    uint32_t lastSpawned;
    bool alive;
    bool fired;
  };
  struct Lava {
    LavaDef d;
    float left, right;
    float growth, flow;
    int width;
    uint32_t lastToggle;
    bool on;
    bool alive;
    bool warned;
  };
  struct Particle {
    int pos;
    int sp;
    int power;
    int life;
    uint8_t alive;
    uint8_t type;  // 0 death, 1 enemy burst, 2 sparkle
    RGB color;
  };
  struct Boss {
    bool alive;
    int pos;
    int lives;
    int maxLives;
    uint8_t immune;
    uint8_t flash;
    uint8_t rageState;  // 0 idle, 1 warning, 2 erupting
    uint32_t rageAt;
  };

  static const int ENEMY_POOL = 32;
  static const int PARTICLE_POOL = 128;
  static const int SPAWNER_POOL = MAX_SPAWNERS + 2;  // +2 reserved for the boss
  static const int EVENT_QUEUE = 48;

  void loadLevel(bool retry);
  void beginLevelIntro(bool retry);
  void nextLevel();
  void levelComplete();
  void die();
  void gameOver();
  void setStage(Stage s);
  bool resolveLevel(int index, LevelDef& out);
  int firstEnabled() const;
  int nextEnabled(int from) const;
  int livesForLevel() const;

  void spawnEnemy(int pos, int dir, int speed, int wobble, uint8_t kind);
  void spawnParticles(int pos, int count, uint8_t type, RGB color);
  void killEnemy(Enemy& e, bool byAttack);
  void spawnBossMinions();

  void updatePlay(const Input& in);
  void updateEnemies();
  void updateLava();
  void updateSpawners();
  void updateBoss();
  void updatePickups(int prevPos);
  bool tickParticles();
  bool inLava(int pos) const;
  bool inZone(int pos, uint8_t type) const;
  int conveyorPush(int pos) const;

  void emit(uint8_t type, uint8_t kind = 0, int pos = 0, int32_t value = 0);

  // ---- rendering
  int led(int pos) const { return clampi((int)mapl(pos, 0, WORLD, 0, n_ - 1), 0, n_ - 1); }
  void put(int i, RGB c) { if (i >= 0 && i < n_) fb_[i] = c; }
  void add(int i, RGB c) { if (i >= 0 && i < n_) fb_[i] = addc(fb_[i], c); }
  void plotSmooth(int pos, RGB c);
  void clearFb();
  void renderBoot();
  void renderIntro();
  void renderPlay();
  void renderDead();
  void renderWin();
  void renderBossKilled();
  void renderGameover();
  void renderScore();
  void renderAttract();
  void applyDarkness();

  const Campaign* campaign_ = nullptr;
  LevelDef L_;
  Rng rng_;
  Rng fxRng_;  // render-only randomness (doesn't affect gameplay determinism)

  Stage stage_ = ST_BOOT;
  Mode mode_ = MODE_CAMPAIGN;
  Mode defaultMode_ = MODE_CAMPAIGN;
  bool paused_ = false;
  bool spectate_ = false;
  bool testValid_ = false;
  bool introRetry_ = false;
  LevelDef testLevel_;
  uint32_t runSeed_ = 1;

  uint32_t clock_ = 0;
  uint32_t clockFrac_ = 0;
  uint32_t realTicks_ = 0;
  uint32_t stageStart_ = 0;
  uint32_t levelStart_ = 0;
  uint32_t lastInput_ = 0;

  int levelIndex_ = 0;
  int furthest_ = 0;
  int lives_ = 3;
  int32_t score_ = 0;
  int32_t scoreAtGameOver_ = 0;
  bool newHigh_ = false;
  bool finalVictory_ = false;
  int combo_ = 0;
  uint32_t lastKill_ = 0;

  float ppos_ = 0;
  float pvel_ = 0;
  int conveyorMod_ = 0;
  int respawnPos_ = -1;
  int deathPos_ = 0;
  bool attacking_ = false;
  uint32_t attackStart_ = 0;
  uint8_t attackId_ = 0;
  int attackWidth_ = 70;

  int freeze_ = 0;      // hit-stop ticks remaining
  int flashPos_ = -1;
  int flashTicks_ = 0;
  RGB screenTint_ = {0, 0, 0};
  int screenTintTicks_ = 0;

  Enemy enemies_[ENEMY_POOL];
  Particle particles_[PARTICLE_POOL];
  Spawner spawners_[SPAWNER_POOL];
  Lava lava_[MAX_LAVA];
  bool pickupAlive_[MAX_PICKUPS];
  bool checkpointOn_[MAX_CHECKPOINTS];
  Boss boss_;

  Event events_[EVENT_QUEUE];
  int evHead_ = 0, evTail_ = 0;

  RGB* fb_ = nullptr;
  int n_ = 0;
  uint8_t fireHeat_[WORLD];
  uint8_t hue_ = 0;
};

// Endless-mode level generator (tw_gen.cpp)
void generateLevel(int depth, uint32_t seed, LevelDef& out);

}  // namespace tw
