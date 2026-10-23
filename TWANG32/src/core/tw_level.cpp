#include "tw_level.h"
#include "tw_json.h"

namespace tw {

static const char* const ENEMY_NAMES[EK_COUNT] = {"basic", "armored", "chaser", "splitter", "phantom", "mini"};
static const char* const MUSIC_NAMES[MUSIC_TRACK_COUNT] = {"descent", "furnace", "reckoning", "abyss"};

const char* enemyKindName(uint8_t k) { return k < EK_COUNT ? ENEMY_NAMES[k] : "basic"; }
const char* musicName(uint8_t m) {
  if (m == MUSIC_AUTO) return "auto";
  if (m == MUSIC_NONE) return "none";
  return m < MUSIC_TRACK_COUNT ? MUSIC_NAMES[m] : "auto";
}

void LevelDef::clear() {
  memzero(this, sizeof(LevelDef));
  flags = LF_ENABLED;
  attackWidth = 70;
  music = MUSIC_AUTO;
  tension = 30;
}

bool LevelDef::addEnemy(int pos, int dir, int speed, int wobble, uint8_t kind) {
  if (nEnemies >= MAX_LEVEL_ENEMIES) return false;
  EnemyDef& e = enemies[nEnemies++];
  e.pos = (int16_t)clampi(pos, 0, WORLD);
  e.dir = dir > 0 ? 1 : -1;
  e.speed = (int16_t)clampi(speed, 0, 20);
  e.wobble = (int16_t)clampi(wobble, 0, WORLD);
  e.kind = kind < EK_COUNT ? kind : (uint8_t)EK_BASIC;
  return true;
}

bool LevelDef::addSpawner(int pos, int rate, int speed, int dir, int delay, uint8_t kind) {
  if (nSpawners >= MAX_SPAWNERS) return false;
  SpawnerDef& s = spawners[nSpawners++];
  s.pos = (int16_t)clampi(pos, 0, WORLD);
  s.rate = (uint16_t)clampi(rate, 250, 60000);
  s.speed = (int16_t)clampi(speed, 0, 20);
  s.dir = dir > 0 ? 1 : -1;
  s.delay = (uint16_t)clampi(delay, 0, 60000);
  s.kind = kind < EK_COUNT ? kind : (uint8_t)EK_BASIC;
  return true;
}

bool LevelDef::addLava(int left, int right, int on, int off, int offset, bool startOn, float grow, float flow) {
  if (nLava >= MAX_LAVA) return false;
  LavaDef& l = lava[nLava++];
  if (left > right) { int t = left; left = right; right = t; }
  l.left = (int16_t)clampi(left, 0, WORLD);
  l.right = (int16_t)clampi(right, 0, WORLD);
  l.on = (uint16_t)clampi(on, 0, 60000);
  l.off = (uint16_t)clampi(off, 0, 60000);
  l.offset = (uint16_t)clampi(offset, 0, 60000);
  l.startOn = startOn ? 1 : 0;
  l.grow = clampf(absf(grow), 0.0f, 2.0f);
  l.flow = clampf(flow, -3.0f, 3.0f);
  return true;
}

bool LevelDef::addConveyor(int start, int end, int speed) {
  if (nConveyors >= MAX_CONVEYORS) return false;
  ConveyorDef& c = conveyors[nConveyors++];
  if (start > end) { int t = start; start = end; end = t; }
  c.start = (int16_t)clampi(start, 0, WORLD);
  c.end = (int16_t)clampi(end, 0, WORLD);
  c.speed = (int8_t)clampi(speed, -9, 9);  // must leave the player some headway
  return true;
}

bool LevelDef::addZone(int start, int end, uint8_t type) {
  if (nZones >= MAX_ZONES) return false;
  ZoneDef& z = zones[nZones++];
  if (start > end) { int t = start; start = end; end = t; }
  z.start = (int16_t)clampi(start, 0, WORLD);
  z.end = (int16_t)clampi(end, 0, WORLD);
  z.type = type < ZONE_COUNT ? type : (uint8_t)ZONE_ICE;
  return true;
}

bool LevelDef::addPickup(int pos, uint8_t type) {
  if (nPickups >= MAX_PICKUPS) return false;
  pickups[nPickups].pos = (int16_t)clampi(pos, 0, WORLD - 1);
  pickups[nPickups].type = type < PK_COUNT ? type : (uint8_t)PK_GEM;
  nPickups++;
  return true;
}

bool LevelDef::addCheckpoint(int pos) {
  if (nCheckpoints >= MAX_CHECKPOINTS) return false;
  checkpoints[nCheckpoints++] = (int16_t)clampi(pos, 1, WORLD - 1);
  return true;
}

static const int16_t CLASSIC_POS[MAX_BOSS_HP] = {800, 200, 600, 400, 900, 300};
static const uint16_t CLASSIC_RATE[MAX_BOSS_HP] = {1800, 1600, 1000, 900, 800, 700};

void LevelDef::setBoss(int hp, const int* positions, const int* rates, int speed, int width, bool rage) {
  boss.present = 1;
  boss.hp = (uint8_t)clampi(hp, 1, MAX_BOSS_HP);
  for (int i = 0; i < MAX_BOSS_HP; i++) {
    boss.positions[i] = (int16_t)clampi(positions ? positions[i] : CLASSIC_POS[i], 60, WORLD - 60);
    boss.rates[i] = (uint16_t)clampi(rates ? rates[i] : CLASSIC_RATE[i], 300, 60000);
  }
  boss.speed = (uint8_t)clampi(speed, 0, 12);
  boss.width = (int16_t)clampi(width, 10, 200);
  boss.rage = rage ? 1 : 0;
}

void LevelDef::setClassicBoss() { setBoss(3, nullptr, nullptr, 3, 40, false); }

uint8_t LevelDef::resolvedMusic() const {
  if (music != MUSIC_AUTO) return music;
  if (boss.present) return MUSIC_RECKONING;
  for (int i = 0; i < nZones; i++) if (zones[i].type == ZONE_DARK) return MUSIC_ABYSS;
  if (nLava >= 2) return MUSIC_FURNACE;
  return MUSIC_DESCENT;
}

// ------------------------------------------------------------------ parsing

struct Ctx {
  JsonReader& r;
  int warnings;
};

static uint8_t lookup(const char* s, const char* const* names, int n, uint8_t fallback) {
  for (int i = 0; i < n; i++) if (streq(s, names[i])) return (uint8_t)i;
  return fallback;
}

static uint8_t readKind(Ctx& c) {
  if (c.r.peek() == JsonReader::T_STRING) {
    char s[16];
    c.r.readString(s, sizeof(s));
    uint8_t k = lookup(s, ENEMY_NAMES, EK_COUNT, 255);
    if (k == 255) { c.warnings++; return EK_BASIC; }
    return k;
  }
  return (uint8_t)clampi(c.r.asInt(0), 0, EK_COUNT - 1);
}

static int readDir(Ctx& c) {
  if (c.r.peek() == JsonReader::T_STRING) {
    char s[12];
    c.r.readString(s, sizeof(s));
    return (streq(s, "exit") || streq(s, "away") || streq(s, "+")) ? 1 : -1;
  }
  return c.r.asInt(-1) > 0 ? 1 : -1;
}

static void parseEnemies(Ctx& c, LevelDef& L) {
  if (!c.r.beginArray()) return;
  while (c.r.nextItem()) {
    int pos = 500, dir = -1, speed = 0, wobble = 0;
    uint8_t kind = EK_BASIC;
    char k[16];
    if (!c.r.beginObject()) return;
    while (c.r.nextKey(k, sizeof(k))) {
      if (streq(k, "pos")) pos = c.r.asInt(pos);
      else if (streq(k, "dir")) dir = readDir(c);
      else if (streq(k, "speed")) speed = c.r.asInt(speed);
      else if (streq(k, "wobble")) wobble = c.r.asInt(wobble);
      else if (streq(k, "kind")) kind = readKind(c);
      else c.r.skip();
    }
    if (!L.addEnemy(pos, dir, speed, wobble, kind)) c.warnings++;
  }
}

static void parseSpawners(Ctx& c, LevelDef& L) {
  if (!c.r.beginArray()) return;
  while (c.r.nextItem()) {
    int pos = 1000, rate = 3000, speed = 2, dir = -1, delay = 0;
    uint8_t kind = EK_BASIC;
    char k[16];
    if (!c.r.beginObject()) return;
    while (c.r.nextKey(k, sizeof(k))) {
      if (streq(k, "pos")) pos = c.r.asInt(pos);
      else if (streq(k, "rate")) rate = c.r.asInt(rate);
      else if (streq(k, "speed")) speed = c.r.asInt(speed);
      else if (streq(k, "dir")) dir = readDir(c);
      else if (streq(k, "delay")) delay = c.r.asInt(delay);
      else if (streq(k, "kind")) kind = readKind(c);
      else c.r.skip();
    }
    if (!L.addSpawner(pos, rate, speed, dir, delay, kind)) c.warnings++;
  }
}

static void parseLava(Ctx& c, LevelDef& L) {
  if (!c.r.beginArray()) return;
  while (c.r.nextItem()) {
    int left = 400, right = 500, on = 2000, off = 2000, offset = 0;
    bool startOn = false;
    float grow = 0, flow = 0;
    char k[16];
    if (!c.r.beginObject()) return;
    while (c.r.nextKey(k, sizeof(k))) {
      if (streq(k, "left")) left = c.r.asInt(left);
      else if (streq(k, "right")) right = c.r.asInt(right);
      else if (streq(k, "on")) on = c.r.asInt(on);
      else if (streq(k, "off")) off = c.r.asInt(off);
      else if (streq(k, "offset")) offset = c.r.asInt(offset);
      else if (streq(k, "startOn")) startOn = c.r.asBool(startOn);
      else if (streq(k, "grow")) grow = c.r.asFloat(grow);
      else if (streq(k, "flow")) flow = c.r.asFloat(flow);
      else c.r.skip();
    }
    if (!L.addLava(left, right, on, off, offset, startOn, grow, flow)) c.warnings++;
  }
}

static void parseConveyors(Ctx& c, LevelDef& L) {
  if (!c.r.beginArray()) return;
  while (c.r.nextItem()) {
    int start = 100, end = 600, speed = -4;
    char k[16];
    if (!c.r.beginObject()) return;
    while (c.r.nextKey(k, sizeof(k))) {
      if (streq(k, "start")) start = c.r.asInt(start);
      else if (streq(k, "end")) end = c.r.asInt(end);
      else if (streq(k, "speed")) speed = c.r.asInt(speed);
      else c.r.skip();
    }
    if (speed < -9 || speed > 9) c.warnings++;
    if (!L.addConveyor(start, end, speed)) c.warnings++;
  }
}

static void parseZones(Ctx& c, LevelDef& L) {
  if (!c.r.beginArray()) return;
  while (c.r.nextItem()) {
    int start = 300, end = 600;
    uint8_t type = ZONE_ICE;
    char k[16];
    if (!c.r.beginObject()) return;
    while (c.r.nextKey(k, sizeof(k))) {
      if (streq(k, "start")) start = c.r.asInt(start);
      else if (streq(k, "end")) end = c.r.asInt(end);
      else if (streq(k, "type")) {
        char s[12];
        if (c.r.peek() == JsonReader::T_STRING) { c.r.readString(s, sizeof(s)); type = streq(s, "dark") ? ZONE_DARK : ZONE_ICE; }
        else type = (uint8_t)clampi(c.r.asInt(0), 0, ZONE_COUNT - 1);
      } else c.r.skip();
    }
    if (!L.addZone(start, end, type)) c.warnings++;
  }
}

static void parsePickups(Ctx& c, LevelDef& L) {
  if (!c.r.beginArray()) return;
  while (c.r.nextItem()) {
    int pos = 500;
    uint8_t type = PK_GEM;
    char k[16];
    if (!c.r.beginObject()) return;
    while (c.r.nextKey(k, sizeof(k))) {
      if (streq(k, "pos")) pos = c.r.asInt(pos);
      else if (streq(k, "type")) {
        char s[12];
        if (c.r.peek() == JsonReader::T_STRING) { c.r.readString(s, sizeof(s)); type = streq(s, "life") ? PK_LIFE : PK_GEM; }
        else type = (uint8_t)clampi(c.r.asInt(0), 0, PK_COUNT - 1);
      } else c.r.skip();
    }
    if (!L.addPickup(pos, type)) c.warnings++;
  }
}

static void parseCheckpoints(Ctx& c, LevelDef& L) {
  if (!c.r.beginArray()) return;
  while (c.r.nextItem()) {
    int p = c.r.asInt(-1);
    if (p < 0) continue;
    if (!L.addCheckpoint(p)) c.warnings++;
  }
}

static int readIntArray(Ctx& c, int* out, int cap, const int* defaults) {
  for (int i = 0; i < cap; i++) out[i] = defaults[i];
  if (!c.r.beginArray()) return 0;
  int n = 0;
  while (c.r.nextItem()) {
    int v = c.r.asInt(0);
    if (n < cap) out[n++] = v;
    else c.warnings++;
  }
  return n;
}

static void parseBoss(Ctx& c, LevelDef& L) {
  JsonReader::Type t = c.r.peek();
  if (t == JsonReader::T_NULL) { c.r.skip(); return; }
  if (t == JsonReader::T_BOOL) {
    if (c.r.asBool(false)) L.setClassicBoss();
    return;
  }
  int hp = 3, speed = 3, width = 40;
  bool rage = false;
  int defPos[MAX_BOSS_HP], defRate[MAX_BOSS_HP];
  for (int i = 0; i < MAX_BOSS_HP; i++) { defPos[i] = CLASSIC_POS[i]; defRate[i] = CLASSIC_RATE[i]; }
  int pos[MAX_BOSS_HP], rates[MAX_BOSS_HP];
  for (int i = 0; i < MAX_BOSS_HP; i++) { pos[i] = defPos[i]; rates[i] = defRate[i]; }
  char k[16];
  if (!c.r.beginObject()) return;
  while (c.r.nextKey(k, sizeof(k))) {
    if (streq(k, "hp")) hp = c.r.asInt(hp);
    else if (streq(k, "speed")) speed = c.r.asInt(speed);
    else if (streq(k, "width")) width = c.r.asInt(width);
    else if (streq(k, "rage")) rage = c.r.asBool(rage);
    else if (streq(k, "positions")) readIntArray(c, pos, MAX_BOSS_HP, defPos);
    else if (streq(k, "rates")) readIntArray(c, rates, MAX_BOSS_HP, defRate);
    else c.r.skip();
  }
  L.setBoss(hp, pos, rates, speed, width, rage);
}

static uint8_t readMusic(Ctx& c) {
  if (c.r.peek() == JsonReader::T_STRING) {
    char s[16];
    c.r.readString(s, sizeof(s));
    if (streq(s, "auto")) return MUSIC_AUTO;
    if (streq(s, "none")) return MUSIC_NONE;
    uint8_t m = lookup(s, MUSIC_NAMES, MUSIC_TRACK_COUNT, MUSIC_AUTO);
    if (m == MUSIC_AUTO) c.warnings++;
    return m;
  }
  int v = c.r.asInt(MUSIC_AUTO);
  return v >= 0 && v < MUSIC_TRACK_COUNT ? (uint8_t)v : (uint8_t)MUSIC_AUTO;
}

static bool parseLevelObject(Ctx& c, LevelDef& L) {
  L.clear();
  char k[16];
  if (!c.r.beginObject()) return false;
  while (c.r.nextKey(k, sizeof(k))) {
    if (streq(k, "id")) c.r.readString(L.id, sizeof(L.id));
    else if (streq(k, "name")) c.r.readString(L.name, sizeof(L.name));
    else if (streq(k, "enabled")) { if (c.r.asBool(true)) L.flags |= LF_ENABLED; else L.flags &= ~LF_ENABLED; }
    else if (streq(k, "safe")) { if (c.r.asBool(false)) L.flags |= LF_SAFE; else L.flags &= ~LF_SAFE; }
    else if (streq(k, "player")) L.playerStart = (int16_t)clampi(c.r.asInt(0), 0, WORLD - 1);
    else if (streq(k, "attackWidth")) L.attackWidth = (int16_t)clampi(c.r.asInt(70), 10, 300);
    else if (streq(k, "music")) L.music = readMusic(c);
    else if (streq(k, "tension")) L.tension = (uint8_t)clampi(c.r.asInt(30), 0, 100);
    else if (streq(k, "par")) L.par = (uint16_t)clampi(c.r.asInt(0), 0, 3600);
    else if (streq(k, "enemies")) parseEnemies(c, L);
    else if (streq(k, "spawners")) parseSpawners(c, L);
    else if (streq(k, "lava")) parseLava(c, L);
    else if (streq(k, "conveyors")) parseConveyors(c, L);
    else if (streq(k, "zones")) parseZones(c, L);
    else if (streq(k, "pickups")) parsePickups(c, L);
    else if (streq(k, "checkpoints")) parseCheckpoints(c, L);
    else if (streq(k, "boss")) parseBoss(c, L);
    else c.r.skip();
  }
  return c.r.ok();
}

static void describeError(JsonReader& r, const char* json, size_t len, ParseResult& res, int levelIndex) {
  // report line number - much friendlier than a byte offset
  size_t p = r.errorPos();
  int line = 1;
  for (size_t i = 0; i < p && i < len; i++) if (json[i] == '\n') line++;
  char buf[96];
  const char* e = r.error();
  // tiny formatter (no printf in the core)
  int o = 0;
  auto put = [&](const char* s) { while (*s && o < 90) buf[o++] = *s++; };
  auto putn = [&](int v) {
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < 11) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0 && o < 90) buf[o++] = t[--n];
  };
  put("JSON error: "); put(e); put(" (line "); putn(line); put(")");
  if (levelIndex >= 0) { put(" in level "); putn(levelIndex + 1); }
  buf[o] = 0;
  strcopy(res.message, buf, sizeof(res.message));
}

ParseResult parseLevel(const char* json, size_t len, LevelDef& out) {
  ParseResult res;
  memzero(&res, sizeof(res));
  JsonReader r(json, len);
  Ctx c{r, 0};
  parseLevelObject(c, out);
  if (!r.ok()) { describeError(r, json, len, res, -1); return res; }
  res.ok = true;
  res.levels = 1;
  res.warnings = c.warnings;
  strcopy(res.message, "ok", sizeof(res.message));
  return res;
}

ParseResult parseCampaign(const char* json, size_t len, Campaign& out) {
  ParseResult res;
  memzero(&res, sizeof(res));
  out.count = 0;
  strcopy(out.name, "Campaign", sizeof(out.name));
  JsonReader r(json, len);
  Ctx c{r, 0};
  char k[16];
  bool sawLevels = false;
  if (r.beginObject()) {
    while (r.nextKey(k, sizeof(k))) {
      if (streq(k, "name")) r.readString(out.name, sizeof(out.name));
      else if (streq(k, "levels")) {
        sawLevels = true;
        if (!r.beginArray()) break;
        while (r.nextItem()) {
          if (out.count >= MAX_CAMPAIGN_LEVELS) { c.warnings++; r.skip(); continue; }
          if (!parseLevelObject(c, out.levels[out.count])) break;
          out.count++;
        }
      } else r.skip();
    }
  }
  if (!r.ok()) { describeError(r, json, len, res, out.count); out.count = 0; return res; }
  if (!sawLevels || out.count == 0) { strcopy(res.message, "campaign has no levels", sizeof(res.message)); return res; }
  res.ok = true;
  res.levels = out.count;
  res.warnings = c.warnings;
  strcopy(res.message, "ok", sizeof(res.message));
  return res;
}

}  // namespace tw
