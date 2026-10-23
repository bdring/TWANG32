/* TWANG Next web UI - app core.
   Runs in two modes:
     device    - served by the ESP32; the strip at the top mirrors the real strip
     simulator - anywhere else; the same game engine runs here as WebAssembly
*/
'use strict';

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  if (attrs) for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'html') el.innerHTML = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const k of kids.flat()) if (k != null && k !== false) el.append(k.nodeType ? k : document.createTextNode(k));
  return el;
}
const fmt = (n) => Math.round(n).toLocaleString('en-US');
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const store = {
  get(k, d) { try { const v = localStorage.getItem('twang.' + k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem('twang.' + k, JSON.stringify(v)); } catch (e) { /* private mode */ } },
};

// In-page confirmation (window.confirm is blocked in some embeds, and this reads better on a phone)
function ask(message, okLabel = 'Continue', danger = false) {
  return new Promise((resolve) => {
    const dlg = document.getElementById('askDlg');
    document.getElementById('askMsg').textContent = message;
    const ok = document.getElementById('askOk');
    ok.textContent = okLabel;
    ok.className = 'btn ' + (danger ? 'danger' : 'primary');
    const done = (v) => { dlg.close(); resolve(v); };
    ok.onclick = () => done(true);
    document.getElementById('askCancel').onclick = () => done(false);
    dlg.oncancel = (e) => { e.preventDefault(); done(false); };
    try { dlg.showModal(); } catch (e) { dlg.setAttribute('open', ''); }
    ok.focus();
  });
}

let toastTimer = 0;
function toast(msg) {
  let t = $('.toast');
  if (!t) { t = h('div', { class: 'toast', role: 'status' }); document.body.append(t); }
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 2600);
}

// ----------------------------------------------------------- mirrors of the C++ enums
const EV = { RUN_START: 1, LEVEL_START: 2, ATTACK: 3, KILL: 4, ARMOR_HIT: 5, SPLIT: 6, SPAWN: 7, DIE: 8, LEVEL_CLEAR: 9,
  BOSS_HIT: 10, BOSS_KILLED: 11, RAGE: 12, GEM: 13, LIFE: 14, CHECKPOINT: 15, LAVA_WARN: 16, LAVA_IGNITE: 17,
  GAME_OVER: 18, NEW_HIGH: 19, CAMPAIGN_DONE: 20, ATTRACT: 21, STAGE: 22 };
const STAGE_LABEL = ['Waking up', 'Get ready', 'Playing', 'Down', 'Level clear', 'Boss defeated', 'Game over', 'Final score', 'Screensaver'];
const MODES = ['Campaign', 'Endless', 'Practice', 'Level test'];
const KINDS = ['basic', 'armored', 'chaser', 'splitter', 'phantom', 'mini'];
const KIND_LABEL = { basic: 'Enemy', armored: 'Armored', chaser: 'Chaser', splitter: 'Splitter', phantom: 'Phantom', mini: 'Fragment' };
const KIND_VAR = { basic: '--led-enemy', armored: '--led-armored', chaser: '--led-chaser', splitter: '--led-splitter', phantom: '--led-phantom', mini: '--led-mini' };
const TRACKS = ['descent', 'furnace', 'reckoning', 'abyss'];
const TRACK_LABEL = { descent: 'Descent', furnace: 'Furnace', reckoning: 'Reckoning', abyss: 'Abyss' };
const cssVar = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();

// ----------------------------------------------------------- the engine (WebAssembly)
class Engine {
  async load() {
    const b64 = window.TWANG_DATA.wasm;
    const bin = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const { instance } = await WebAssembly.instantiate(bin, {});
    this.x = instance.exports;
    this.x.__wasm_call_ctors();
    this.enc = new TextEncoder();
    this.x.tw_init(22050, (Math.random() * 1e9) >>> 0);
    this.sampleRate = 22050;
  }
  mem() { return this.x.memory.buffer; }
  text(str) {
    const bytes = this.enc.encode(str);
    const cap = this.x.tw_text_capacity();
    if (bytes.length >= cap) throw new Error('Campaign is too large (' + bytes.length + ' bytes)');
    new Uint8Array(this.mem(), this.x.tw_text_buffer(), cap).set(bytes);
    return bytes.length;
  }
  str(ptr) {
    const m = new Uint8Array(this.mem());
    let s = '';
    while (m[ptr]) s += String.fromCharCode(m[ptr++]);
    return s;
  }
  result(n) { return { ok: n >= 0, count: n, message: this.str(this.x.tw_parse_message()), warnings: this.x.tw_parse_warnings() }; }
  loadCampaign(obj) { return this.result(this.x.tw_load_campaign(this.text(JSON.stringify(obj)))); }
  validate(obj) { return this.result(this.x.tw_validate_campaign(this.text(JSON.stringify(obj)))); }
  testLevel(level, spectate) { return this.result(this.x.tw_test_level(this.text(JSON.stringify(level)), spectate ? 1 : 0)); }
  tick(inp) {
    this.x.tw_set_input(inp.tilt, inp.attack ? 1 : 0, inp.active ? 1 : 0);
    const n = this.x.tw_tick();
    const out = [];
    for (let i = 0; i < n; i++) out.push({ type: this.x.tw_event_type(i), kind: this.x.tw_event_kind(i), pos: this.x.tw_event_pos(i), value: this.x.tw_event_value(i) });
    return out;
  }
  render(n) { return new Uint8Array(this.mem(), this.x.tw_render(n), n * 3); }
  audio(n) { return new Float32Array(this.mem(), this.x.tw_audio(n), n); }
  state() {
    const x = this.x;
    return { stage: x.tw_stage(), mode: x.tw_mode(), level: x.tw_level(), lives: x.tw_lives(), score: x.tw_score(),
      combo: x.tw_combo(), name: this.str(x.tw_level_name()), bossHp: x.tw_boss_hp(), paused: !!x.tw_paused(),
      track: x.tw_music_track(), intensity: x.tw_music_intensity() };
  }
  opt(id, v) { this.x.tw_set_option(id, v | 0); }
}
const OPT = { livesPerLevel: 0, livesPerRun: 1, idleTimeout: 2, juice: 3, smooth: 4, palette: 5, difficulty: 6, gravity: 7,
  telegraph: 8, attract: 9, moveSound: 10, music: 11, highScore: 12, defaultMode: 13, spectate: 14, attractAudio: 15 };

// ----------------------------------------------------------- the LED strip view
class StripView {
  constructor(canvas) {
    this.c = canvas;
    this.g = canvas.getContext('2d');
    this.small = document.createElement('canvas');
    this.glow = document.createElement('canvas');
    this.lut = new Uint8Array(256);
    // LEDs read much brighter to the eye than the same value on a screen: lift the low end
    for (let i = 0; i < 256; i++) this.lut[i] = i === 0 ? 0 : Math.round(255 * Math.pow(i / 255, 1 / 2.2));
    this.n = 0;
  }
  draw(rgb, n) {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const W = Math.round(this.c.clientWidth * dpr), H = Math.round(this.c.clientHeight * dpr);
    if (!W || !H) return;
    if (this.c.width !== W || this.c.height !== H) { this.c.width = W; this.c.height = H; }
    if (this.small.width !== n) { this.small.width = n; this.small.height = 1; this.img = this.small.getContext('2d').createImageData(n, 1); }
    const d = this.img.data, L = this.lut;
    for (let i = 0; i < n; i++) {
      d[i * 4] = L[rgb[i * 3]]; d[i * 4 + 1] = L[rgb[i * 3 + 1]]; d[i * 4 + 2] = L[rgb[i * 3 + 2]]; d[i * 4 + 3] = 255;
    }
    this.small.getContext('2d').putImageData(this.img, 0, 0);
    const g = this.g;
    g.globalCompositeOperation = 'source-over';
    g.fillStyle = '#07070a';
    g.fillRect(0, 0, W, H);
    const pitch = W / n;
    // copper pads of the tape, only when there is room for them
    if (pitch > 5) {
      g.fillStyle = '#141318';
      for (let i = 0; i < n; i++) g.fillRect(i * pitch + pitch * 0.2, H / 2 - pitch * 0.3, pitch * 0.6, pitch * 0.6);
    }
    // soft diffusion: the frame stretched across the tape, faded toward the edges
    if (this.glow.width !== W || this.glow.height !== H) { this.glow.width = W; this.glow.height = H; }
    const gg = this.glow.getContext('2d');
    gg.globalCompositeOperation = 'source-over';
    gg.clearRect(0, 0, W, H);
    gg.imageSmoothingEnabled = true;
    gg.drawImage(this.small, 0, 0, n, 1, -pitch / 2, 0, W + pitch, H);
    gg.globalCompositeOperation = 'destination-in';
    const grad = gg.createLinearGradient(0, 0, 0, H);
    grad.addColorStop(0, 'rgba(0,0,0,0)');
    grad.addColorStop(0.5, 'rgba(0,0,0,0.55)');
    grad.addColorStop(1, 'rgba(0,0,0,0)');
    gg.fillStyle = grad;
    gg.fillRect(0, 0, W, H);
    g.globalCompositeOperation = 'lighter';
    g.drawImage(this.glow, 0, 0);
    // the emitters themselves
    const w = Math.max(1, pitch * 0.62), hh = Math.max(3, Math.min(pitch * 0.62, 10 * dpr));
    for (let i = 0; i < n; i++) {
      const r = d[i * 4], gr = d[i * 4 + 1], b = d[i * 4 + 2];
      if (!(r | gr | b)) continue;
      g.fillStyle = `rgb(${r},${gr},${b})`;
      g.fillRect(i * pitch + (pitch - w) / 2, H / 2 - hh / 2, w, hh);
    }
    g.globalCompositeOperation = 'source-over';
  }
}

// ----------------------------------------------------------- input
class Inputs {
  constructor() {
    this.keys = new Set();
    this.touchTilt = 0;
    this.touchAttack = false;
    this.motionOn = false;
    this.motionTilt = 0;
    this.shakeUntil = 0;
    this.lastEvent = 0;
    const gameKeys = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', ' ', 'a', 'd', 'w', 'A', 'D', 'W', 'Shift', 'Enter'];
    const typing = (e) => /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName) || e.target.isContentEditable;
    window.addEventListener('keydown', (e) => {
      if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
      if (!gameKeys.includes(e.key)) return;
      if (e.target.tagName === 'BUTTON' && (e.key === ' ' || e.key === 'Enter')) return;
      this.keys.add(e.key.length === 1 ? e.key.toLowerCase() : e.key);
      this.lastEvent = performance.now();
      if (e.key.startsWith('Arrow') || e.key === ' ') e.preventDefault();
    });
    window.addEventListener('keyup', (e) => { this.keys.delete(e.key.length === 1 ? e.key.toLowerCase() : e.key); });
    window.addEventListener('blur', () => this.keys.clear());
    // A button clicked with a mouse or finger keeps focus, so the next Space (twang) would click it
    // again. Hand focus back to the page; keyboard users (click detail 0) keep their focus.
    document.addEventListener('click', (e) => {
      const b = e.target.closest && e.target.closest('button');
      if (b && e.detail > 0 && !b.closest('dialog')) b.blur();
    });
  }
  bindTouch(spring, button) {
    const knob = spring.querySelector('.knob');
    let id = null;
    const move = (e) => {
      const r = spring.getBoundingClientRect();
      const x = clamp((e.clientX - r.left) / r.width * 2 - 1, -1, 1);
      this.touchTilt = Math.abs(x) < 0.08 ? 0 : x * 66;
      knob.style.left = `calc(${(x + 1) * 50}% - 25px)`;
      this.lastEvent = performance.now();
    };
    spring.addEventListener('pointerdown', (e) => { id = e.pointerId; spring.setPointerCapture(id); move(e); });
    spring.addEventListener('pointermove', (e) => { if (e.pointerId === id) move(e); });
    const end = (e) => { if (e.pointerId !== id) return; id = null; this.touchTilt = 0; knob.style.left = 'calc(50% - 25px)'; };
    spring.addEventListener('pointerup', end);
    spring.addEventListener('pointercancel', end);
    const press = (on) => (e) => { e.preventDefault(); this.touchAttack = on; button.classList.toggle('on', on); this.lastEvent = performance.now(); };
    button.addEventListener('pointerdown', press(true));
    button.addEventListener('pointerup', press(false));
    button.addEventListener('pointercancel', press(false));
    button.addEventListener('pointerleave', press(false));
  }
  async enableMotion() {
    // tilt the phone to move, flick it to twang - just like the spring
    try {
      if (typeof DeviceMotionEvent !== 'undefined' && DeviceMotionEvent.requestPermission) {
        const p = await DeviceMotionEvent.requestPermission();
        if (p !== 'granted') return false;
      }
      if (typeof DeviceOrientationEvent !== 'undefined' && DeviceOrientationEvent.requestPermission) {
        await DeviceOrientationEvent.requestPermission();
      }
    } catch (e) { return false; }
    window.addEventListener('deviceorientation', (e) => {
      const landscape = Math.abs(window.orientation || (screen.orientation && screen.orientation.angle) || 0) === 90;
      const raw = landscape ? -(e.beta || 0) * Math.sign(window.orientation || 90) : (e.gamma || 0);
      this.motionTilt = Math.abs(raw) < 4 ? 0 : clamp(raw * 2.4, -66, 66);
    });
    window.addEventListener('devicemotion', (e) => {
      const a = e.acceleration || {};
      const m = Math.hypot(a.x || 0, a.y || 0, a.z || 0);
      if (m > 14) this.shakeUntil = performance.now() + 220;
    });
    this.motionOn = true;
    return true;
  }
  poll() {
    const k = this.keys;
    let tilt = 0;
    const slow = k.has('Shift') ? 0.5 : 1;
    if (k.has('ArrowRight') || k.has('d')) tilt += 62 * slow;
    if (k.has('ArrowLeft') || k.has('a')) tilt -= 62 * slow;
    let attack = k.has(' ') || k.has('ArrowUp') || k.has('w') || k.has('Enter') || this.touchAttack;
    if (this.touchTilt) tilt = this.touchTilt;
    if (this.motionOn && !tilt) tilt = this.motionTilt;
    if (performance.now() < this.shakeUntil) attack = true;
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const p of pads) {
      if (!p) continue;
      const ax = p.axes[0] || 0;
      if (Math.abs(ax) > 0.15) tilt = ax * 66;
      if (p.buttons.some((b, i) => i < 4 && b.pressed)) attack = true;
      if (p.buttons[14] && p.buttons[14].pressed) tilt = -62;
      if (p.buttons[15] && p.buttons[15].pressed) tilt = 62;
    }
    const active = tilt !== 0 || attack || performance.now() - this.lastEvent < 400;
    return { tilt, attack, active };
  }
}

// ----------------------------------------------------------- audio (browser)
class WebAudio {
  constructor(engine) { this.e = engine; this.ctx = null; this.on = false; }
  async start() {
    if (this.ctx) { if (this.ctx.state !== 'running') await this.ctx.resume(); return true; }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return false;
    try { this.ctx = new AC({ sampleRate: 22050 }); } catch (e) { this.ctx = new AC(); }
    this.e.x.tw_audio_init(this.ctx.sampleRate);
    app.applySoundSettings();
    const node = this.ctx.createScriptProcessor(1024, 0, 1);
    node.onaudioprocess = (ev) => {
      const out = ev.outputBuffer.getChannelData(0);
      if (!this.on) { out.fill(0); return; }
      let off = 0;
      while (off < out.length) {
        const n = Math.min(1024, out.length - off);
        out.set(this.e.audio(n), off);
        off += n;
      }
    };
    node.connect(this.ctx.destination);
    this.node = node;
    if (this.ctx.state !== 'running') await this.ctx.resume();
    this.on = true;
    return true;
  }
}

// ----------------------------------------------------------- the device (when served by TWANG)
class DeviceLink {
  constructor() { this.ws = null; this.listeners = {}; this.retry = 0; this.mirror = null; this.connected = false; }
  async detect() {
    if (!/^https?:$/.test(location.protocol)) return null;
    try {
      const ctl = new AbortController();
      const t = setTimeout(() => ctl.abort(), 1800);
      const r = await fetch('/api/info', { signal: ctl.signal, cache: 'no-store' });
      clearTimeout(t);
      if (!r.ok) return null;
      const j = await r.json();
      return j && j.device === 'twang' ? j : null;
    } catch (e) { return null; }
  }
  async api(path, opts = {}) {
    const init = { method: opts.method || (opts.body !== undefined ? 'POST' : 'GET'), cache: 'no-store' };
    if (opts.body !== undefined) {
      init.headers = { 'Content-Type': opts.raw ? 'application/octet-stream' : 'application/json' };
      init.body = opts.raw ? opts.body : (typeof opts.body === 'string' ? opts.body : JSON.stringify(opts.body));
    }
    const r = await fetch(path, init);
    const text = await r.text();
    let j = null;
    try { j = text ? JSON.parse(text) : {}; } catch (e) { j = { raw: text }; }
    if (!r.ok) throw new Error((j && j.message) || `${r.status} ${r.statusText}`);
    return j;
  }
  on(t, fn) { (this.listeners[t] = this.listeners[t] || []).push(fn); }
  emit(t, d) { (this.listeners[t] || []).forEach((f) => f(d)); }
  connect() {
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
    ws.binaryType = 'arraybuffer';
    this.ws = ws;
    ws.onopen = () => { this.connected = true; this.retry = 0; this.emit('open'); this.send({ t: 'mirror', on: true }); };
    ws.onmessage = (m) => {
      if (typeof m.data === 'string') {
        let j; try { j = JSON.parse(m.data); } catch (e) { return; }
        this.emit(j.t, j);
      } else {
        const v = new DataView(m.data);
        if (v.getUint8(0) !== 1) return;
        const n = v.getUint16(1, true);
        this.mirror = { n, rgb: new Uint8Array(m.data, 3, n * 3), at: performance.now() };
      }
    };
    ws.onclose = () => {
      this.connected = false;
      this.emit('close');
      setTimeout(() => this.connect(), Math.min(8000, 500 * 2 ** this.retry++));
    };
  }
  send(o) { if (this.ws && this.ws.readyState === 1) this.ws.send(JSON.stringify(o)); }
}

// ----------------------------------------------------------- settings schema
const SETTINGS = [
  { group: 'Game', items: [
    { k: 'difficulty', label: 'Difficulty', type: 'select', opts: [[0, 'Easy'], [1, 'Normal'], [2, 'Hard']], def: 1 },
    { k: 'livesPerLevel', label: 'Lives', type: 'number', min: 1, max: 9, def: 3 },
    { k: 'livesPerRun', label: 'Lives carry over between levels', type: 'switch', def: false, hint: 'Off is the original rule: your lives refill every level.' },
    { k: 'idleTimeout', label: 'Screensaver after (seconds)', type: 'number', min: 5, max: 3600, def: 30 },
    { k: 'defaultMode', label: 'Start up in', type: 'select', opts: [[0, 'Campaign'], [1, 'Endless']], def: 0 },
    { k: 'juice', label: 'Hit-stop, kill bursts and flashes', type: 'switch', def: true },
    { k: 'telegraph', label: 'Warn before lava ignites and enemies spawn', type: 'switch', def: true },
    { k: 'smooth', label: 'Smooth player motion', type: 'switch', def: true },
    { k: 'palette', label: 'Colours', type: 'select', opts: [[0, 'Classic'], [1, 'Colour-blind friendly']], def: 0 },
    { k: 'attract', label: 'Screensaver', type: 'switch', def: true },
  ] },
  { group: 'LED strip', items: [
    { k: 'ledCount', label: 'Number of LEDs', type: 'number', min: 1, max: 1000, def: 144, hint: 'Levels scale to any length.' },
    { k: 'brightness', label: 'Brightness', type: 'range', min: 10, max: 255, def: 150, device: true },
    { k: 'powerLimit', label: 'Power limit (mA)', type: 'number', min: 0, max: 20000, def: 2500, device: true, hint: 'Frames that would draw more are dimmed. Set it to your 5 V supply rating. 0 turns it off.' },
    { k: 'gravity', label: 'Death sparks fall toward the start', type: 'switch', def: false, hint: 'For strips that run up a wall.' },
    { k: 'bendPoint', label: 'Where the strip turns upward (0-1000)', type: 'number', min: 0, max: 1000, def: 550 },
    { k: 'conveyorBrightness', label: 'Conveyor brightness', type: 'number', min: 1, max: 80, def: 8, device: true, hint: 'APA102: about 8. NeoPixel: about 40.' },
    { k: 'lavaOffBrightness', label: 'Cool lava brightness', type: 'number', min: 1, max: 60, def: 4, device: true, hint: 'APA102: about 4. NeoPixel: about 15.' },
  ] },
  { group: 'Joystick', device: true, items: [
    { k: 'deadzone', label: 'Dead zone', type: 'number', min: 0, max: 30, def: 8, hint: 'Raise it if the player creeps without input.' },
    { k: 'attackThreshold', label: 'Twang sensitivity', type: 'range', min: 10000, max: 32767, def: 30000, invert: true, hint: 'Further right means a lighter flick twangs. The original is near the left end.' },
    { k: 'joyAxis', label: 'Tilt axis', type: 'select', opts: [[0, 'X'], [1, 'Y'], [2, 'Z']], def: 1 },
    { k: 'joyInvert', label: 'Reverse direction', type: 'switch', def: false },
  ] },
];
const SOUND_SETTINGS = [
  { k: 'masterVol', label: 'Master volume', type: 'range', min: 0, max: 100, def: 80 },
  { k: 'musicVol', label: 'Music', type: 'range', min: 0, max: 100, def: 60 },
  { k: 'sfxVol', label: 'Effects', type: 'range', min: 0, max: 100, def: 90 },
  { k: 'music', label: 'Adaptive music', type: 'switch', def: true },
  { k: 'moveSound', label: 'Movement thrum', type: 'switch', def: true },
  { k: 'attractAudio', label: 'Ambience during the screensaver', type: 'switch', def: false },
];
function defaults() {
  const d = {};
  for (const g of SETTINGS) for (const it of g.items) d[it.k] = it.def;
  for (const it of SOUND_SETTINGS) d[it.k] = it.def;
  return d;
}

// ----------------------------------------------------------- app
const app = {
  mode: 'sim',
  info: null,
  engine: new Engine(),
  dev: new DeviceLink(),
  inputs: new Inputs(),
  settings: defaults(),
  campaign: null,
  stats: null,
  view: 'play',
  preview: null,     // { level, spectate } while the editor previews locally
  lastState: null,
  remote: null,      // last state message from the device
  ticker: [],
  lastSaveIdx: -1,

  async boot() {
    this.strip = new StripView($('#strip'));
    this.inputs.bindTouch($('#spring'), $('#twangBtn'));
    if (matchMedia('(pointer: coarse)').matches) document.body.classList.add('touch');
    this.applyTheme(store.get('theme', 'system'));
    await this.engine.load();
    this.audio = new WebAudio(this.engine);

    this.info = await this.dev.detect();
    this.mode = this.info ? 'device' : 'sim';
    document.body.classList.add(this.mode);
    this.setConn(this.mode === 'device' ? 'live' : 'sim');

    if (this.mode === 'device') {
      try { Object.assign(this.settings, await this.dev.api('/api/settings')); } catch (e) { toast('Could not read settings: ' + e.message); }
      try { this.campaign = await this.dev.api('/api/campaign'); } catch (e) { this.campaign = clone(window.TWANG_DATA.campaigns.next); }
      try { this.stats = await this.dev.api('/api/stats'); } catch (e) { this.stats = emptyStats(); }
      this.dev.on('s', (s) => { this.remote = s; });
      this.dev.on('e', (e) => this.onEvent(e, true));
      this.dev.on('open', () => this.setConn('live'));
      this.dev.on('close', () => this.setConn('lost'));
      this.dev.on('saved', () => this.refreshStats());
      this.dev.connect();
    } else {
      Object.assign(this.settings, store.get('settings', {}));
      this.campaign = store.get('campaign', null) || clone(window.TWANG_DATA.campaigns.next);
      this.stats = store.get('stats', null) || emptyStats();
    }
    this.applyGameSettings();
    const r = this.engine.loadCampaign(this.campaign);
    if (!r.ok) { this.campaign = clone(window.TWANG_DATA.campaigns.next); this.engine.loadCampaign(this.campaign); toast('Saved campaign was damaged, loaded the default'); }
    this.engine.x.tw_boot();

    this.buildTabs();
    this.buildPlay();
    this.buildSound();
    this.buildSetup();
    this.buildStats();
    Editor.mount($('#v-levels'));
    this.showView(location.hash.slice(1) || 'play');
    window.addEventListener('hashchange', () => this.showView(location.hash.slice(1) || 'play'));

    // audio needs a gesture
    const startBtn = $('#startAudio');
    const wantsAudio = () => this.mode === 'sim' || (this.preview && this.settings.previewSound);
    startBtn.hidden = !wantsAudio();
    const kick = () => { if (wantsAudio()) this.audio.start().then((ok) => { if (ok) startBtn.hidden = true; }); };
    startBtn.addEventListener('click', kick);
    window.addEventListener('pointerdown', kick, { once: true, capture: true });
    window.addEventListener('keydown', kick, { once: true, capture: true });

    this.acc = 0;
    this.last = performance.now();
    requestAnimationFrame((t) => this.frame(t));
  },

  localActive() { return this.mode === 'sim' || !!this.preview; },

  frame(t) {
    const dt = Math.min(100, t - this.last);
    this.last = t;
    if (this.localActive()) {
      this.acc += dt;
      let steps = 0;
      while (this.acc >= 1000 / 60 && steps < 6) {
        const inp = this.inputs.poll();
        const events = this.engine.tick(inp);
        for (const e of events) this.onEvent(e, false);
        this.acc -= 1000 / 60;
        steps++;
      }
      const n = this.mode === 'sim' ? clamp(this.settings.ledCount | 0, 1, 1000) : clamp((this.info && this.info.ledCount) || 144, 1, 1000);
      this.strip.draw(this.engine.render(n), n);
    } else if (this.dev.mirror) {
      this.strip.draw(this.dev.mirror.rgb, this.dev.mirror.n);
    }
    if (!this.hudAt || t - this.hudAt > 100) { this.hudAt = t; this.updateHud(); }
    requestAnimationFrame((tt) => this.frame(tt));
  },

  currentState() {
    if (this.localActive()) return this.engine.state();
    const r = this.remote;
    if (!r) return null;
    return { stage: r.stage, mode: r.mode, level: r.level, lives: r.lives, score: r.score, combo: r.combo, name: r.name || '', bossHp: r.boss, paused: !!r.paused };
  },

  updateHud() {
    const s = this.currentState();
    const badge = $('#stripBadge');
    if (this.preview) badge.textContent = this.preview.spectate ? 'Preview: watching (you are a ghost)' : 'Preview: playing this level';
    else badge.textContent = this.mode === 'device' ? (this.dev.connected ? 'Live strip' : 'Reconnecting…') : 'Simulator';
    if (!s) return;
    const lvl = s.mode === 1 ? `Depth ${s.level + 1}` : s.mode === 3 ? 'Test' : `Level ${s.level + 1}`;
    const name = s.stage === 8 ? 'Waiting for a player' : (s.name || '');
    $('#hudName').innerHTML = '';
    $('#hudName').append(h('span', null, lvl), name);
    const lives = $('#hudLives');
    const want = clamp(s.lives, 0, 9);
    if (lives.childElementCount !== Math.max(want, 3)) {
      lives.innerHTML = '';
      for (let i = 0; i < Math.max(want, 3); i++) lives.append(h('b'));
    }
    $$('b', lives).forEach((b, i) => b.classList.toggle('gone', i >= want));
    $('#hudScore').textContent = fmt(s.score);
    $('#hudCombo').textContent = s.combo > 1 ? `×${s.combo}` : '';
    $('#hudStage').textContent = s.paused ? 'Paused' : (s.bossHp ? `Boss ${s.bossHp} left` : STAGE_LABEL[s.stage] || '');
    const pauseBtn = $('#btnPause');
    if (pauseBtn) pauseBtn.textContent = s.paused ? 'Resume' : 'Pause';
    document.body.classList.toggle('playing', this.view === 'play' && this.localActive() && s.stage !== 8);
    if (this.view === 'sound') this.updateSoundMeter(s);
  },

  setConn(kind) {
    const c = $('#conn');
    c.className = 'conn ' + kind;
    const ip = this.info ? (this.info.ip || location.host) : '';
    c.querySelector('span').textContent = kind === 'live' ? `Connected to ${this.info.name || 'TWANG'}${ip ? ' · ' + ip : ''}`
      : kind === 'lost' ? 'Lost connection, retrying' : 'Simulator in your browser';
  },

  // ---------------------------------------------------------- events
  onEvent(e, fromDevice) {
    const r = this.remote || {};
    const s = fromDevice ? Object.assign({}, r, { mode: e.mode ?? r.mode, level: e.lvl ?? r.level }) : this.engine.state();
    const camp = this.campaign && this.campaign.levels;
    const levelObj = s.mode === 3 ? (this.preview && this.preview.level) : (s.mode === 0 || s.mode === 2) && camp ? camp[s.level] : null;
    const kindName = KIND_LABEL[KINDS[e.kind]] || 'Enemy';
    let msg = null, color = '--led-exit';
    switch (e.type) {
      case EV.LEVEL_START: if (!e.kind) { msg = `${s.mode === 1 ? 'Depth' : 'Level'} ${e.value + 1}${levelObj ? ': ' + levelObj.name : ''}`; color = '--led-player'; } break;
      case EV.KILL: msg = e.value > 0 ? `Twanged ${kindName.toLowerCase()}${e.value > 1 ? ` ×${e.value}` : ''}` : `${kindName} burned up`; color = KIND_VAR[KINDS[e.kind]]; break;
      case EV.ARMOR_HIT: msg = 'Armor cracked'; color = '--led-armored'; break;
      case EV.SPLIT: msg = 'Splitter burst into fragments'; color = '--led-splitter'; break;
      case EV.DIE: msg = `Died at ${e.pos}`; color = '--led-enemy'; break;
      case EV.LEVEL_CLEAR: msg = `Cleared in ${(e.value / 1000).toFixed(1)} s${e.pos > 0 ? `, +${fmt(e.pos)} bonus` : ''}`; color = '--led-player'; break;
      case EV.BOSS_HIT: msg = e.value > 0 ? `Boss hit, ${e.value} to go` : 'Final blow'; color = '--led-boss'; break;
      case EV.BOSS_KILLED: msg = 'Boss defeated'; color = '--led-splitter'; break;
      case EV.GEM: msg = 'Gem +50'; color = '--led-gem'; break;
      case EV.LIFE: msg = 'Extra life'; color = '--led-life'; break;
      case EV.CHECKPOINT: msg = 'Checkpoint'; color = '--led-check'; break;
      case EV.GAME_OVER: msg = e.kind ? `Victory with ${fmt(e.value)}` : `Game over with ${fmt(e.value)}`; color = '--led-enemy'; break;
      case EV.NEW_HIGH: msg = 'New high score'; color = '--led-splitter'; break;
      case EV.CAMPAIGN_DONE: msg = 'Campaign complete'; color = '--led-splitter'; break;
      default: break;
    }
    if (msg) this.pushTicker(msg, color);
    if (!fromDevice && this.mode === 'sim' && !this.preview) this.recordStat(e, s, levelObj);
    if (!fromDevice && this.preview) this.recordPreviewStat(e, levelObj);
    if (e.type === EV.GAME_OVER && e.value > 0 && !this.preview && s.mode !== 3) this.maybeAskName(e.value, e.pos, s.mode, fromDevice);
    if (fromDevice && (e.type === EV.GAME_OVER || e.type === EV.LEVEL_CLEAR)) setTimeout(() => this.refreshStats(), 1200);
  },

  pushTicker(msg, color) {
    this.ticker.unshift({ msg, color, at: new Date() });
    this.ticker.length = Math.min(this.ticker.length, 40);
    const ul = $('#ticker');
    if (!ul) return;
    ul.prepend(h('li', null, h('i', { style: { background: `var(${color})` } }), msg,
      h('time', null, this.ticker[0].at.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }))));
    while (ul.childElementCount > 40) ul.lastChild.remove();
    $('#tickerEmpty').hidden = true;
  },

  recordStat(e, s, levelObj) {
    const st = this.stats;
    const lv = levelObj && levelObj.id ? (st.levels[levelObj.id] = st.levels[levelObj.id] || { plays: 0, clears: 0, deaths: 0, best: 0, heat: new Array(20).fill(0) }) : null;
    switch (e.type) {
      case EV.LEVEL_START: if (lv && !e.kind) lv.plays++; break;
      case EV.DIE: if (lv) { lv.deaths++; lv.heat[clamp(Math.floor(e.pos / 50), 0, 19)]++; } break;
      case EV.LEVEL_CLEAR: if (lv) { lv.clears++; if (!lv.best || e.value < lv.best) lv.best = e.value; } break;
      case EV.BOSS_KILLED: st.bossKills++; break;
      case EV.CAMPAIGN_DONE: st.wins++; break;
      case EV.GAME_OVER: st.games++; st.totalScore += e.value; break;
      default: return;
    }
    store.set('stats', st);
    if (this.view === 'stats') this.renderStats();
  },

  recordPreviewStat(e, levelObj) {
    if (e.type !== EV.DIE || !levelObj) return;
    Editor.notePreviewDeath(e.pos);
  },

  maybeAskName(score, level, mode, fromDevice) {
    const list = this.stats.scores || [];
    const qualifies = list.length < 10 || score > list[list.length - 1].score;
    if (!qualifies) return;
    const dlg = $('#nameDlg');
    $('#nameScore').textContent = fmt(score);
    const input = $('#nameInput');
    input.value = store.get('lastName', '');
    const done = async (save) => {
      dlg.close();
      const name = (input.value || '').trim().slice(0, 12) || 'Anonymous';
      if (!save) return;
      store.set('lastName', name);
      if (fromDevice) {
        try { await this.dev.api('/api/scores/name', { body: { name } }); this.refreshStats(); } catch (e) { toast(e.message); }
      } else {
        list.push({ name, score, level: level + 1, mode, date: Date.now() });
        list.sort((a, b) => b.score - a.score);
        list.length = Math.min(list.length, 10);
        this.stats.scores = list;
        store.set('stats', this.stats);
        if (this.view === 'stats') this.renderStats();
      }
    };
    $('#nameSave').onclick = () => done(true);
    $('#nameSkip').onclick = () => done(false);
    dlg.onkeydown = (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); done(true); } };
    if (!dlg.open) { try { dlg.showModal(); } catch (e) { dlg.setAttribute('open', ''); } input.focus(); }
  },

  // ---------------------------------------------------------- commands (local or device)
  async play(mode, level = -1) {
    if (this.mode === 'device') {
      try { await this.dev.api('/api/play', { body: { mode, level } }); } catch (e) { toast(e.message); }
    } else {
      this.engine.x.tw_start(mode, level);
      $('#strip').scrollIntoView({ block: 'nearest' });
    }
  },
  async control(action) {
    if (this.mode === 'device') {
      try { await this.dev.api('/api/control', { body: { action } }); } catch (e) { toast(e.message); }
      return;
    }
    const x = this.engine.x;
    if (action === 'pause') x.tw_pause(this.engine.state().paused ? 0 : 1);
    else if (action === 'skip') x.tw_skip();
    else if (action === 'boot') x.tw_boot();
    else if (action === 'attract') x.tw_attract();
  },

  startPreview(level, spectate) {
    this.preview = { level, spectate };
    const r = this.engine.testLevel(level, spectate);
    if (!r.ok) toast(r.message);
    if (this.mode === 'sim' || this.settings.previewSound) this.audio.start();
    $('#startAudio').hidden = !(this.mode === 'sim' || this.settings.previewSound) || (this.audio.ctx && this.audio.ctx.state === 'running');
    if (this.audio) this.audio.on = this.mode === 'sim' || !!this.settings.previewSound;
    return r;
  },
  endPreview() {
    if (!this.preview) return;
    this.preview = null;
    this.engine.opt(OPT.spectate, 0);
    if (this.mode === 'sim') {
      this.engine.loadCampaign(this.campaign);
      this.engine.x.tw_boot();
    } else if (this.audio) {
      this.audio.on = false;
    }
  },

  // ---------------------------------------------------------- settings
  applyGameSettings() {
    const s = this.settings, e = this.engine;
    e.opt(OPT.livesPerLevel, s.livesPerLevel);
    e.opt(OPT.livesPerRun, s.livesPerRun ? 1 : 0);
    e.opt(OPT.idleTimeout, s.idleTimeout);
    e.opt(OPT.juice, s.juice ? 1 : 0);
    e.opt(OPT.smooth, s.smooth ? 1 : 0);
    e.opt(OPT.palette, s.palette);
    e.opt(OPT.difficulty, s.difficulty);
    e.opt(OPT.gravity, s.gravity ? 1 : 0);
    e.opt(OPT.telegraph, s.telegraph ? 1 : 0);
    e.opt(OPT.attract, s.attract ? 1 : 0);
    e.opt(OPT.defaultMode, s.defaultMode);
    e.opt(OPT.highScore, this.stats && this.stats.scores && this.stats.scores[0] ? this.stats.scores[0].score : 0);
    this.applySoundSettings();
  },
  applySoundSettings() {
    const s = this.settings, e = this.engine;
    if (!e.x) return;
    e.opt(OPT.moveSound, s.moveSound ? 1 : 0);
    e.opt(OPT.music, s.music ? 1 : 0);
    e.opt(OPT.attractAudio, s.attractAudio ? 1 : 0);
    e.x.tw_set_volumes(s.masterVol / 100, s.musicVol / 100, s.sfxVol / 100);
  },
  async saveSetting(k, v) {
    this.settings[k] = v;
    this.applyGameSettings();
    if (this.mode === 'device') {
      clearTimeout(this.saveTimer);
      this.pendingSettings = Object.assign(this.pendingSettings || {}, { [k]: v });
      this.saveTimer = setTimeout(async () => {
        const body = this.pendingSettings;
        this.pendingSettings = null;
        try { await this.dev.api('/api/settings', { body }); } catch (e) { toast('Not saved: ' + e.message); }
      }, 350);
    } else {
      store.set('settings', this.settings);
    }
  },
  applyTheme(t) {
    if (t === 'system') document.documentElement.removeAttribute('data-theme');
    else document.documentElement.setAttribute('data-theme', t);
  },

  // ---------------------------------------------------------- views
  buildTabs() {
    const tabs = [['play', 'Play'], ['levels', 'Levels'], ['sound', 'Sound'], ['setup', 'Setup'], ['stats', 'Stats']];
    const nav = $('#tabs');
    for (const [id, label] of tabs) {
      nav.append(h('button', { role: 'tab', id: 'tab-' + id, 'aria-controls': 'v-' + id, onclick: () => { location.hash = id; } }, label));
    }
  },
  showView(id) {
    if (!$('#v-' + id)) id = 'play';
    const leaving = this.view;
    this.view = id;
    $$('.view').forEach((v) => { v.hidden = v.id !== 'v-' + id; });
    $$('#tabs button').forEach((b) => b.setAttribute('aria-selected', b.id === 'tab-' + id ? 'true' : 'false'));
    if (leaving === 'levels' && id !== 'levels') { Editor.onHide(); this.endPreview(); }
    if (id === 'levels') Editor.onShow();
    if (id === 'stats') this.refreshStats();
    if (id === 'setup' && this.mode === 'device') { this.dev.send({ t: 'joy', on: true }); this.refreshInfo(); }
    else if (this.mode === 'device') this.dev.send({ t: 'joy', on: false });
  },

  buildPlay() {
    const v = $('#v-play');
    const levels = () => (this.campaign.levels || []).map((l, i) => [i, `${i + 1}. ${l.name || 'Untitled'}`]);
    const pick = h('select', { id: 'practicePick', 'aria-label': 'Level to practice' }, levels().map(([i, n]) => h('option', { value: i }, n)));
    v.append(
      h('div', { class: 'modes' },
        h('button', { class: 'mode', onclick: () => this.play(0) }, h('strong', null, 'Campaign'), h('span', null, `${this.campaign.levels.length} levels, from the first twang to the Hydra`)),
        h('button', { class: 'mode', onclick: () => this.play(1) }, h('strong', null, 'Endless'), h('span', null, 'Generated levels that get meaner forever. A boss every seventh.')),
        h('div', { class: 'mode', style: { cursor: 'default' } }, h('strong', null, 'Practice'), h('span', null, 'Any level, unlimited lives.'),
          h('div', { class: 'row', style: { marginTop: '10px', flexWrap: 'nowrap' } }, h('label', { class: 'field', style: { flex: '1 1 auto' } }, pick),
            h('button', { class: 'btn', style: { flex: 'none' }, onclick: () => this.play(2, +pick.value) }, 'Go'))),
      ),
      h('div', { class: 'row', style: { marginTop: '14px' } },
        h('button', { class: 'btn', id: 'btnPause', onclick: () => this.control('pause') }, 'Pause'),
        h('button', { class: 'btn', onclick: () => this.control('skip') }, 'Skip level'),
        h('button', { class: 'btn', onclick: () => this.control('boot') }, 'Restart'),
        h('button', { class: 'btn ghost', onclick: () => this.control('attract') }, 'Screensaver'),
      ),
      this.mode === 'sim' ? h('div', null,
        h('h2', null, 'How to play'),
        h('p', { class: 'keys' }, 'Move with ', h('kbd', null, '←'), ' ', h('kbd', null, '→'), ' or ', h('kbd', null, 'A'), ' ', h('kbd', null, 'D'),
          ' (hold ', h('kbd', null, 'Shift'), ' to creep). Twang with ', h('kbd', null, 'Space'),
          '. On a phone, use the spring and the twang button, or tilt the phone and flick it. A gamepad works too.'),
        h('p', { class: 'small muted' }, 'Green is you, blue is the exit. Red things kill you; twang them first. Lava flickers before it ignites. Magenta needs two twangs, orange chases you, yellow splits, violet fades in and out.'),
        h('div', { class: 'row' },
          h('button', { class: 'btn small', onclick: async () => { const ok = await this.inputs.enableMotion(); toast(ok ? 'Tilt to move, flick to twang' : 'Motion controls are not available here'); } }, 'Use phone tilt'),
        ),
      ) : h('p', { class: 'small muted', style: { marginTop: '14px' } }, 'These buttons drive the real strip. Grab the spring and play.'),
      h('h2', null, 'Happening now'),
      h('p', { id: 'tickerEmpty', class: 'small muted' }, 'Kills, deaths and pickups will show up here as you play.'),
      h('ul', { class: 'ticker', id: 'ticker' }),
    );
    this.refreshPracticeList = () => {
      pick.innerHTML = '';
      levels().forEach(([i, n]) => pick.append(h('option', { value: i }, n)));
      $('.mode span', v).textContent = `${this.campaign.levels.length} levels`;
    };
  },

  buildSound() {
    const v = $('#v-sound');
    const fields = h('div', { class: 'grid' }, SOUND_SETTINGS.map((it) => this.settingField(it)));
    const sfx = [['Twang', 0], ['Kill', 1], ['Armor hit', 2], ['Split', 3], ['Death', 4], ['Level clear', 5], ['Boss hit', 6],
      ['Boss defeated', 7], ['Gem', 8], ['Extra life', 9], ['Checkpoint', 10], ['Lava ignites', 11], ['Boss rage', 12], ['Game over', 13], ['Power on', 100]];
    const intensity = h('input', { type: 'range', min: 0, max: 100, value: 60, id: 'musicIntensity' });
    let playing = -1;
    const playTrack = (i) => {
      playing = i;
      const I = intensity.value / 100;
      if (this.mode === 'device') this.dev.api('/api/sound', { body: { music: i, intensity: I } }).catch((e) => toast(e.message));
      else { this.audio.start(); this.engine.x.tw_music(i, I); }
    };
    intensity.addEventListener('input', () => { if (playing >= 0) playTrack(playing); });
    v.append(
      h('h2', null, 'Volume'), fields,
      h('h2', null, 'Music'),
      h('p', { class: 'small muted' }, 'Each track is a loop of layers that fade in as danger rises: drone, heartbeat, bells, bass, drums, a lead, and a dissonant shimmer when something is about to kill you. Drag intensity to hear it build.'),
      h('div', { class: 'board' }, TRACKS.map((t, i) => h('button', { class: 'btn', onclick: () => playTrack(i) }, TRACK_LABEL[t])),
        h('button', { class: 'btn ghost', onclick: () => { playing = -1; if (this.mode === 'device') this.dev.api('/api/sound', { body: { music: -1 } }); else this.engine.x.tw_music(-1, 0); } }, 'Stop music')),
      h('label', { class: 'field', style: { maxWidth: '420px', marginTop: '12px' } }, h('span', null, 'Intensity'), intensity),
      h('div', { style: { maxWidth: '420px', marginTop: '10px' } }, h('div', { class: 'small muted', id: 'musicNow' }, ' '), h('div', { class: 'meter' }, h('i', { id: 'musicMeter' }))),
      h('h2', null, 'Sound effects'),
      h('div', { class: 'board' }, sfx.map(([label, id]) => h('button', { class: 'btn small', onclick: () => this.testSound(id) }, label))),
      this.mode === 'device' ? h('label', { class: 'switch', style: { marginTop: '16px' } },
        h('input', { type: 'checkbox', checked: this.settings.previewSound ? true : null, onchange: (e) => { this.settings.previewSound = e.target.checked; store.set('previewSound', e.target.checked); } }),
        'Play level previews on this phone too') : '',
    );
    if (this.mode === 'device') this.settings.previewSound = store.get('previewSound', false);
  },
  testSound(id) {
    if (this.mode === 'device') { this.dev.api('/api/sound', { body: { id } }).catch((e) => toast(e.message)); return; }
    this.audio.start().then(() => this.engine.x.tw_test_sound(id));
  },
  updateSoundMeter() {
    if (this.mode !== 'sim') return;
    const t = this.engine.x.tw_music_track();
    const I = this.engine.x.tw_music_intensity();
    $('#musicNow').textContent = t < 4 ? `${TRACK_LABEL[TRACKS[t]]}, intensity ${Math.round(I * 100)}` : 'No music playing';
    $('#musicMeter').style.width = (t < 4 ? I * 100 : 0) + '%';
  },

  settingField(it) {
    const val = this.settings[it.k] ?? it.def;
    const id = 'set-' + it.k;
    const save = (v) => this.saveSetting(it.k, v);
    if (it.type === 'switch') {
      return h('label', { class: 'switch' }, h('input', { type: 'checkbox', id, checked: val ? true : null, onchange: (e) => save(e.target.checked) }),
        h('span', null, it.label, it.hint ? h('div', { class: 'hint' }, it.hint) : null));
    }
    let input;
    if (it.type === 'select') {
      input = h('select', { id, onchange: (e) => save(+e.target.value) }, it.opts.map(([v, l]) => h('option', { value: v, selected: +val === v ? true : null }, l)));
    } else if (it.type === 'range') {
      const shown = it.invert ? it.max + it.min - val : val;
      input = h('input', { type: 'range', id, min: it.min, max: it.max, value: shown, oninput: (e) => { const x = +e.target.value; save(it.invert ? it.max + it.min - x : x); } });
    } else {
      input = h('input', { type: 'number', id, min: it.min, max: it.max, value: val, inputmode: 'numeric',
        onchange: (e) => { const x = clamp(+e.target.value || it.def, it.min, it.max); e.target.value = x; save(x); } });
    }
    return h('label', { class: 'field' }, h('span', null, it.label), input, it.hint ? h('span', { class: 'hint' }, it.hint) : null);
  },

  buildSetup() {
    const v = $('#v-setup');
    for (const g of SETTINGS) {
      if (g.device && this.mode !== 'device') continue;
      const items = g.items.filter((it) => !it.device || this.mode === 'device');
      v.append(h('h2', null, g.group));
      if (g.group === 'LED strip' && this.mode === 'sim') v.append(h('p', { class: 'small muted' }, 'In the simulator this sets how many LEDs the strip above draws, so you can see your level at your real strip length.'));
      v.append(h('div', { class: 'grid' }, items.map((it) => this.settingField(it))));
      if (g.group === 'Joystick') {
        v.append(h('div', { class: 'panel', style: { marginTop: '14px', maxWidth: '520px' } },
          h('div', { class: 'small muted' }, 'Live from the spring'),
          h('div', { class: 'row', style: { marginTop: '6px' } }, h('span', { style: { width: '5em' } }, 'Tilt'), h('div', { class: 'meter', style: { flex: 1 } }, h('i', { id: 'joyTilt' })), h('span', { id: 'joyTiltN', style: { width: '3em', textAlign: 'right' } }, '0')),
          h('div', { class: 'row', style: { marginTop: '6px' } }, h('span', { style: { width: '5em' } }, 'Wobble'), h('div', { class: 'meter', style: { flex: 1 } }, h('i', { id: 'joyWob' })), h('span', { id: 'joyWobN', style: { width: '3em', textAlign: 'right' } }, '0')),
          h('div', { class: 'row', style: { marginTop: '12px' } },
            h('button', { class: 'btn', onclick: async () => { try { await this.dev.api('/api/calibrate', { body: {} }); toast('Calibrated. That position is now level.'); } catch (e) { toast(e.message); } } }, 'Calibrate level'),
            h('span', { class: 'hint' }, 'Let the spring hang still, then calibrate. This removes drift better than a big dead zone.'))));
        this.dev.on('j', (j) => {
          const t = $('#joyTilt');
          if (!t) return;
          t.style.width = clamp(50 + j.tilt / 1.8, 0, 100) + '%';
          $('#joyTiltN').textContent = Math.round(j.tilt);
          $('#joyWob').style.width = clamp(j.wobble / 340, 0, 100) + '%';
          $('#joyWob').style.background = j.wobble >= this.settings.attackThreshold ? 'var(--led-exit)' : 'var(--led-chaser)';
          $('#joyWobN').textContent = Math.round(j.wobble / 1000) + 'k';
        });
      }
    }
    v.append(h('h2', null, 'Appearance'),
      h('div', { class: 'grid' }, h('label', { class: 'field' }, h('span', null, 'Theme'),
        h('select', { onchange: (e) => { store.set('theme', e.target.value); this.applyTheme(e.target.value); } },
          [['system', 'Match this device'], ['dark', 'Dark'], ['light', 'Light']].map(([k, l]) => h('option', { value: k, selected: store.get('theme', 'system') === k ? true : null }, l))))));
    if (this.mode === 'device') this.buildDeviceSetup(v);
    else v.append(h('h2', null, 'Build the real one'),
      h('p', { class: 'small muted' }, 'Everything here, including the levels you design, runs on the ESP32 firmware in the TWANG32 repo. Flash it, join the TWANG Wi-Fi, and this same page opens on your phone with a live view of the strip.'),
      h('div', { class: 'row' }, h('button', { class: 'btn danger', onclick: async () => { if (await ask('Reset simulator settings, levels and stats?', 'Reset', true)) { ['settings', 'campaign', 'stats', 'draft'].forEach((k) => store.set(k, null)); location.reload(); } } }, 'Reset the simulator')));
  },

  buildDeviceSetup(v) {
    const wifi = { apName: this.settings.apName || 'TWANG', apPass: '', staSsid: this.settings.staSsid || '', staPass: '' };
    const inp = (k, label, type = 'text', hint) => h('label', { class: 'field' }, h('span', null, label),
      h('input', { type, value: wifi[k], autocomplete: 'off', oninput: (e) => { wifi[k] = e.target.value; } }), hint ? h('span', { class: 'hint' }, hint) : null);
    v.append(h('h2', null, 'Wi-Fi'),
      h('p', { class: 'small muted' }, 'TWANG always makes its own network. Add your home network too and it will also be reachable at twang.local.'),
      h('div', { class: 'grid' },
        inp('apName', 'TWANG network name'),
        inp('apPass', 'TWANG network password', 'password', 'At least 8 characters. Leave blank to keep the current one.'),
        inp('staSsid', 'Home network'),
        inp('staPass', 'Home network password', 'password', 'Leave blank to keep the current one.')),
      h('div', { class: 'row', style: { marginTop: '12px' } }, h('button', { class: 'btn', onclick: async () => {
        if (wifi.apPass && wifi.apPass.length < 8) { toast('The TWANG password needs at least 8 characters'); return; }
        try { await this.dev.api('/api/wifi', { body: wifi }); toast('Wi-Fi saved. TWANG will restart its network.'); } catch (e) { toast(e.message); }
      } }, 'Save Wi-Fi')));

    const sys = h('div', { class: 'panel small', id: 'sysInfo', style: { maxWidth: '560px' } }, 'Loading…');
    const fileIn = h('input', { type: 'file', accept: '.bin', hidden: true, onchange: (e) => this.uploadFirmware(e.target.files[0]) });
    const restoreIn = h('input', { type: 'file', accept: '.json', hidden: true, onchange: (e) => this.restoreBackup(e.target.files[0]) });
    v.append(h('h2', null, 'System'), sys,
      h('div', { class: 'row', style: { marginTop: '12px' } },
        h('button', { class: 'btn', onclick: () => fileIn.click() }, 'Update firmware'), fileIn,
        h('button', { class: 'btn', onclick: () => this.backup() }, 'Download backup'),
        h('button', { class: 'btn', onclick: () => restoreIn.click() }, 'Restore backup'), restoreIn,
        h('button', { class: 'btn', onclick: async () => { await this.dev.api('/api/reboot', { body: {} }).catch(() => {}); toast('Restarting'); } }, 'Restart TWANG'),
        h('button', { class: 'btn danger', onclick: async () => { if (!(await ask('Erase all settings, levels and stats on TWANG?', 'Erase everything', true))) return; await this.dev.api('/api/factory', { body: {} }).catch(() => {}); toast('Factory reset. Restarting.'); } }, 'Factory reset')),
      h('div', { class: 'meter', style: { maxWidth: '560px', marginTop: '10px' }, hidden: true, id: 'otaBar' }, h('i')));
  },
  async refreshInfo() {
    if (this.mode !== 'device') return;
    try {
      const i = await this.dev.api('/api/info');
      this.info = Object.assign(this.info || {}, i);
      const el = $('#sysInfo');
      if (!el) return;
      const up = i.uptime || 0;
      el.innerHTML = '';
      const rows = [['Firmware', i.version], ['Address', `${i.ip}${i.staIp ? ', ' + i.staIp + ' on ' + i.staSsid : ''}`], ['Joystick', i.joystick],
        ['Audio output', i.audio], ['LEDs', `${i.ledCount} (${i.ledType})`], ['Free memory', `${fmt(i.heap / 1024)} KB`],
        ['Frame rate', `${i.fps} fps`], ['Up for', `${Math.floor(up / 3600)} h ${Math.floor(up / 60) % 60} min`]];
      el.append(h('table', null, h('tbody', null, rows.map(([k, val]) => h('tr', null, h('td', { class: 'muted' }, k), h('td', null, val ?? ''))))));
    } catch (e) { /* offline */ }
  },
  async uploadFirmware(file) {
    if (!file) return;
    if (!(await ask(`Install ${file.name} (${fmt(file.size / 1024)} KB)? TWANG restarts when it is done.`, 'Install'))) return;
    const bar = $('#otaBar');
    bar.hidden = false;
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/update');
    xhr.upload.onprogress = (e) => { bar.firstChild.style.width = (e.loaded / e.total * 100) + '%'; };
    xhr.onload = () => { toast(xhr.status === 200 ? 'Installed. Restarting.' : 'Update failed: ' + xhr.responseText); if (xhr.status === 200) setTimeout(() => location.reload(), 9000); };
    xhr.onerror = () => toast('Upload failed');
    const fd = new FormData();
    fd.append('firmware', file, file.name);
    xhr.send(fd);
  },
  async backup() {
    const data = { format: 'twang-backup', version: 1, date: new Date().toISOString(), settings: this.settings, campaign: this.campaign, stats: this.stats };
    downloadJson(data, 'twang-backup.json');
  },
  async restoreBackup(file) {
    if (!file) return;
    try {
      const j = JSON.parse(await file.text());
      if (j.format !== 'twang-backup') throw new Error('That is not a TWANG backup file');
      if (!(await ask('Replace settings, levels and stats with this backup?', 'Restore'))) return;
      await this.dev.api('/api/settings', { body: j.settings });
      await this.dev.api('/api/campaign', { method: 'PUT', body: JSON.stringify(j.campaign) });
      if (j.stats) await this.dev.api('/api/stats', { method: 'PUT', body: JSON.stringify(j.stats) });
      toast('Backup restored');
      setTimeout(() => location.reload(), 800);
    } catch (e) { toast(e.message); }
  },

  buildStats() { this.renderStats(); },
  async refreshStats() {
    if (this.mode === 'device') {
      try { this.stats = await this.dev.api('/api/stats'); } catch (e) { /* keep */ }
    }
    this.applyGameSettings();
    if (this.view === 'stats') this.renderStats();
    Editor.onStats();
  },
  renderStats() {
    const v = $('#v-stats');
    const st = this.stats || emptyStats();
    v.innerHTML = '';
    const best = st.scores && st.scores[0] ? st.scores[0].score : 0;
    v.append(h('div', { class: 'bigstats' },
      h('div', null, h('strong', null, fmt(best)), h('span', null, 'High score')),
      h('div', null, h('strong', null, fmt(st.games)), h('span', null, 'Games played')),
      h('div', null, h('strong', null, st.games ? fmt(st.totalScore / st.games) : '0'), h('span', null, 'Average score')),
      h('div', null, h('strong', null, fmt(st.bossKills)), h('span', null, 'Bosses beaten')),
      h('div', null, h('strong', null, fmt(st.wins)), h('span', null, 'Campaigns finished'))));
    v.append(h('h2', null, 'High scores'));
    if (!st.scores || !st.scores.length) v.append(h('p', { class: 'muted' }, 'No scores yet. Finish a game and your score lands here.'));
    else v.append(h('table', null, h('thead', null, h('tr', null, h('th', null, ''), h('th', null, 'Name'), h('th', { class: 'n' }, 'Score'), h('th', { class: 'n' }, 'Reached'), h('th', null, 'Mode'))),
      h('tbody', null, st.scores.map((s, i) => h('tr', null, h('td', { class: 'muted' }, i + 1), h('td', null, s.name), h('td', { class: 'n' }, fmt(s.score)),
        h('td', { class: 'n' }, s.mode === 1 ? `Depth ${s.level}` : `Level ${s.level}`), h('td', { class: 'muted' }, MODES[s.mode] || ''))))));
    v.append(h('h2', null, 'Levels'));
    const levels = (this.campaign && this.campaign.levels) || [];
    const rows = levels.map((l, i) => {
      const s = st.levels[l.id] || { plays: 0, clears: 0, deaths: 0, best: 0 };
      const rate = s.plays ? s.clears / s.plays : 0;
      return h('tr', null, h('td', { class: 'muted' }, i + 1), h('td', null, l.name), h('td', { class: 'n' }, s.plays), h('td', { class: 'n' }, s.deaths),
        h('td', null, h('div', { class: 'minibar', title: `${Math.round(rate * 100)}% cleared` }, h('i', { style: { width: (rate * 100) + '%' } }))),
        h('td', { class: 'n' }, s.best ? (s.best / 1000).toFixed(1) + ' s' : ''));
    });
    v.append(h('div', { style: { overflowX: 'auto' } }, h('table', null, h('thead', null, h('tr', null, h('th', null, ''), h('th', null, 'Level'), h('th', { class: 'n' }, 'Tries'), h('th', { class: 'n' }, 'Deaths'), h('th', null, 'Cleared'), h('th', { class: 'n' }, 'Best'))), h('tbody', null, rows))));
    v.append(h('p', { class: 'small muted', style: { marginTop: '10px' } }, 'Where players die is drawn under each level in the editor, so you can see which spot is too mean.'));
    v.append(h('div', { class: 'row', style: { marginTop: '12px' } }, h('button', { class: 'btn danger', onclick: async () => {
      if (!(await ask('Clear all scores and play statistics?', 'Clear', true))) return;
      if (this.mode === 'device') { try { await this.dev.api('/api/stats/reset', { body: {} }); } catch (e) { toast(e.message); } }
      else store.set('stats', emptyStats());
      this.stats = emptyStats();
      this.refreshStats();
    } }, 'Clear statistics')));
  },
};

function emptyStats() { return { games: 0, totalScore: 0, bossKills: 0, wins: 0, scores: [], levels: {} }; }
function clone(o) { return JSON.parse(JSON.stringify(o)); }
function downloadJson(obj, name) {
  const blob = new Blob([JSON.stringify(obj, null, 2)], { type: 'application/json' });
  const a = h('a', { href: URL.createObjectURL(blob), download: name });
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}
