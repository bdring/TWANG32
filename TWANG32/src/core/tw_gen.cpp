/*
  Endless mode: procedural levels that get harder forever.

  Each depth unlocks more mechanics. Constraints keep every level fair:
  nothing spawns on top of the player, lava never covers the start or the
  exit, conveyors never out-push the player, and every 7th level is a boss.
*/
#include "tw_game.h"

namespace tw {

static int pickSlot(Rng& r, int* used, int n, int lo, int hi, int minGap) {
  for (int tries = 0; tries < 24; tries++) {
    int p = r.range(lo, hi);
    bool ok = true;
    for (int i = 0; i < n; i++) if (absi(used[i] - p) < minGap) { ok = false; break; }
    if (ok) return p;
  }
  return r.range(lo, hi);
}

void generateLevel(int depth, uint32_t seed, LevelDef& L) {
  L.clear();
  Rng r;
  r.seed(seed * 2654435761u + (uint32_t)depth * 40503u + 17u);
  int d = depth < 0 ? 0 : depth;

  // name: "Depth 12"
  const char* pre = "Depth ";
  int o = 0;
  while (pre[o]) { L.name[o] = pre[o]; o++; }
  char num[8]; int nn = 0; int v = d + 1;
  while (v > 0 && nn < 7) { num[nn++] = (char)('0' + v % 10); v /= 10; }
  while (nn > 0) L.name[o++] = num[--nn];
  L.name[o] = 0;
  strcopy(L.id, "endless", sizeof(L.id));

  L.playerStart = 0;
  L.tension = (uint8_t)clampi(25 + d * 4, 25, 95);
  L.par = (uint16_t)(18 + d);
  L.music = MUSIC_AUTO;

  // boss every 7th level
  if (d > 0 && (d + 1) % 7 == 0) {
    int hp = clampi(3 + d / 14, 3, MAX_BOSS_HP);
    int pos[MAX_BOSS_HP], rate[MAX_BOSS_HP];
    for (int i = 0; i < MAX_BOSS_HP; i++) {
      pos[i] = r.range(250, 850);
      if (i > 0 && absi(pos[i] - pos[i - 1]) < 220) pos[i] = pos[i - 1] > 500 ? pos[i - 1] - 300 : pos[i - 1] + 300;
      rate[i] = clampi(1900 - d * 25 - i * 250, 600, 2000);
    }
    L.setBoss(hp, pos, rate, clampi(2 + d / 10, 2, 5), 40, d >= 13);
    if (d >= 20) L.addLava(80, 140, 1800, 2600, r.range(0, 1500), false, 0, 0);
    return;
  }

  int used[32];
  int nUsed = 0;
  used[nUsed++] = 0;     // player start
  used[nUsed++] = 1000;  // exit

  // ---- lava (d >= 2): non-overlapping pools with room to stand between them
  if (d >= 2) {
    int pools = clampi(1 + d / 6 + (int)r.below(2), 1, d >= 18 ? 4 : 3);
    int cursor = 170;
    bool grown = false;
    for (int i = 0; i < pools; i++) {
      int room = 920 - cursor;
      int slots = pools - i;
      if (room < 120) break;
      int w = r.range(45, clampi(65 + d * 2, 65, 115));
      int maxStart = cursor + room / slots - w;
      if (maxStart <= cursor) break;
      int left = r.range(cursor, maxStart);
      int on = r.range(1500, 2200);
      int off = r.range(1900, 2600);
      float grow = 0, flow = 0;
      if (!grown && d >= 6 && r.below(5) == 0) { grow = 0.05f + 0.025f * r.range(0, 2); grown = true; }
      else if (d >= 5 && r.below(4) == 0) flow = (r.below(2) ? 0.2f : -0.2f) * (1.0f + 0.25f * r.range(0, 3));
      L.addLava(left, left + w, on, off, (int)r.below(2400), false, grow, flow);
      if (nUsed < 30) used[nUsed++] = left + w / 2;
      cursor = left + w + 100;  // a safe ledge after every pool
    }
  }

  // ---- conveyor (d >= 4); gentler where it runs over lava
  if (d >= 4 && r.below(3) != 0) {
    int a = r.range(80, 450);
    int b = clampi(a + r.range(250, 500), a + 150, 980);
    int sp = r.range(3, clampi(3 + d / 6, 3, 6));
    bool overLava = false;
    for (int i = 0; i < L.nLava; i++) if (L.lava[i].right > a && L.lava[i].left < b) overLava = true;
    if (overLava) sp = mini(sp, 3);
    L.addConveyor(a, b, r.below(3) == 0 ? sp : -sp);
  }

  // ---- ice sits in a gap between lava pools, darkness can go anywhere
  if (d >= 6 && r.below(3) == 0) {
    int prevEnd = 150;
    for (int i = 0; i <= L.nLava; i++) {
      int nextStart = i < L.nLava ? L.lava[i].left : 980;
      if (nextStart - prevEnd >= 200) {
        int a = prevEnd + 20;
        int b = mini(a + r.range(120, 260), nextStart - 90);  // leave room to stop before the next pool
        if (b - a >= 80) { L.addZone(a, b, ZONE_ICE); break; }
      }
      if (i < L.nLava) prevEnd = L.lava[i].right;
    }
  }
  if (d >= 9 && r.below(3) == 0) {
    int a = r.range(300, 700);
    L.addZone(a, clampi(a + r.range(150, 300), a, 980), ZONE_DARK);
  }

  // ---- enemies
  int nEnemies = clampi(1 + d / 3 + (int)r.below(2), 1, d >= 20 ? 8 : 7);
  for (int i = 0; i < nEnemies; i++) {
    uint8_t kind = EK_BASIC;
    uint32_t roll = r.below(100);
    if (d >= 6 && roll < 16) kind = EK_ARMORED;
    else if (d >= 8 && roll < 28) kind = EK_CHASER;
    else if (d >= 10 && roll < 40) kind = EK_SPLITTER;
    else if (d >= 12 && roll < 50) kind = EK_PHANTOM;
    int pos = pickSlot(r, used, nUsed, 220, 960, 50);
    int speed;
    switch (kind) {
      case EK_CHASER: speed = d >= 18 ? r.range(1, 2) : 1; break;
      case EK_ARMORED: speed = r.range(0, 2); break;
      case EK_SPLITTER:
      case EK_PHANTOM: speed = r.range(0, 2); break;
      default: speed = r.range(0, clampi(1 + d / 6, 1, 3)); break;
    }
    int wobble = 0;
    if (kind == EK_BASIC && d >= 3 && r.below(3) == 0) {
      wobble = r.range(120, clampi(150 + d * 10, 150, 320));
      pos = clampi(pos, 150 + wobble, 990 - wobble / 2);
      if (pos - wobble < 120) pos = 120 + wobble;
      speed = r.range(4, 7);
    }
    if (pos - wobble <= 60) continue;
    L.addEnemy(pos, -1, speed, wobble, kind);
    if (nUsed < 30) used[nUsed++] = pos;
  }

  // ---- spawner (d >= 1)
  if (d >= 1) {
    int rate = clampi(5200 - d * 150, 1700, 5200);
    L.addSpawner(1000, rate, r.range(2, clampi(2 + d / 8, 2, 4)), -1, r.range(0, 2000),
                 d >= 11 && r.below(3) == 0 ? EK_ARMORED : EK_BASIC);
    if (d >= 9 && r.below(2) == 0) L.addSpawner(0, clampi(rate + 1500, 2500, 8000), 3, 1, 6000, EK_BASIC);
  }

  // ---- rewards
  int gems = 1 + (int)r.below(2);
  for (int i = 0; i < gems; i++) L.addPickup(pickSlot(r, used, nUsed, 150, 950, 30), PK_GEM);
  if (d > 0 && d % 5 == 4) L.addPickup(r.range(400, 800), PK_LIFE);
  if (d >= 8) {
    // checkpoint somewhere mid-level that is never inside lava
    for (int cp = 480; cp < 700; cp += 20) {
      bool clear = true;
      for (int i = 0; i < L.nLava; i++)
        if (cp > L.lava[i].left - 40 && cp < L.lava[i].right + 40) clear = false;
      if (clear) { L.addCheckpoint(cp); break; }
    }
  }
  if (d < 2) L.attackWidth = 80;
}

}  // namespace tw
