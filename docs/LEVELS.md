# Level format

The editor in the web app writes this for you. This page is for hand-editing, sharing levels, or understanding what a value does. Unknown keys are ignored, out-of-range values are clamped, and `//` comments are allowed.

The playfield is always **1000 units** long whatever the strip length. Time is in milliseconds. Speeds are units per frame at 60 fps; the player's top speed is 10.

```json
{
  "format": "twang-campaign",
  "version": 1,
  "name": "My campaign",
  "levels": [ { ...level... }, ... ]
}
```

Up to 48 levels; disabled levels stay in the file but are skipped in play.

## Level

| Key | Default | Meaning |
|---|---|---|
| `id` | | Stable identifier. Stats are keyed by it, so reordering keeps them. |
| `name` | | Up to 31 characters. |
| `enabled` | `true` | `false` keeps it out of the campaign. |
| `safe` | `false` | No lives lost and no score (the original level 0). |
| `player` | `0` | Start position. |
| `attackWidth` | `70` | Width of the twang. |
| `music` | `"auto"` | `descent`, `furnace`, `reckoning`, `abyss`, `none`, or `auto` (boss → reckoning, darkness → abyss, 2+ lava pools → furnace, else descent). |
| `tension` | `30` | 0-100. How heavy the music sits before anything is near. |
| `par` | `0` | Seconds. Clearing faster earns 20 points per second saved. |

## Things in a level

**`enemies`** (24 max): `pos`, `dir` (`-1` toward the start, `1` toward the exit), `speed`, `wobble`, `kind`.
- `wobble` > 0 swings the enemy that far either side of `pos` on a sine path; `speed` then sets how fast it swings.
- `kind`: `basic`, `armored` (two hits; the first knocks it back 70 and stuns it), `chaser` (homes in at `speed`, ignores `dir`), `splitter` (on death, two fragments appear further away and come back), `phantom` (solid 1.6 s, faded 1 s; faded it can't hurt or be hurt).

**`spawners`** (4 max): `pos`, `rate` (ms between spawns), `speed`, `dir`, `delay` (before the first), `kind`. They never spawn within 40 units of the player.

**`lava`** (8 max): `left`, `right`, `on`, `off`, `offset`, `startOn`, `grow`, `flow`.
- `offset` pretends the pool has already been in its starting state that long, which lets several pools ripple.
- `grow` widens both edges by one unit every 1/grow frames. `flow` drifts it (positive toward the exit). Both behave exactly like the original.
- It flickers for the last 600 ms before igniting.

**`conveyors`** (4 max): `start`, `end`, `speed` (-9 to 9; negative pushes back toward the start).

**`zones`** (4 max): `type` (`ice` or `dark`), `start`, `end`.

**`pickups`** (12 max): `type` (`gem` +50, or `life`), `pos`.

**`checkpoints`** (4 max): a list of positions. Dying after passing one restarts you there.

**`boss`**: `true` for the original Warden, or an object:

```json
"boss": { "hp": 5, "positions": [850, 300, 700, 450, 600], "rates": [1700, 1500, 1300, 1100, 900],
          "speed": 3, "width": 50, "rage": true }
```

It stands at `positions[0]`; after each hit it moves to the next position and spawns minions both ways every `rates[i]` ms at `speed`. With `rage`, in its last phase it erupts in lava 75 units beyond its body: 750 ms of warning glow, 650 ms burning, 1.9 s quiet. The exit opens when it dies, and its minions die with it.

## Scoring

Kill: 10 × combo (combo builds for kills within 0.9 s, up to ×8). Armor crack 5, gem 50, boss hit 100, boss kill 1000. Level clear: 100 per life left plus the par bonus. Endless adds 50 × depth per level.
