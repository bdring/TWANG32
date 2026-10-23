/*
  TWANG Next - level data model.

  Levels are plain data. The firmware stores the campaign as JSON on LittleFS,
  the browser editor edits the same JSON, and this file turns it into
  fixed-size LevelDef structs that the engine plays. See docs/LEVELS.md.
*/
#pragma once
#include "tw_base.h"

namespace tw {

constexpr int MAX_LEVEL_ENEMIES = 24;
constexpr int MAX_SPAWNERS = 4;
constexpr int MAX_LAVA = 8;
constexpr int MAX_CONVEYORS = 4;
constexpr int MAX_ZONES = 4;
constexpr int MAX_PICKUPS = 12;
constexpr int MAX_CHECKPOINTS = 4;
constexpr int MAX_BOSS_HP = 6;
constexpr int MAX_CAMPAIGN_LEVELS = 48;

enum EnemyKind : uint8_t {
  EK_BASIC = 0,   // red. moves or sits still; wobble > 0 makes it swing on a sine path
  EK_ARMORED,     // magenta. takes two twangs, gets knocked back by the first
  EK_CHASER,      // pulsing orange. homes in on the player
  EK_SPLITTER,    // yellow. bursts into two small fast enemies further away
  EK_PHANTOM,     // violet. fades in and out; harmless and untouchable while faded
  EK_MINI,        // internal: the fragments a splitter leaves behind
  EK_COUNT
};

enum ZoneType : uint8_t { ZONE_ICE = 0, ZONE_DARK = 1, ZONE_COUNT };
enum PickupType : uint8_t { PK_GEM = 0, PK_LIFE = 1, PK_COUNT };

enum MusicTrack : uint8_t {
  MUSIC_DESCENT = 0,
  MUSIC_FURNACE = 1,
  MUSIC_RECKONING = 2,
  MUSIC_ABYSS = 3,
  MUSIC_TRACK_COUNT = 4,
  MUSIC_AUTO = 254,
  MUSIC_NONE = 255
};

enum LevelFlags : uint8_t {
  LF_ENABLED = 1,
  LF_SAFE = 2,  // no lives lost and no score (the original level 0 behaviour)
};

struct EnemyDef {
  int16_t pos;
  int8_t dir;  // -1 toward the start, +1 toward the exit
  uint8_t kind;
  int16_t speed;
  int16_t wobble;  // 0 = straight line, >0 = sine swing width (speed then sets swing rate)
};

struct SpawnerDef {
  int16_t pos;
  int8_t dir;
  uint8_t kind;
  int16_t speed;
  uint16_t rate;   // ms between spawns
  uint16_t delay;  // ms before the first spawn
};

struct LavaDef {
  int16_t left, right;
  uint16_t on, off, offset;  // ms
  uint8_t startOn;
  float grow;  // per-frame growth (0.1 - 0.5 is reasonable)
  float flow;  // per-frame drift, +/- (0.2 - 0.8 is reasonable)
};

struct ConveyorDef {
  int16_t start, end;
  int8_t speed;  // + pushes toward the exit, - toward the start
};

struct ZoneDef {
  int16_t start, end;
  uint8_t type;
};

struct PickupDef {
  int16_t pos;
  uint8_t type;
};

struct BossDef {
  uint8_t present;
  uint8_t hp;
  uint8_t rage;   // final phase erupts lava around itself (telegraphed)
  uint8_t speed;  // speed of the minions it spawns
  int16_t width;
  int16_t positions[MAX_BOSS_HP];  // where it stands for each remaining hit
  uint16_t rates[MAX_BOSS_HP];     // minion spawn interval per phase (ms)
};

struct LevelDef {
  char id[24];
  char name[32];
  uint8_t flags;
  int16_t playerStart;
  int16_t attackWidth;
  uint8_t music;    // MusicTrack, MUSIC_AUTO or MUSIC_NONE
  uint8_t tension;  // 0-100, base intensity of the adaptive music
  uint16_t par;     // seconds; clearing faster earns a time bonus (0 = none)

  uint8_t nEnemies, nSpawners, nLava, nConveyors, nZones, nPickups, nCheckpoints;
  EnemyDef enemies[MAX_LEVEL_ENEMIES];
  SpawnerDef spawners[MAX_SPAWNERS];
  LavaDef lava[MAX_LAVA];
  ConveyorDef conveyors[MAX_CONVEYORS];
  ZoneDef zones[MAX_ZONES];
  PickupDef pickups[MAX_PICKUPS];
  int16_t checkpoints[MAX_CHECKPOINTS];
  BossDef boss;

  void clear();
  bool enabled() const { return flags & LF_ENABLED; }
  bool safe() const { return flags & LF_SAFE; }

  // builder helpers (used by the endless generator and the tests)
  bool addEnemy(int pos, int dir, int speed, int wobble, uint8_t kind = EK_BASIC);
  bool addSpawner(int pos, int rate, int speed, int dir, int delay, uint8_t kind = EK_BASIC);
  bool addLava(int left, int right, int on, int off, int offset, bool startOn, float grow, float flow);
  bool addConveyor(int start, int end, int speed);
  bool addZone(int start, int end, uint8_t type);
  bool addPickup(int pos, uint8_t type);
  bool addCheckpoint(int pos);
  void setBoss(int hp, const int* positions, const int* rates, int speed, int width, bool rage);
  void setClassicBoss();
  uint8_t resolvedMusic() const;
};

struct Campaign {
  char name[32];
  int count;
  LevelDef levels[MAX_CAMPAIGN_LEVELS];
};

// Result of parsing; message is human readable and says where things went wrong.
struct ParseResult {
  bool ok;
  int levels;
  int warnings;
  char message[96];
};

// Parse a whole campaign document: {"name":..., "levels":[...]}
ParseResult parseCampaign(const char* json, size_t len, Campaign& out);
// Parse a single level object (used by the editor's "test on strip").
ParseResult parseLevel(const char* json, size_t len, LevelDef& out);

const char* enemyKindName(uint8_t k);
const char* musicName(uint8_t m);

}  // namespace tw
