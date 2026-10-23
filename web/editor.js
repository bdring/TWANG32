/* TWANG Next web UI - level editor */
'use strict';

const LIMITS = { enemies: 24, spawners: 4, lava: 8, conveyors: 4, zones: 4, pickups: 12, checkpoints: 4, bossHp: 6, levels: 48 };
const MUSIC_OPTS = [['auto', 'Pick for me'], ['descent', 'Descent'], ['furnace', 'Furnace'], ['reckoning', 'Reckoning'], ['abyss', 'Abyss'], ['none', 'Silence']];
const LANES = [['player', 'Player'], ['terrain', 'Ground'], ['lava', 'Lava'], ['enemies', 'Enemies'], ['spawners', 'Spawners'], ['pickups', 'Pickups'], ['boss', 'Boss']];
const newId = () => 'l' + Math.random().toString(36).slice(2, 8);
const ADDS = [
  ['Enemy', '--led-enemy', (L) => list(L, 'enemies').push({ pos: freeSpot(L), dir: -1, speed: 1 }), 'enemy'],
  ['Armored', '--led-armored', (L) => list(L, 'enemies').push({ pos: freeSpot(L), dir: -1, speed: 0, kind: 'armored' }), 'enemy'],
  ['Chaser', '--led-chaser', (L) => list(L, 'enemies').push({ pos: freeSpot(L), dir: -1, speed: 1, kind: 'chaser' }), 'enemy'],
  ['Splitter', '--led-splitter', (L) => list(L, 'enemies').push({ pos: freeSpot(L), dir: -1, speed: 0, kind: 'splitter' }), 'enemy'],
  ['Phantom', '--led-phantom', (L) => list(L, 'enemies').push({ pos: freeSpot(L), dir: -1, speed: 1, kind: 'phantom' }), 'enemy'],
  ['Swinger', '--led-enemy', (L) => list(L, 'enemies').push({ pos: 650, dir: 1, speed: 6, wobble: 220 }), 'enemy'],
  ['Spawner', '--led-enemy', (L) => list(L, 'spawners').push({ pos: 1000, rate: 3000, speed: 2, dir: -1, delay: 0 }), 'spawner'],
  ['Lava', '--led-lava', (L) => { const p = freeSpot(L); list(L, 'lava').push({ left: p - 40, right: p + 40, on: 2000, off: 2000, offset: 0 }); }, 'lava'],
  ['Conveyor', '--led-conveyor', (L) => list(L, 'conveyors').push({ start: 300, end: 650, speed: -4 }), 'conveyor'],
  ['Ice', '--led-ice', (L) => list(L, 'zones').push({ type: 'ice', start: 300, end: 550 }), 'zone'],
  ['Darkness', '--led-dark', (L) => list(L, 'zones').push({ type: 'dark', start: 200, end: 900 }), 'zone'],
  ['Gem', '--led-gem', (L) => list(L, 'pickups').push({ type: 'gem', pos: freeSpot(L) }), 'pickup'],
  ['Extra life', '--led-life', (L) => list(L, 'pickups').push({ type: 'life', pos: freeSpot(L) }), 'pickup'],
  ['Checkpoint', '--led-check', (L) => list(L, 'checkpoints').push(500), 'checkpoint'],
];
const TYPE_LIST = { enemy: 'enemies', spawner: 'spawners', lava: 'lava', conveyor: 'conveyors', zone: 'zones', pickup: 'pickups', checkpoint: 'checkpoints' };
const LIMIT_OF = { enemy: 'enemies', spawner: 'spawners', lava: 'lava', conveyor: 'conveyors', zone: 'zones', pickup: 'pickups', checkpoint: 'checkpoints' };

function list(L, key) { if (!Array.isArray(L[key])) L[key] = []; return L[key]; }
function freeSpot(L) {
  const taken = [L.player || 0, ...(L.enemies || []).map((e) => e.pos), ...(L.pickups || []).map((p) => p.pos)];
  for (let p = 500; p < 960; p += 40) if (taken.every((t) => Math.abs(t - p) > 45)) return p;
  for (let p = 460; p > 140; p -= 40) if (taken.every((t) => Math.abs(t - p) > 45)) return p;
  return 500;
}
function bossOf(L) {
  if (L.boss === true) L.boss = { hp: 3, positions: [800, 200, 600], rates: [1800, 1600, 1000], speed: 3, width: 40 };
  return L.boss && typeof L.boss === 'object' ? L.boss : null;
}
const BOSS_POS = [800, 200, 600, 400, 900, 300], BOSS_RATE = [1800, 1600, 1000, 900, 800, 700];

// ---------------------------------------------------------- fairness checks
function issues(L) {
  const out = [];
  const err = (msg) => out.push({ sev: 'err', msg });
  const warn = (msg) => out.push({ sev: 'warn', msg });
  const start = L.player || 0;
  for (const [k, lim] of Object.entries({ enemies: LIMITS.enemies, spawners: LIMITS.spawners, lava: LIMITS.lava, conveyors: LIMITS.conveyors, zones: LIMITS.zones, pickups: LIMITS.pickups, checkpoints: LIMITS.checkpoints })) {
    if ((L[k] || []).length > lim) err(`Too many ${k}: ${L[k].length} of ${lim}. The extras are ignored.`);
  }
  if (!(L.name || '').trim()) warn('This level has no name.');
  for (const e of L.enemies || []) {
    const w = e.wobble || 0;
    if (!w && Math.abs(e.pos - start) < 50 && e.pos > start) warn(`The enemy at ${e.pos} starts on top of the player.`);
    if (w && e.pos - w <= start + 20 && e.pos + w >= start - 20) warn(`The swinger at ${e.pos} swings over the player start.`);
  }
  for (const l of L.lava || []) {
    const a = Math.min(l.left, l.right), b = Math.max(l.left, l.right);
    if (start >= a && start <= b) err(`Lava ${a}-${b} covers the player start.`);
    if (b >= 995) warn(`Lava ${a}-${b} reaches the exit.`);
    const crossMs = ((b - a) / 10 + 4) * 16.7;
    if ((l.off ?? 2000) < crossMs + 150 && !(l.grow || l.flow)) err(`Lava ${a}-${b} is off for ${l.off} ms, but crossing it takes about ${Math.round(crossMs)} ms.`);
    if ((l.grow || 0) > 0.3) warn(`Lava ${a}-${b} grows fast (${l.grow}). Players will need to hurry.`);
    for (const c of L.checkpoints || []) if (c >= a && c <= b) err(`The checkpoint at ${c} is inside lava ${a}-${b}.`);
  }
  for (const c of L.conveyors || []) {
    if (Math.abs(c.speed) >= 9) warn(`Conveyor ${c.start}-${c.end} at speed ${c.speed} leaves the player almost no headway.`);
  }
  for (const s of L.spawners || []) if (Math.abs(s.pos - start) < 60) warn(`The spawner at ${s.pos} is right next to the player start.`);
  const B = bossOf(L);
  if (B) {
    const hp = clamp(B.hp || 3, 1, 6);
    for (let i = 0; i < hp; i++) {
      const p = (B.positions || BOSS_POS)[i] ?? BOSS_POS[i];
      if (Math.abs(p - start) < (B.width || 40) / 2 + 40) err(`Boss position ${i + 1} (${p}) is on top of the player start.`);
    }
  }
  const empty = !(L.enemies || []).length && !(L.spawners || []).length && !(L.lava || []).length && !B;
  if (empty) warn('Nothing can hurt the player here yet. Add an enemy, a spawner or some lava.');
  return out;
}

// ---------------------------------------------------------- editor
const Editor = {
  root: null,
  camp: null,
  idx: 0,
  sel: null,
  undoStack: [],
  redoStack: [],
  dirty: false,
  spectate: true,
  visible: false,
  previewDeaths: {},

  mount(root) {
    this.root = root;
    this.camp = app.campaign;
    this.normalize(this.camp);
    root.append(
      h('div', { class: 'ed-bar' },
        h('input', { class: 'cname', id: 'campName', 'aria-label': 'Campaign name', style: { background: 'transparent', border: 0, fontSize: '18px', minWidth: 0, flex: '1 1 160px' },
          onchange: (e) => this.edit(() => { this.camp.name = e.target.value.slice(0, 31); }) }),
        h('span', { class: 'dirty', id: 'edStatus' }),
        h('button', { class: 'btn small', id: 'edUndo', onclick: () => this.undo(), title: 'Undo (Ctrl+Z)' }, 'Undo'),
        h('button', { class: 'btn small', id: 'edRedo', onclick: () => this.redo(), title: 'Redo (Ctrl+Shift+Z)' }, 'Redo'),
        h('select', { class: 'btn small', id: 'edPreset', 'aria-label': 'Load a preset', onchange: (e) => { this.loadPreset(e.target.value); e.target.value = ''; } },
          h('option', { value: '' }, 'Load a preset…'), h('option', { value: 'next' }, 'TWANG Next (33 levels)'),
          h('option', { value: 'classic' }, 'Classic 20 (the original)'), h('option', { value: 'empty' }, 'Start from scratch')),
        h('button', { class: 'btn small', onclick: () => $('#edImport').click() }, 'Import'),
        h('input', { type: 'file', id: 'edImport', accept: '.json,application/json', hidden: true, onchange: (e) => this.importFile(e.target.files[0]) }),
        h('button', { class: 'btn small', onclick: () => downloadJson(this.camp, slug(this.camp.name || 'campaign') + '.json') }, 'Export'),
        h('button', { class: 'btn small', onclick: () => this.copyJson() }, 'Copy JSON'),
        app.mode === 'device' ? h('button', { class: 'btn primary small', id: 'edSave', onclick: () => this.saveToDevice() }, 'Save to TWANG') : null,
      ),
      h('div', { id: 'edDraft' }),
      h('div', { class: 'ed' },
        h('div', null,
          h('ul', { class: 'levels', id: 'edList', 'aria-label': 'Levels in play order' }),
          h('div', { class: 'row', style: { marginTop: '8px' } },
            h('button', { class: 'btn small', onclick: () => this.addLevel() }, 'New level'),
            h('span', { class: 'hint' }, 'Drag the handle to reorder.'))),
        h('div', { id: 'edDetail' })),
    );
    window.addEventListener('keydown', (e) => {
      if (!this.visible || !(e.ctrlKey || e.metaKey) || e.key.toLowerCase() !== 'z') return;
      if (/^(INPUT|TEXTAREA)$/.test(e.target.tagName)) return;
      e.preventDefault();
      if (e.shiftKey) this.redo(); else this.undo();
    });
    if (app.mode === 'device') {
      const draft = store.get('draft', null);
      if (draft && JSON.stringify(draft) !== JSON.stringify(this.camp)) {
        $('#edDraft').append(h('div', { class: 'notice' }, 'You have unsaved level edits from earlier. ',
          h('button', { class: 'btn small', onclick: (e) => { this.pushUndo(); this.setCampaign(draft); this.dirty = true; e.target.closest('.notice').remove(); this.render(); } }, 'Restore them'),
          ' ', h('button', { class: 'btn small ghost', onclick: (e) => { store.set('draft', null); e.target.closest('.notice').remove(); } }, 'Discard')));
      }
    }
  },

  normalize(c) {
    if (!c.levels) c.levels = [];
    const seen = new Set();
    for (const l of c.levels) {
      if (!l.id || seen.has(l.id)) l.id = newId();
      seen.add(l.id);
      if (l.boss === true) bossOf(l);
    }
    if (!c.format) c.format = 'twang-campaign';
    if (!c.version) c.version = 1;
  },
  setCampaign(c) {
    this.normalize(c);
    this.camp = c;
    app.campaign = c;
    this.idx = clamp(this.idx, 0, Math.max(0, c.levels.length - 1));
    this.sel = null;
  },
  level() { return this.camp.levels[this.idx]; },

  onShow() {
    this.visible = true;
    if (this.camp !== app.campaign) this.setCampaign(app.campaign);
    this.render();
    this.preview(true);
  },
  onHide() {
    this.visible = false;
    this.persist(true);
  },
  onStats() { if (this.visible) this.renderHeat(); },

  // ---------------------------------------------------------- history and saving
  snapshot() { return JSON.stringify(this.camp); },
  pushUndo() {
    this.undoStack.push(this.snapshot());
    if (this.undoStack.length > 80) this.undoStack.shift();
    this.redoStack = [];
  },
  undo() {
    if (!this.undoStack.length) return;
    this.redoStack.push(this.snapshot());
    this.setCampaign(JSON.parse(this.undoStack.pop()));
    this.changed();
  },
  redo() {
    if (!this.redoStack.length) return;
    this.undoStack.push(this.snapshot());
    this.setCampaign(JSON.parse(this.redoStack.pop()));
    this.changed();
  },
  edit(fn, opts = {}) {
    if (opts.undo !== false) this.pushUndo();
    fn(this.level(), this.camp);
    this.changed(opts);
  },
  changed(opts = {}) {
    this.dirty = true;
    this.persist(false);
    if (opts.render !== false) this.render();
    this.preview();
    if (app.refreshPracticeList) app.refreshPracticeList();
  },
  persist(now) {
    clearTimeout(this.persistTimer);
    const go = () => {
      if (app.mode === 'sim') { store.set('campaign', this.camp); this.dirty = false; this.renderStatus(); }
      else if (this.dirty) store.set('draft', this.camp);
    };
    if (now) go(); else this.persistTimer = setTimeout(go, 500);
  },
  async saveToDevice() {
    const r = app.engine.validate(this.camp);
    if (!r.ok) { toast(r.message); return; }
    const btn = $('#edSave');
    btn.disabled = true;
    try {
      const res = await app.dev.api('/api/campaign', { method: 'PUT', body: JSON.stringify(this.camp) });
      this.dirty = false;
      store.set('draft', null);
      toast(`Saved ${res.levels} levels to TWANG${res.warnings ? `, ${res.warnings} values adjusted to fit` : ''}`);
    } catch (e) { toast('Not saved: ' + e.message); }
    btn.disabled = false;
    this.renderStatus();
  },
  renderStatus() {
    const s = $('#edStatus');
    if (!s) return;
    s.textContent = app.mode === 'sim' ? (this.dirty ? 'Saving…' : 'Saved in this browser') : (this.dirty ? 'Unsaved changes' : 'Saved on TWANG');
    s.style.color = this.dirty && app.mode === 'device' ? 'var(--led-splitter)' : 'var(--ash)';
    $('#edUndo').disabled = !this.undoStack.length;
    $('#edRedo').disabled = !this.redoStack.length;
  },

  // ---------------------------------------------------------- campaign operations
  async loadPreset(k) {
    if (!k) return;
    const src = k === 'empty' ? { format: 'twang-campaign', version: 1, name: 'My campaign', levels: [this.blankLevel()] } : clone(window.TWANG_DATA.campaigns[k]);
    if (!(await ask(`Replace the levels you are editing with "${src.name}"? You can undo this.`, 'Replace'))) return;
    this.pushUndo();
    this.idx = 0;
    this.setCampaign(src);
    this.changed();
    toast(`Loaded ${src.name}`);
  },
  async importFile(file) {
    if (!file) return;
    $('#edImport').value = '';
    let j;
    try { j = JSON.parse(await file.text()); } catch (e) { toast(`${file.name} is not valid JSON: ${e.message}`); return; }
    if (Array.isArray(j)) j = { levels: j };
    if (j.format === 'twang-backup') j = j.campaign;
    if (j && !j.levels && (j.enemies || j.lava || j.spawners || j.boss || j.name)) {
      this.edit((L, c) => { j.id = newId(); c.levels.splice(this.idx + 1, 0, j); this.idx++; });
      toast(`Added level "${j.name || 'Untitled'}"`);
      return;
    }
    const r = app.engine.validate(j);
    if (!r.ok) { toast(r.message); return; }
    if (!(await ask(`Replace the current levels with ${r.count} levels from ${file.name}? You can undo this.`, 'Replace'))) return;
    this.pushUndo();
    this.idx = 0;
    this.setCampaign(j);
    this.changed();
  },
  async copyJson() {
    try { await navigator.clipboard.writeText(JSON.stringify(this.camp, null, 2)); toast('Campaign JSON copied'); }
    catch (e) { toast('Copy is blocked here. Use Export instead.'); }
  },
  blankLevel() { return { id: newId(), name: 'New level', tension: 40, par: 20, enemies: [{ pos: 650, dir: -1, speed: 1 }] }; },
  addLevel() {
    if (this.camp.levels.length >= LIMITS.levels) { toast(`A campaign holds up to ${LIMITS.levels} levels`); return; }
    this.edit((L, c) => { c.levels.splice(this.idx + 1, 0, this.blankLevel()); this.idx++; this.sel = null; });
    setTimeout(() => { const n = $('.lvl-name'); if (n) { n.focus(); n.select(); } }, 30);
  },
  move(from, to) {
    if (to < 0 || to >= this.camp.levels.length || from === to) return;
    this.edit((L, c) => { const [l] = c.levels.splice(from, 1); c.levels.splice(to, 0, l); this.idx = to; });
  },

  // ---------------------------------------------------------- preview on the strip
  preview(now) {
    clearTimeout(this.previewTimer);
    const go = () => {
      if (!this.visible || !this.level()) return;
      app.startPreview(this.level(), this.spectate);
    };
    if (now) go(); else this.previewTimer = setTimeout(go, 380);
  },
  notePreviewDeath(pos) {
    const L = this.level();
    if (!L) return;
    const a = (this.previewDeaths[L.id] = this.previewDeaths[L.id] || new Array(20).fill(0));
    a[clamp(Math.floor(pos / 50), 0, 19)]++;
    this.renderHeat();
  },

  // ---------------------------------------------------------- rendering
  render() {
    if (!this.root) return;
    $('#campName').value = this.camp.name || '';
    this.renderList();
    this.renderDetail();
    this.renderStatus();
  },

  renderList() {
    const ul = $('#edList');
    ul.innerHTML = '';
    this.camp.levels.forEach((l, i) => {
      const tags = [];
      if (l.boss) tags.push('--led-boss');
      if ((l.lava || []).length) tags.push('--led-lava');
      if ((l.zones || []).some((z) => z.type === 'dark')) tags.push('--led-dark');
      if ((l.zones || []).some((z) => z.type === 'ice')) tags.push('--led-ice');
      const kinds = new Set((l.enemies || []).map((e) => e.kind || 'basic'));
      ['armored', 'chaser', 'splitter', 'phantom'].forEach((k) => kinds.has(k) && tags.push(KIND_VAR[k]));
      const li = h('li', { 'aria-current': i === this.idx ? 'true' : null, class: l.enabled === false ? 'off' : null, tabindex: 0,
        onclick: () => { if (this.idx !== i) { this.idx = i; this.sel = null; this.render(); this.preview(true); } },
        onkeydown: (e) => {
          if (e.key === 'Enter') li.click();
          if (e.altKey && e.key === 'ArrowUp') { e.preventDefault(); this.move(i, i - 1); }
          if (e.altKey && e.key === 'ArrowDown') { e.preventDefault(); this.move(i, i + 1); }
        } },
      h('span', { class: 'grip', title: 'Drag to reorder', 'aria-hidden': 'true' }, '⋮⋮'),
      h('span', { class: 'num' }, i + 1),
      h('span', { class: 'nm' }, l.name || 'Untitled'),
      h('span', { class: 'tags' }, tags.slice(0, 4).map((t) => h('i', { style: { background: `var(${t})` } }))));
      this.bindReorder(li.querySelector('.grip'), i);
      ul.append(li);
    });
    const cur = ul.children[this.idx];
    if (cur && cur.scrollIntoView) cur.scrollIntoView({ block: 'nearest' });
  },

  bindReorder(grip, i) {
    grip.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      e.stopPropagation();
      grip.setPointerCapture(e.pointerId);
      const items = $$('#edList li');
      let target = i;
      const move = (ev) => {
        target = items.length;
        for (let k = 0; k < items.length; k++) {
          const r = items[k].getBoundingClientRect();
          if (ev.clientY < r.top + r.height / 2) { target = k; break; }
        }
        items.forEach((it, k) => it.classList.toggle('drop-before', k === target && k !== i && k !== i + 1));
      };
      const up = () => {
        grip.removeEventListener('pointermove', move);
        grip.removeEventListener('pointerup', up);
        grip.removeEventListener('pointercancel', up);
        items.forEach((it) => it.classList.remove('drop-before'));
        const to = target > i ? target - 1 : target;
        if (to !== i) this.move(i, to);
      };
      grip.addEventListener('pointermove', move);
      grip.addEventListener('pointerup', up);
      grip.addEventListener('pointercancel', up);
    });
  },

  renderDetail() {
    const d = $('#edDetail');
    d.innerHTML = '';
    const L = this.level();
    if (!L) {
      d.append(h('p', { class: 'muted' }, 'No levels yet. Add one to start building.'));
      return;
    }
    const field = (label, input, hint) => h('label', { class: 'field' }, h('span', null, label), input, hint ? h('span', { class: 'hint' }, hint) : null);
    const num = (key, min, max, def, step = 1) => h('input', { type: 'number', min, max, step, value: L[key] ?? def, inputmode: 'numeric',
      onchange: (e) => this.edit((LL) => { LL[key] = clamp(+e.target.value || 0, min, max); }) });
    const i = this.idx;
    d.append(
      h('input', { class: 'lvl-name', value: L.name || '', maxlength: 31, 'aria-label': 'Level name', placeholder: 'Name this level',
        onchange: (e) => this.edit((LL) => { LL.name = e.target.value.slice(0, 31); }) }),
      h('div', { class: 'row', style: { marginBottom: '14px' } },
        h('button', { class: 'btn small', 'aria-pressed': this.spectate ? 'true' : 'false', onclick: () => { this.spectate = true; this.preview(true); this.renderDetail(); } }, 'Watch'),
        h('button', { class: 'btn small', 'aria-pressed': this.spectate ? 'false' : 'true', onclick: () => { this.spectate = false; this.preview(true); this.renderDetail(); } }, 'Play it'),
        app.mode === 'device' ? h('button', { class: 'btn small', onclick: () => this.testOnStrip() }, 'Test on strip') : null,
        h('span', { style: { flex: 1 } }),
        h('button', { class: 'btn small ghost', onclick: () => this.move(i, i - 1), disabled: i === 0 ? true : null }, 'Move up'),
        h('button', { class: 'btn small ghost', onclick: () => this.move(i, i + 1), disabled: i === this.camp.levels.length - 1 ? true : null }, 'Move down'),
        h('button', { class: 'btn small ghost', onclick: () => this.edit((LL, c) => { const copy = clone(LL); copy.id = newId(); copy.name = (copy.name || 'Level') + ' copy'; c.levels.splice(i + 1, 0, copy); this.idx = i + 1; }) }, 'Duplicate'),
        h('button', { class: 'btn small danger', onclick: async () => { if (this.camp.levels.length > 1 && await ask(`Delete "${L.name}"? You can undo this.`, 'Delete', true)) this.edit((LL, c) => { c.levels.splice(i, 1); this.idx = clamp(i, 0, c.levels.length - 1); this.sel = null; }); } }, 'Delete')),
      h('p', { class: 'small muted', style: { marginTop: '-6px' } }, this.spectate
        ? 'Watching: the level loops on the strip above while you edit. You are an invincible ghost, so you can still walk around with the arrow keys.'
        : 'Playing: the level restarts whenever you change something. Lives are unlimited.'),
      h('div', { class: 'grid' },
        field('Player starts at', num('player', 0, 999, 0)),
        field('Twang width', num('attackWidth', 10, 300, 70), 'The original is 70.'),
        field('Music', h('select', { onchange: (e) => this.edit((LL) => { LL.music = e.target.value; }) },
          MUSIC_OPTS.map(([v, l]) => h('option', { value: v, selected: (L.music || 'auto') === v ? true : null }, l)))),
        field(`Tension ${L.tension ?? 30}`, h('input', { type: 'range', min: 0, max: 100, value: L.tension ?? 30,
          oninput: (e) => { e.target.previousSibling.textContent = `Tension ${e.target.value}`; },
          onchange: (e) => this.edit((LL) => { LL.tension = +e.target.value; }) }), 'How heavy the music sits before anything gets close.'),
        field('Par time (seconds)', num('par', 0, 3600, 0), 'Beat it for a time bonus. 0 turns it off.'),
        h('div', null,
          h('label', { class: 'switch' }, h('input', { type: 'checkbox', checked: L.enabled === false ? null : true, onchange: (e) => this.edit((LL) => { if (e.target.checked) delete LL.enabled; else LL.enabled = false; }) }), 'In the campaign'),
          h('label', { class: 'switch' }, h('input', { type: 'checkbox', checked: L.safe ? true : null, onchange: (e) => this.edit((LL) => { if (e.target.checked) LL.safe = true; else delete LL.safe; }) }), 'Practice level (no lives lost, no score)'))),
      this.renderTrack(L),
      h('div', { class: 'adds' }, ADDS.map(([label, color, fn, type]) => h('button', { class: 'chip', onclick: () => this.add(fn, type, label) }, h('i', { style: { background: `var(${color})` } }), label)),
        h('button', { class: 'chip', onclick: () => this.toggleBoss() }, h('i', { style: { background: 'var(--led-boss)' } }), bossOf(L) ? 'Remove boss' : 'Boss')),
      h('div', { class: 'panel inspector', id: 'edInspector' }),
      h('ul', { class: 'issues', id: 'edIssues' }),
    );
    this.renderInspector();
    this.renderIssues();
  },

  add(fn, type, label) {
    const L = this.level();
    const key = LIMIT_OF[type];
    if (key && (L[key] || []).length >= LIMITS[key]) { toast(`A level holds up to ${LIMITS[key]} ${key}`); return; }
    this.edit((LL) => { fn(LL); this.sel = { type, i: list(LL, TYPE_LIST[type]).length - 1 }; });
    toast(`${label} added. Drag it on the track.`);
  },
  toggleBoss() {
    this.edit((L) => {
      if (bossOf(L)) { delete L.boss; this.sel = null; }
      else { L.boss = { hp: 3, positions: [800, 200, 600], rates: [1800, 1600, 1000], speed: 3, width: 40, rage: false }; this.sel = { type: 'boss', i: 0 }; }
    });
  },
  async testOnStrip() {
    try {
      await app.dev.api('/api/test', { body: { level: this.level(), spectate: false } });
      toast('Playing it on the strip. Grab the spring.');
    } catch (e) { toast(e.message); }
  },

  // ---------------------------------------------------------- the track
  renderTrack(L) {
    const wrap = h('div', { class: 'track', id: 'edTrack' });
    const ruler = h('div', { class: 'ruler' });
    for (let p = 0; p <= 1000; p += 100) ruler.append(h('span', { style: { left: p / 10 + '%' }, class: p % 200 ? 'odd' : null }, p % 200 && window.innerWidth < 560 ? '' : p));
    wrap.append(ruler);
    const B = bossOf(L);
    for (const [lane, label] of LANES) {
      if (lane === 'boss' && !B) continue;
      const bed = h('div', { class: 'bed', 'data-lane': lane });
      wrap.append(h('div', { class: 'lane' }, h('b', null, label), bed));
      this.fillLane(bed, lane, L);
    }
    wrap.append(h('div', { class: 'heat', id: 'edHeat', title: 'Where players died' }));
    setTimeout(() => this.renderHeat(), 0);
    return wrap;
  },

  fillLane(bed, lane, L) {
    const sel = this.sel;
    const isSel = (type, i) => sel && sel.type === type && sel.i === i;
    const pct = (v) => clamp(v, 0, 1000) / 10 + '%';
    const mk = (cls, type, i, style, text, title) => {
      const el = h('div', { class: `ent ${cls}${isSel(type, i) ? ' sel' : ''}`, tabindex: 0, role: 'button', 'aria-label': title, title, style }, text || null);
      this.bindDrag(el, type, i);
      return el;
    };
    if (lane === 'player') {
      bed.append(h('div', { class: 'marker-exit', title: 'Exit' }));
      bed.append(mk('dot', 'player', 0, { left: pct(L.player || 0), background: 'var(--led-player)' }, '', `Player start at ${L.player || 0}`));
    } else if (lane === 'terrain') {
      (L.zones || []).forEach((z, i) => {
        const a = Math.min(z.start, z.end), b = Math.max(z.start, z.end);
        const el = mk('bar', 'zone', i, { left: pct(a), width: (b - a) / 10 + '%', background: z.type === 'dark' ? 'var(--led-dark)' : 'var(--led-ice)', color: z.type === 'dark' ? '#fff' : '#08070a' },
          z.type === 'dark' ? 'Dark' : 'Ice', `${z.type === 'dark' ? 'Darkness' : 'Ice'} ${a}-${b}`);
        el.append(h('span', { class: 'h l' }), h('span', { class: 'h r' }));
        bed.append(el);
      });
      (L.conveyors || []).forEach((c, i) => {
        const a = Math.min(c.start, c.end), b = Math.max(c.start, c.end);
        const arrows = (c.speed > 0 ? '›' : '‹').repeat(clamp(Math.abs(c.speed), 1, 9));
        const el = mk('bar', 'conveyor', i, { left: pct(a), width: (b - a) / 10 + '%', background: 'var(--led-conveyor)', color: '#fff', top: '15px', height: '12px', lineHeight: '12px' },
          arrows, `Conveyor ${a}-${b}, speed ${c.speed}`);
        el.append(h('span', { class: 'h l' }), h('span', { class: 'h r' }));
        bed.append(el);
      });
    } else if (lane === 'lava') {
      (L.lava || []).forEach((l, i) => {
        const a = Math.min(l.left, l.right), b = Math.max(l.left, l.right);
        const extra = (l.grow ? ' grows' : '') + (l.flow ? (l.flow > 0 ? ' drifts →' : ' drifts ←') : '');
        const el = mk('bar', 'lava', i, { left: pct(a), width: (b - a) / 10 + '%', background: 'var(--led-lava)' },
          `${((l.on ?? 2000) / 1000).toFixed(1)}/${((l.off ?? 2000) / 1000).toFixed(1)}s${extra}`, `Lava ${a}-${b}, on ${l.on} ms, off ${l.off} ms`);
        el.append(h('span', { class: 'h l' }), h('span', { class: 'h r' }));
        bed.append(el);
      });
    } else if (lane === 'enemies') {
      (L.enemies || []).forEach((e, i) => {
        const k = e.kind || 'basic';
        if (e.wobble) bed.append(h('div', { class: 'swing', style: { left: pct(e.pos - e.wobble), width: (Math.min(1000, e.pos + e.wobble) - Math.max(0, e.pos - e.wobble)) / 10 + '%', background: `var(${KIND_VAR[k]})` } }));
        bed.append(mk('dot', 'enemy', i, { left: pct(e.pos), background: `var(${KIND_VAR[k]})` }, '',
          `${KIND_LABEL[k]} at ${e.pos}${e.wobble ? `, swings ${e.wobble}` : e.speed ? `, moves ${e.speed}` : ''}`));
      });
    } else if (lane === 'spawners') {
      (L.spawners || []).forEach((s, i) => {
        const k = s.kind || 'basic';
        bed.append(mk('num', 'spawner', i, { left: pct(s.pos), background: `var(${KIND_VAR[k]})` }, s.dir > 0 ? '▸' : '◂', `Spawner at ${s.pos}, every ${s.rate} ms`));
      });
    } else if (lane === 'pickups') {
      (L.pickups || []).forEach((p, i) => {
        bed.append(p.type === 'life'
          ? mk('dot', 'pickup', i, { left: pct(p.pos), background: 'var(--led-life)' }, '', `Extra life at ${p.pos}`)
          : mk('gem', 'pickup', i, { left: pct(p.pos), background: 'var(--led-gem)' }, '', `Gem at ${p.pos}`));
      });
      (L.checkpoints || []).forEach((c, i) => {
        bed.append(mk('flag', 'checkpoint', i, { left: pct(c), background: 'var(--led-check)', color: 'var(--led-check)' }, '', `Checkpoint at ${c}`));
      });
    } else if (lane === 'boss') {
      const B = bossOf(L);
      const hp = clamp(B.hp || 3, 1, 6);
      for (let k = 0; k < hp; k++) {
        const p = (B.positions || [])[k] ?? BOSS_POS[k];
        bed.append(mk('num', 'boss', k, { left: pct(p), background: 'var(--led-boss)', color: '#fff' }, String(k + 1), `Boss stands here for hit ${k + 1}`));
      }
    }
  },

  // read or write an entity's position(s)
  getPos(L, type, i) {
    switch (type) {
      case 'player': return [L.player || 0];
      case 'enemy': return [L.enemies[i].pos];
      case 'spawner': return [L.spawners[i].pos];
      case 'pickup': return [L.pickups[i].pos];
      case 'checkpoint': return [L.checkpoints[i]];
      case 'boss': { const B = bossOf(L); if (!B.positions) B.positions = BOSS_POS.slice(); return [B.positions[i] ?? BOSS_POS[i]]; }
      case 'lava': return [L.lava[i].left, L.lava[i].right];
      case 'conveyor': return [L.conveyors[i].start, L.conveyors[i].end];
      case 'zone': return [L.zones[i].start, L.zones[i].end];
    }
    return [0];
  },
  setPos(L, type, i, a, b) {
    switch (type) {
      case 'player': L.player = clamp(a, 0, 999); break;
      case 'enemy': L.enemies[i].pos = clamp(a, 0, 1000); break;
      case 'spawner': L.spawners[i].pos = clamp(a, 0, 1000); break;
      case 'pickup': L.pickups[i].pos = clamp(a, 0, 999); break;
      case 'checkpoint': L.checkpoints[i] = clamp(a, 1, 999); break;
      case 'boss': { const B = bossOf(L); if (!B.positions) B.positions = BOSS_POS.slice(); while (B.positions.length <= i) B.positions.push(BOSS_POS[B.positions.length]); B.positions[i] = clamp(a, 60, 940); break; }
      case 'lava': L.lava[i].left = a; L.lava[i].right = b; break;
      case 'conveyor': L.conveyors[i].start = a; L.conveyors[i].end = b; break;
      case 'zone': L.zones[i].start = a; L.zones[i].end = b; break;
    }
  },

  bindDrag(el, type, i) {
    const select = () => {
      this.sel = { type, i };
      $$('#edTrack .ent.sel').forEach((x) => x.classList.remove('sel'));
      el.classList.add('sel');
      this.renderInspector();
    };
    el.addEventListener('keydown', (e) => {
      const L = this.level();
      if (e.key === 'Delete' || e.key === 'Backspace') { e.preventDefault(); this.sel = { type, i }; this.removeSelected(); return; }
      if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); select(); } return; }
      e.preventDefault();
      const step = (e.shiftKey ? 25 : 5) * (e.key === 'ArrowLeft' ? -1 : 1);
      const [a, b] = this.getPos(L, type, i);
      this.sel = { type, i };
      this.edit((LL) => this.setPos(LL, type, i, clamp(a + step, 0, 1000), b != null ? clamp(b + step, 0, 1000) : undefined));
      setTimeout(() => { const n = $('#edTrack .ent.sel'); if (n) n.focus(); }, 0);
    });
    el.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      select();
      const bed = el.parentElement;
      const rect = bed.getBoundingClientRect();
      const world = (x) => (x - rect.left) / rect.width * 1000;
      const handle = e.target.classList.contains('h') ? (e.target.classList.contains('l') ? 'l' : 'r') : null;
      const L = this.level();
      const [a0, b0] = this.getPos(L, type, i);
      const grab = world(e.clientX) - a0;
      const x0 = e.clientX;
      let dragging = false;
      el.setPointerCapture(e.pointerId);
      const move = (ev) => {
        if (!dragging) {
          if (Math.abs(ev.clientX - x0) < 3) return;
          dragging = true;
          this.pushUndo();
        }
        const snap = ev.altKey || ev.shiftKey ? 1 : 5;
        const q = (v) => Math.round(v / snap) * snap;
        const w = world(ev.clientX);
        if (b0 == null) {
          this.setPos(L, type, i, q(clamp(w - grab, 0, 1000)));
          const [p] = this.getPos(L, type, i);
          el.style.left = p / 10 + '%';
          el.title = el.title.replace(/\d+/, p);
        } else {
          let a = a0, b = b0;
          const lo = Math.min(a0, b0), hi = Math.max(a0, b0);
          if (handle === 'l') { a = clamp(q(w), 0, hi - 10); b = hi; }
          else if (handle === 'r') { a = lo; b = clamp(q(w), lo + 10, 1000); }
          else { const width = hi - lo; a = clamp(q(w - grab), 0, 1000 - width); b = a + width; }
          this.setPos(L, type, i, a, b);
          el.style.left = a / 10 + '%';
          el.style.width = (b - a) / 10 + '%';
        }
        if (type === 'enemy' && L.enemies[i].wobble) {
          const sw = el.previousSibling;
          if (sw && sw.classList.contains('swing')) {
            const e2 = L.enemies[i];
            sw.style.left = clamp(e2.pos - e2.wobble, 0, 1000) / 10 + '%';
            sw.style.width = (Math.min(1000, e2.pos + e2.wobble) - Math.max(0, e2.pos - e2.wobble)) / 10 + '%';
          }
        }
        this.renderInspectorValues();
      };
      const up = () => {
        el.removeEventListener('pointermove', move);
        el.removeEventListener('pointerup', up);
        el.removeEventListener('pointercancel', up);
        if (dragging) this.changed({ render: false }), this.renderDetail();
      };
      el.addEventListener('pointermove', move);
      el.addEventListener('pointerup', up);
      el.addEventListener('pointercancel', up);
    });
  },

  removeSelected() {
    const s = this.sel;
    if (!s || s.type === 'player') return;
    this.edit((L) => {
      if (s.type === 'boss') {
        const B = bossOf(L);
        if (B.hp > 1) { B.hp--; (B.positions || []).splice(s.i, 1); (B.rates || []).splice(s.i, 1); } else delete L.boss;
      } else list(L, TYPE_LIST[s.type]).splice(s.i, 1);
      this.sel = null;
    });
  },

  // ---------------------------------------------------------- inspector
  renderInspector() {
    const box = $('#edInspector');
    if (!box) return;
    box.innerHTML = '';
    const L = this.level();
    const s = this.sel;
    if (!s) {
      box.append(h('p', { class: 'small muted', style: { margin: 0 } }, 'Select something on the track to change it. Drag markers to move them, drag the ends of a bar to resize. Arrow keys nudge the selected marker, Delete removes it.'));
      return;
    }
    const num = (obj, key, label, min, max, def, step = 1, hint) => h('label', { class: 'field' }, h('span', null, label),
      h('input', { type: 'number', min, max, step, value: obj[key] ?? def, 'data-k': key, inputmode: 'decimal',
        onchange: (e) => this.edit(() => { obj[key] = clamp(+e.target.value || 0, min, max); }) }),
      hint ? h('span', { class: 'hint' }, hint) : null);
    const sel = (obj, key, label, opts, def) => h('label', { class: 'field' }, h('span', null, label),
      h('select', { onchange: (e) => this.edit(() => { const v = e.target.value; obj[key] = /^-?\d+$/.test(v) ? +v : v; }) },
        opts.map(([v, l]) => h('option', { value: v, selected: String(obj[key] ?? def) === String(v) ? true : null }, l))));
    const sw = (obj, key, label) => h('label', { class: 'switch' }, h('input', { type: 'checkbox', checked: obj[key] ? true : null, onchange: (e) => this.edit(() => { obj[key] = e.target.checked; }) }), label);
    const title = (text, color) => h('h3', null, h('i', { style: { background: `var(${color})` } }), text,
      s.type !== 'player' ? h('button', { class: 'btn small danger', style: { marginLeft: 'auto' }, onclick: () => this.removeSelected() }, 'Remove') : null);
    const dirOpts = [[-1, 'Toward the start'], [1, 'Toward the exit']];
    const kindOpts = ['basic', 'armored', 'chaser', 'splitter', 'phantom'].map((k) => [k, KIND_LABEL[k]]);
    let body;
    switch (s.type) {
      case 'player':
        body = [title('Player start', '--led-player'), h('div', { class: 'grid' }, num(L, 'player', 'Position', 0, 999, 0))];
        break;
      case 'enemy': {
        const e = L.enemies[s.i];
        const k = e.kind || 'basic';
        const blurb = { basic: 'Red. Twang it once.', armored: 'Takes two twangs. The first knocks it back and stuns it.', chaser: 'Homes in on the player. Speed 1 is plenty.',
          splitter: 'Bursts into two fragments further away that come at you.', phantom: 'Fades in and out. Faded, it passes through you and your twang passes through it.' }[k];
        body = [title(KIND_LABEL[k], KIND_VAR[k]), h('p', { class: 'small muted' }, blurb),
          h('div', { class: 'grid' }, sel(e, 'kind', 'Kind', kindOpts, 'basic'), num(e, 'pos', 'Position', 0, 1000, 500),
            k !== 'chaser' ? sel(e, 'dir', 'Moves', dirOpts, -1) : null, num(e, 'speed', 'Speed', 0, 20, 0, 1, 'Units per frame. 1 to 4 is typical.'),
            k !== 'chaser' ? num(e, 'wobble', 'Swing', 0, 1000, 0, 5, '0 moves in a line. Above 0 it swings this far each way, and speed sets how fast.') : null)];
        break;
      }
      case 'spawner': {
        const sp = L.spawners[s.i];
        body = [title('Spawner', KIND_VAR[sp.kind || 'basic']), h('p', { class: 'small muted' }, 'Sends a fresh enemy every few seconds, forever. It glows just before each one.'),
          h('div', { class: 'grid' }, sel(sp, 'kind', 'Spawns', kindOpts, 'basic'), num(sp, 'pos', 'Position', 0, 1000, 1000), sel(sp, 'dir', 'Sends them', dirOpts, -1),
            num(sp, 'rate', 'Every (ms)', 250, 60000, 3000, 100), num(sp, 'speed', 'Speed', 0, 20, 2), num(sp, 'delay', 'First one after (ms)', 0, 60000, 0, 100))];
        break;
      }
      case 'lava': {
        const l = L.lava[s.i];
        body = [title('Lava', '--led-lava'), h('p', { class: 'small muted' }, 'Burns anything standing in it while it is on. It flickers for a moment before it ignites.'),
          h('div', { class: 'grid' }, num(l, 'left', 'From', 0, 1000, 400), num(l, 'right', 'To', 0, 1000, 500),
            num(l, 'on', 'On for (ms)', 0, 60000, 2000, 100), num(l, 'off', 'Off for (ms)', 0, 60000, 2000, 100),
            num(l, 'offset', 'Head start (ms)', 0, 60000, 0, 100, 'Shifts this pool’s cycle so several pools can ripple.'),
            num(l, 'grow', 'Grows per frame', 0, 2, 0, 0.05, '0.1 to 0.5 is reasonable.'), num(l, 'flow', 'Drifts per frame', -3, 3, 0, 0.05, 'Positive drifts toward the exit.')),
          sw(l, 'startOn', 'Starts burning')];
        break;
      }
      case 'conveyor': {
        const c = L.conveyors[s.i];
        body = [title('Conveyor', '--led-conveyor'), h('div', { class: 'grid' }, num(c, 'start', 'From', 0, 1000, 300), num(c, 'end', 'To', 0, 1000, 600),
          num(c, 'speed', 'Push', -9, 9, -4, 1, 'Negative pushes back toward the start. The player tops out at 10.'))];
        break;
      }
      case 'zone': {
        const z = L.zones[s.i];
        body = [title(z.type === 'dark' ? 'Darkness' : 'Ice', z.type === 'dark' ? '--led-dark' : '--led-ice'),
          h('p', { class: 'small muted' }, z.type === 'dark' ? 'Inside, only what is near the player is lit. Burning lava glows faintly.' : 'Slippery: the player keeps momentum and slides, even while twanging.'),
          h('div', { class: 'grid' }, sel(z, 'type', 'Kind', [['ice', 'Ice'], ['dark', 'Darkness']], 'ice'), num(z, 'start', 'From', 0, 1000, 300), num(z, 'end', 'To', 0, 1000, 600))];
        break;
      }
      case 'pickup': {
        const p = L.pickups[s.i];
        body = [title(p.type === 'life' ? 'Extra life' : 'Gem', p.type === 'life' ? '--led-life' : '--led-gem'),
          h('div', { class: 'grid' }, sel(p, 'type', 'Kind', [['gem', 'Gem (+50)'], ['life', 'Extra life']], 'gem'), num(p, 'pos', 'Position', 0, 999, 500))];
        break;
      }
      case 'checkpoint': {
        const wrapper = { pos: L.checkpoints[s.i] };
        body = [title('Checkpoint', '--led-check'), h('p', { class: 'small muted' }, 'After passing it, dying brings the player back here instead of the start.'),
          h('div', { class: 'grid' }, h('label', { class: 'field' }, h('span', null, 'Position'),
            h('input', { type: 'number', min: 1, max: 999, value: wrapper.pos, 'data-k': 'pos', onchange: (e) => this.edit((LL) => { LL.checkpoints[s.i] = clamp(+e.target.value || 500, 1, 999); }) })))];
        break;
      }
      case 'boss': {
        const B = bossOf(L);
        const hp = clamp(B.hp || 3, 1, 6);
        if (!B.positions) B.positions = BOSS_POS.slice(0, hp);
        if (!B.rates) B.rates = BOSS_RATE.slice(0, hp);
        const rows = [];
        for (let k = 0; k < hp; k++) {
          rows.push(h('tr', null, h('td', { class: 'muted' }, `Hit ${k + 1}`),
            h('td', null, h('input', { type: 'number', min: 60, max: 940, value: B.positions[k] ?? BOSS_POS[k], 'aria-label': `Position for hit ${k + 1}`, style: { width: '6em' },
              onchange: (e) => this.edit(() => { while (B.positions.length <= k) B.positions.push(BOSS_POS[B.positions.length]); B.positions[k] = clamp(+e.target.value, 60, 940); }) })),
            h('td', null, h('input', { type: 'number', min: 300, max: 60000, step: 100, value: B.rates[k] ?? BOSS_RATE[k], 'aria-label': `Minion interval for hit ${k + 1}`, style: { width: '6em' },
              onchange: (e) => this.edit(() => { while (B.rates.length <= k) B.rates.push(BOSS_RATE[B.rates.length]); B.rates[k] = clamp(+e.target.value, 300, 60000); }) }))));
        }
        body = [title('Boss', '--led-boss'), h('p', { class: 'small muted' }, 'After each hit it moves to the next spot and spits minions both ways. The exit opens when it dies.'),
          h('div', { class: 'grid' }, num(B, 'hp', 'Hits to kill', 1, 6, 3), num(B, 'width', 'Width', 10, 200, 40), num(B, 'speed', 'Minion speed', 0, 12, 3)),
          sw(B, 'rage', 'Rage: in its last phase it erupts in lava around itself (with a warning glow)'),
          h('table', { style: { marginTop: '8px', maxWidth: '420px' } }, h('thead', null, h('tr', null, h('th', null, ''), h('th', null, 'Stands at'), h('th', null, 'Minion every (ms)'))), h('tbody', null, rows))];
        break;
      }
      default: body = [];
    }
    box.append(...body.filter(Boolean));
  },
  renderInspectorValues() {
    const s = this.sel;
    if (!s) return;
    const [a, b] = this.getPos(this.level(), s.type, s.i);
    const box = $('#edInspector');
    const map = { lava: ['left', 'right'], conveyor: ['start', 'end'], zone: ['start', 'end'] }[s.type] || [s.type === 'player' ? 'player' : 'pos'];
    const f0 = box.querySelector(`[data-k="${map[0]}"]`);
    if (f0) f0.value = a;
    if (map[1]) { const f1 = box.querySelector(`[data-k="${map[1]}"]`); if (f1) f1.value = b; }
  },

  renderIssues() {
    const ul = $('#edIssues');
    if (!ul) return;
    ul.innerHTML = '';
    for (const it of issues(this.level())) ul.append(h('li', { class: it.sev === 'err' ? 'err' : null }, it.msg));
  },

  renderHeat() {
    const el = $('#edHeat');
    const L = this.level();
    if (!el || !L) return;
    el.innerHTML = '';
    const st = (app.stats && app.stats.levels && app.stats.levels[L.id]) || null;
    const heat = new Array(20).fill(0);
    if (st && st.heat) st.heat.forEach((v, i) => { heat[i] += v; });
    const pv = this.previewDeaths[L.id];
    if (pv) pv.forEach((v, i) => { heat[i] += v; });
    const max = Math.max(...heat);
    const total = heat.reduce((a, b) => a + b, 0);
    heat.forEach((v, i) => el.append(h('i', { style: { opacity: max ? (v / max) * 0.9 : 0 }, title: `${v} deaths between ${i * 50} and ${i * 50 + 50}` })));
    el.title = total ? `${total} deaths recorded on this level` : 'No deaths recorded yet';
  },
};

function slug(s) { return String(s).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'campaign'; }
