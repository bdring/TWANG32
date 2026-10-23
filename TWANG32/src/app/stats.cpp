#include "stats.h"
#include <LittleFS.h>
#include "json_out.h"
#include "../core/tw_json.h"

namespace stats {

struct LevelStat {
  char id[24];
  uint16_t plays, clears, deaths;
  uint32_t best;
  uint16_t heat[20];
};
struct Score {
  char name[13];
  int32_t score;
  uint16_t level;
  uint8_t mode;
};

static const int MAX_LEVEL_STATS = 64;
static const int MAX_SCORES = 10;

static uint32_t s_games, s_total, s_boss, s_wins;
static Score s_scores[MAX_SCORES];
static int s_nScores;
static LevelStat s_levels[MAX_LEVEL_STATS];
static int s_nLevels;
static int s_pending = -1;
static uint32_t s_pendingAt;
static bool s_dirty;
static bool s_urgent;  // write on the next flush (a finished game, a new name)
static uint32_t s_dirtyAt;
static SemaphoreHandle_t s_lock;

struct Lock {
  Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_lock); }
};

static LevelStat* level(const char* id, bool create) {
  if (!id || !id[0]) return nullptr;
  for (int i = 0; i < s_nLevels; i++) if (!strcmp(s_levels[i].id, id)) return &s_levels[i];
  if (!create || s_nLevels >= MAX_LEVEL_STATS) return nullptr;
  LevelStat& L = s_levels[s_nLevels++];
  memset(&L, 0, sizeof L);
  strlcpy(L.id, id, sizeof L.id);
  return &L;
}

static void markDirty() {
  if (!s_dirty) s_dirtyAt = millis();
  s_dirty = true;
}

void begin() {
  s_lock = xSemaphoreCreateMutex();
  File f = LittleFS.open("/stats.json", "r");
  if (!f) return;
  size_t n = f.size();
  char* buf = (char*)malloc(n + 1);
  if (buf) {
    f.readBytes(buf, n);
    buf[n] = 0;
    fromJson(buf, n);
    free(buf);
  }
  f.close();
  s_dirty = false;
}

int32_t highScore() { return s_nScores ? s_scores[0].score : 0; }

void onEvent(const tw::Event& e, const tw::Game& g, const tw::Campaign& c) {
  using namespace tw;
  Mode m = g.mode();
  const char* id = nullptr;
  if ((m == MODE_CAMPAIGN || m == MODE_PRACTICE) && g.levelIndex() < c.count) id = c.levels[g.levelIndex()].id;
  Lock lock;
  switch (e.type) {
    case EV_LEVEL_START:
      if (!e.kind && id) { LevelStat* L = level(id, true); if (L) L->plays++; markDirty(); }
      break;
    case EV_DIE:
      if (id) {
        LevelStat* L = level(id, true);
        if (L) { L->deaths++; L->heat[constrain(e.pos / 50, 0, 19)]++; }
        markDirty();
      }
      break;
    case EV_LEVEL_CLEAR:
      if (id) {
        LevelStat* L = level(id, true);
        if (L) { L->clears++; if (!L->best || (uint32_t)e.value < L->best) L->best = (uint32_t)e.value; }
        markDirty();
      }
      break;
    case EV_BOSS_KILLED: s_boss++; markDirty(); break;
    case EV_CAMPAIGN_DONE: s_wins++; markDirty(); break;
    case EV_GAME_OVER: {
      if (m == MODE_TEST || m == MODE_PRACTICE) break;
      s_games++;
      s_total += (uint32_t)max<int32_t>(0, e.value);
      if (e.value > 0 && (s_nScores < MAX_SCORES || e.value > s_scores[s_nScores - 1].score)) {
        int at = s_nScores < MAX_SCORES ? s_nScores++ : MAX_SCORES - 1;
        while (at > 0 && s_scores[at - 1].score < e.value) { s_scores[at] = s_scores[at - 1]; at--; }
        Score& S = s_scores[at];
        strlcpy(S.name, "???", sizeof S.name);
        S.score = e.value;
        S.level = (uint16_t)(e.pos + 1);
        S.mode = (uint8_t)m;
        s_pending = at;
        s_pendingAt = millis();
      }
      markDirty();
      s_urgent = true;  // a finished game is worth a flash write (done by the main loop)
      break;
    }
    default: break;
  }
}

bool setPendingName(const char* name) {
  Lock lock;
  if (s_pending < 0 || millis() - s_pendingAt > 5UL * 60 * 1000) return false;
  strlcpy(s_scores[s_pending].name, name && name[0] ? name : "???", sizeof s_scores[0].name);
  s_pending = -1;
  markDirty();
  s_urgent = true;
  return true;
}

String toJson() {
  Lock lock;
  JsonOut j(1024 + s_nLevels * 140);
  j.obj().kv("games", (unsigned long)s_games).kv("totalScore", (unsigned long)s_total).kv("bossKills", (unsigned long)s_boss).kv("wins", (unsigned long)s_wins);
  j.key("scores").arr();
  for (int i = 0; i < s_nScores; i++)
    j.obj().kv("name", s_scores[i].name).kv("score", (long)s_scores[i].score).kv("level", s_scores[i].level).kv("mode", s_scores[i].mode).end();
  j.endArr();
  j.key("levels").obj();
  for (int i = 0; i < s_nLevels; i++) {
    const LevelStat& L = s_levels[i];
    j.key(L.id).obj().kv("plays", L.plays).kv("clears", L.clears).kv("deaths", L.deaths).kv("best", (unsigned long)L.best);
    j.key("heat").arr();
    for (int k = 0; k < 20; k++) j.val(L.heat[k]);
    j.endArr().end();
  }
  j.end().end();
  return j.s;
}

bool fromJson(const char* json, size_t len) {
  using tw::JsonReader;
  JsonReader r(json, len);
  char k[24];
  Lock lock;
  uint32_t games = 0, total = 0, boss = 0, wins = 0;
  Score scores[MAX_SCORES];
  int nScores = 0;
  static LevelStat levels[MAX_LEVEL_STATS];
  int nLevels = 0;
  if (!r.beginObject()) return false;
  while (r.nextKey(k, sizeof k)) {
    if (!strcmp(k, "games")) games = r.asInt(0);
    else if (!strcmp(k, "totalScore")) total = (uint32_t)r.asFloat(0);
    else if (!strcmp(k, "bossKills")) boss = r.asInt(0);
    else if (!strcmp(k, "wins")) wins = r.asInt(0);
    else if (!strcmp(k, "scores")) {
      if (!r.beginArray()) break;
      while (r.nextItem()) {
        Score S;
        memset(&S, 0, sizeof S);
        if (!r.beginObject()) break;
        char kk[12];
        while (r.nextKey(kk, sizeof kk)) {
          if (!strcmp(kk, "name")) r.readString(S.name, sizeof S.name);
          else if (!strcmp(kk, "score")) S.score = r.asInt(0);
          else if (!strcmp(kk, "level")) S.level = r.asInt(0);
          else if (!strcmp(kk, "mode")) S.mode = r.asInt(0);
          else r.skip();
        }
        if (nScores < MAX_SCORES) scores[nScores++] = S;
      }
    } else if (!strcmp(k, "levels")) {
      if (!r.beginObject()) break;
      char id[24];
      while (r.nextKey(id, sizeof id)) {
        LevelStat L;
        memset(&L, 0, sizeof L);
        strlcpy(L.id, id, sizeof L.id);
        if (!r.beginObject()) break;
        char kk[12];
        while (r.nextKey(kk, sizeof kk)) {
          if (!strcmp(kk, "plays")) L.plays = r.asInt(0);
          else if (!strcmp(kk, "clears")) L.clears = r.asInt(0);
          else if (!strcmp(kk, "deaths")) L.deaths = r.asInt(0);
          else if (!strcmp(kk, "best")) L.best = (uint32_t)r.asFloat(0);
          else if (!strcmp(kk, "heat")) {
            if (r.beginArray()) { int i = 0; while (r.nextItem()) { int v = r.asInt(0); if (i < 20) L.heat[i++] = v; } }
          } else r.skip();
        }
        if (nLevels < MAX_LEVEL_STATS) levels[nLevels++] = L;
      }
    } else r.skip();
  }
  if (!r.ok()) return false;
  s_games = games; s_total = total; s_boss = boss; s_wins = wins;
  memcpy(s_scores, scores, sizeof scores);
  s_nScores = nScores;
  memcpy(s_levels, levels, sizeof(LevelStat) * nLevels);
  s_nLevels = nLevels;
  markDirty();
  return true;
}

void reset() {
  Lock lock;
  s_games = s_total = s_boss = s_wins = 0;
  s_nScores = s_nLevels = 0;
  s_pending = -1;
  markDirty();
  s_urgent = true;
}

void flushIfDirty(bool force) {
  if (!s_dirty) return;
  if (!force && !s_urgent && millis() - s_dirtyAt < 15000) return;  // batch frequent changes (deaths) into one write
  String js = toJson();
  File f = LittleFS.open("/stats.json.tmp", "w");
  if (!f) return;
  f.print(js);
  f.close();
  LittleFS.remove("/stats.json");
  LittleFS.rename("/stats.json.tmp", "/stats.json");
  s_dirty = false;
  s_urgent = false;
}

}  // namespace stats
