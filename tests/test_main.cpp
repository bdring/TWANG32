// TWANG Next - native test harness.
// Build & run: tests/run.sh   (uses AddressSanitizer + UBSan)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../TWANG32/src/core/tw_game.h"
#include "../TWANG32/src/core/tw_synth.h"

using namespace tw;

static int failures = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      failures++;                                         \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__);       \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

static std::string readFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) { printf("cannot open %s\n", path); exit(2); }
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

// ------------------------------------------------------------------ bot
namespace tw {
struct GameProbe {
  static Input bot(Game& g) {
    Input in;
    in.active = true;
    if (g.stage_ != ST_PLAY) return in;
    int p = (int)g.ppos_;
    int reach = g.attackWidth_ / 2 - 3;
    int ahead = 100000, behind = 100000;
    for (auto& e : g.enemies_) {
      if (!e.alive || !e.solid(g.clock_)) continue;
      int d = e.pos() - p;
      if (d >= 0) ahead = std::min(ahead, d);
      else behind = std::min(behind, -d);
    }
    int bossDir = 0;
    if (g.boss_.alive) {
      int half = g.L_.boss.width / 2;
      if (g.boss_.pos > p) { int d = g.boss_.pos - half - p; ahead = std::min(ahead, std::max(d, 0)); bossDir = 1; }
      else { int d = p - (g.boss_.pos + half); behind = std::min(behind, std::max(d, 0)); bossDir = -1; }
    }
    if (ahead <= reach || behind <= reach) in.attack = true;

    bool wait = false, retreat = false;
    for (auto& l : g.lava_) {
      if (!l.alive) continue;
      int left = (int)l.left, right = (int)l.right;
      if (p >= left - 2 && p <= right) continue;  // already in it: commit
      if (p < left && left - p < 12 && (l.on || l.d.grow > 0)) retreat = true;  // it's creeping toward us
      if (p < left && left - p < 35) {
        int push = 0;
        for (int c = 0; c < g.L_.nConveyors; c++)
          if (g.L_.conveyors[c].start < right && g.L_.conveyors[c].end > left) push = g.L_.conveyors[c].speed;
        float sp = std::max(3.0f, 10.0f + push);
        float crossMs = ((right - p) / sp + 6) * 16.7f;
        uint32_t el = g.clock_ - l.lastToggle;
        if (l.on) wait = true;
        else if ((float)l.d.off - (float)el < crossMs + 80) wait = true;
      }
    }
    if (g.boss_.alive && g.boss_.rageState) {
      int r = g.L_.boss.width / 2 + 75;
      if (std::abs(p - g.boss_.pos) < r + 25) retreat = true;
    }
    // phantom fading in right in front of us: hold still and let it come into reach
    for (auto& e : g.enemies_) {
      if (!e.alive || e.solid(g.clock_)) continue;
      int d = e.pos() - p;
      if (d > 0 && d < 30) wait = true;
    }
    in.tilt = retreat ? -60.0f * (bossDir ? bossDir : 1) : (wait ? 0.0f : 60.0f);
    if (!retreat && bossDir < 0) in.tilt = -60.0f;  // the boss teleported behind us: go back for it
    return in;
  }
  static int ppos(Game& g) { return (int)g.ppos_; }
};
}  // namespace tw

// ------------------------------------------------------------------ helpers
struct RunStats {
  int deaths = 0, clears = 0, kills = 0, attacks = 0, events = 0;
  int maxStageTicks = 0;
};

static RunStats drive(Game& g, int ticks, int leds, bool stopOnClear, int* clearedAt = nullptr) {
  RunStats st;
  static RGB fb[1000];
  int lastStage = -1, stageTicks = 0;
  for (int t = 0; t < ticks; t++) {
    Input in = GameProbe::bot(g);
    g.tick(in);
    if (t % 3 == 0) g.render(fb, leds);
    Event e;
    while (g.pollEvent(e)) {
      st.events++;
      if (e.type == EV_DIE) st.deaths++;
      if (e.type == EV_KILL) st.kills++;
      if (e.type == EV_ATTACK) st.attacks++;
      if (e.type == EV_LEVEL_CLEAR) {
        st.clears++;
        if (clearedAt && *clearedAt < 0) *clearedAt = t;
        if (stopOnClear) return st;
      }
    }
    if ((int)g.stage() != lastStage) { lastStage = g.stage(); stageTicks = 0; }
    else if (++stageTicks > st.maxStageTicks) st.maxStageTicks = stageTicks;
  }
  return st;
}

static void writeWav(const char* path, const std::vector<int16_t>& s, int sr) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  uint32_t dataBytes = (uint32_t)s.size() * 2;
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); fwrite("WAVEfmt ", 1, 8, f);
  w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
  fwrite("data", 1, 4, f); w32(dataBytes);
  fwrite(s.data(), 2, s.size(), f);
  fclose(f);
}

struct Level { double peak, rms; int clipped; };
static Level measure(const std::vector<int16_t>& s) {
  Level L{0, 0, 0};
  double acc = 0;
  for (int16_t v : s) {
    double a = std::abs((double)v) / 32768.0;
    L.peak = std::max(L.peak, a);
    acc += a * a;
    if (std::abs((int)v) > 30500) L.clipped++;
  }
  L.rms = s.empty() ? 0 : std::sqrt(acc / s.size());
  return L;
}

// ------------------------------------------------------------------ tests
static Campaign campaignNext, campaignClassic;

static void testParsing(const std::string& next, const std::string& classic) {
  printf("[parse]\n");
  ParseResult r = parseCampaign(next.data(), next.size(), campaignNext);
  CHECK(r.ok, "next.json: %s", r.message);
  CHECK(campaignNext.count == 33, "next has %d levels", campaignNext.count);
  CHECK(r.warnings == 0, "next.json has %d warnings", r.warnings);
  r = parseCampaign(classic.data(), classic.size(), campaignClassic);
  CHECK(r.ok, "classic.json: %s", r.message);
  CHECK(campaignClassic.count == 20, "classic has %d levels", campaignClassic.count);
  CHECK(r.warnings == 0, "classic.json has %d warnings", r.warnings);
  // spot-check faithful conversion
  const LevelDef& c7 = campaignClassic.levels[7];
  CHECK(c7.nEnemies == 6 && c7.enemies[5].wobble == 400 && c7.enemies[5].speed == 5, "level 7 enemies wrong");
  const LevelDef& c15 = campaignClassic.levels[15];
  CHECK(c15.nLava == 1 && c15.lava[0].offset == 2000 && c15.lava[0].off == 800, "level 15 lava wrong");
  CHECK(campaignClassic.levels[19].boss.present && campaignClassic.levels[19].boss.hp == 3, "boss missing");
  CHECK(campaignClassic.levels[13].attackWidth == 32, "needle point width");
  CHECK(campaignClassic.levels[0].safe(), "level 0 should be safe");

  const char* bad = "{\n \"levels\": [\n  { \"name\": \"x\", \"enemies\": [ {\"pos\": 5,, } ] }\n ]\n}";
  Campaign tmp;
  r = parseCampaign(bad, strlen(bad), tmp);
  CHECK(!r.ok, "malformed json accepted");
  CHECK(strstr(r.message, "line 3") != nullptr, "error message lacks line: %s", r.message);
  printf("  malformed json -> \"%s\"\n", r.message);

  const char* single = "{\"name\":\"t\",\"lava\":[{\"left\":600,\"right\":500}],\"boss\":true,\"music\":\"furnace\"}";
  LevelDef L;
  r = parseLevel(single, strlen(single), L);
  CHECK(r.ok && L.lava[0].left == 500 && L.boss.present && L.music == MUSIC_FURNACE, "single level parse");
}

static void testEveryLevel(Campaign& C, const char* label) {
  printf("[bot: %s]\n", label);
  int cleared = 0;
  for (int i = 0; i < C.count; i++) {
    Game g;
    g.init(&C, 1234 + i);
    g.opt.attract = false;
    g.startRun(MODE_PRACTICE, i);
    int at = -1;
    RunStats st = drive(g, 60 * 120, 144, true, &at);
    bool ok = st.clears > 0;
    cleared += ok;
    printf("  %2d %-20s %s  deaths=%-3d kills=%-3d %s\n", i, C.levels[i].name, ok ? "CLEAR" : "-----", st.deaths,
           st.kills, ok ? "" : "(bot could not clear in 2 min)");
    CHECK(st.maxStageTicks < 60 * 130, "level %d: stage stuck", i);
  }
  printf("  bot cleared %d/%d\n", cleared, C.count);
  CHECK(cleared >= C.count * 2 / 3, "%s: bot cleared only %d/%d", label, cleared, C.count);
}

static void testEndless() {
  printf("[endless generator]\n");
  int bad = 0, cleared = 0;
  for (int d = 0; d < 250; d++) {
    LevelDef L;
    generateLevel(d, 777, L);
    for (int i = 0; i < L.nEnemies; i++)
      if (L.enemies[i].pos - L.enemies[i].wobble < 60) { bad++; printf("  depth %d enemy too close to start\n", d); }
    for (int i = 0; i < L.nLava; i++)
      if (L.lava[i].left < 100 && !L.boss.present && L.lava[i].right > 0 && L.lava[i].left <= L.playerStart) {
        bad++; printf("  depth %d lava on start\n", d);
      }
    for (int i = 0; i < L.nCheckpoints; i++)
      for (int k = 0; k < L.nLava; k++)
        if (L.checkpoints[i] >= L.lava[k].left && L.checkpoints[i] <= L.lava[k].right) { bad++; printf("  depth %d checkpoint in lava\n", d); }
  }
  CHECK(bad == 0, "%d invalid generated levels", bad);
  for (int d = 0; d < 30; d++) {
    LevelDef L;
    generateLevel(d, 4242, L);
    Game g;
    g.init(nullptr, 99 + d);
    g.opt.attract = false;
    g.testLevel(L, false);
    RunStats st = drive(g, 60 * 120, 300, true);
    cleared += st.clears > 0;
  }
  printf("  bot cleared %d/30 of the first endless depths\n", cleared);
  CHECK(cleared >= 20, "endless too hard for the bot: %d/30", cleared);
}

static void testStripSizesAndModes() {
  printf("[strip sizes, modes, determinism]\n");
  int sizes[] = {1, 2, 60, 144, 288, 300, 1000};
  for (int n : sizes) {
    Game g;
    g.init(&campaignNext, 5);
    g.opt.idleTimeoutSec = 5;
    RGB fb[1000];
    memset(fb, 0, sizeof fb);
    for (int t = 0; t < 60 * 90; t++) {
      Input in = GameProbe::bot(g);
      if (t > 60 * 60) in.active = false, in.tilt = 0;  // let it fall into attract mode
      g.tick(in);
      g.render(fb, n);
      Event e;
      while (g.pollEvent(e)) {}
    }
    CHECK(g.stage() == ST_ATTRACT, "strip %d: expected attract after idling, stage=%d", n, g.stage());
  }
  // determinism: same seed + same inputs = same outcome
  auto runOnce = [](uint32_t seed) {
    Game g;
    g.init(&campaignNext, seed);
    g.opt.attract = false;
    unsigned long long sig = 0;
    for (int t = 0; t < 60 * 120; t++) {
      g.tick(GameProbe::bot(g));
      Event e;
      while (g.pollEvent(e)) sig = sig * 31u + (unsigned long long)(e.type * 7 + e.pos + e.value);
    }
    return sig ^ ((unsigned long long)g.score() << 20) ^ (unsigned long long)g.levelIndex();
  };
  CHECK(runOnce(42) == runOnce(42), "not deterministic");
  // every mode starts and survives
  for (int m = 0; m < MODE_COUNT; m++) {
    Game g;
    g.init(&campaignNext, 3);
    g.opt.attract = false;
    if (m == MODE_TEST) g.testLevel(campaignNext.levels[10], true);
    else g.startRun((Mode)m, 2);
    RunStats st = drive(g, 60 * 60, 144, false);
    CHECK(st.events > 0, "mode %d produced no events", m);
  }
  // full campaign marathon with lives
  Game g;
  g.init(&campaignNext, 2024);
  g.opt.attract = false;
  g.highScore = 500;
  int gameOvers = 0, maxLevel = 0;
  for (int t = 0; t < 60 * 60 * 20; t++) {
    g.tick(GameProbe::bot(g));
    Event e;
    while (g.pollEvent(e)) if (e.type == EV_GAME_OVER) gameOvers++;
    maxLevel = std::max(maxLevel, g.levelIndex());
  }
  printf("  20 min campaign marathon: furthest level %d, %d game overs\n", maxLevel, gameOvers);
  CHECK(maxLevel >= 10, "bot never got past level %d in campaign", maxLevel);
}

static void testAudio(const char* outDir) {
  printf("[audio]\n");
  const int SR = 22050;
  Synth s;
  s.init(SR);
  SoundDirector d;
  d.attach(&s);
  std::vector<int16_t> buf;
  auto renderSec = [&](float sec) {
    int n = (int)(sec * SR);
    std::vector<int16_t> out(n);
    for (int i = 0; i < n; i += 256) s.render(out.data() + i, std::min(256, n - i));
    return out;
  };
  const char* trackNames[4] = {"descent", "furnace", "reckoning", "abyss"};
  float intens[3] = {0.15f, 0.55f, 1.0f};
  for (int t = 0; t < 4; t++) {
    std::vector<int16_t> all;
    for (float I : intens) {
      s.init(SR);
      s.volumes(0.8f, 0.7f, 0.9f);
      s.music((uint8_t)t);
      s.intensity(I);
      auto seg = renderSec(9.0f);
      Level L = measure(seg);
      printf("  %-10s intensity %.2f  peak %.2f  rms %.3f  clipped %d\n", trackNames[t], I, L.peak, L.rms, L.clipped);
      CHECK(L.rms > 0.01, "%s too quiet at %.2f", trackNames[t], I);
      CHECK(L.clipped < (int)seg.size() / 200, "%s clipping at %.2f", trackNames[t], I);
      all.insert(all.end(), seg.begin(), seg.end());
    }
    char path[256];
    snprintf(path, sizeof path, "%s/music_%s.wav", outDir, trackNames[t]);
    writeWav(path, all, SR);
  }
  const char* sfxNames[] = {"twang", "kill", "armor", "split", "die", "level_clear", "boss_hit", "boss_killed",
                            "gem", "life", "checkpoint", "lava_ignite", "rage", "game_over"};
  std::vector<int16_t> board;
  for (int i = 0; i < 14; i++) {
    s.init(SR);
    s.volumes(0.8f, 0.7f, 0.9f);
    d.testSound(i);
    auto seg = renderSec(i == 7 || i == 13 || i == 5 ? 2.6f : 1.2f);
    Level L = measure(seg);
    printf("  sfx %-12s peak %.2f  rms %.3f\n", sfxNames[i], L.peak, L.rms);
    CHECK(L.peak > 0.05, "sfx %s silent", sfxNames[i]);
    CHECK(L.clipped < (int)seg.size() / 100, "sfx %s clipping", sfxNames[i]);
    char path[256];
    snprintf(path, sizeof path, "%s/sfx_%s.wav", outDir, sfxNames[i]);
    writeWav(path, seg, SR);
    board.insert(board.end(), seg.begin(), seg.end());
  }
  char path[256];
  snprintf(path, sizeof path, "%s/sfx_all.wav", outDir);
  writeWav(path, board, SR);

  // a minute of real gameplay with music + effects
  Game g;
  g.init(&campaignNext, 77);
  g.opt.attract = false;
  s.init(SR);
  s.volumes(0.8f, 0.6f, 0.9f);
  std::vector<int16_t> play;
  int16_t chunk[368];
  for (int t = 0; t < 60 * 75; t++) {
    g.tick(GameProbe::bot(g));
    Event e;
    while (g.pollEvent(e)) d.onEvent(e);
    d.update(g.audio());
    int n = t % 4 == 3 ? 366 : 367;  // 22050 / 60 = 367.5
    s.render(chunk, n);
    play.insert(play.end(), chunk, chunk + n);
  }
  Level L = measure(play);
  printf("  75 s gameplay mix: peak %.2f rms %.3f clipped %d, dropped cmds %u\n", L.peak, L.rms, L.clipped,
         s.droppedCommands());
  CHECK(s.droppedCommands() == 0, "synth dropped commands");
  CHECK(L.clipped < (int)play.size() / 300, "gameplay mix clipping");
  snprintf(path, sizeof path, "%s/gameplay.wav", outDir);
  writeWav(path, play, SR);
}

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : ".";
  std::string next = readFile("campaign/next.json");
  std::string classic = readFile("campaign/classic.json");
  testParsing(next, classic);
  testEveryLevel(campaignClassic, "classic 20");
  testEveryLevel(campaignNext, "TWANG Next");
  testEndless();
  testStripSizesAndModes();
  testAudio(outDir);
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
  return failures ? 1 : 0;
}
