#!/usr/bin/env node
/*
  Virtual TWANG: runs the real game engine (build/twang.wasm) behind the same
  HTTP + WebSocket API as the ESP32 firmware, so the web UI can be developed and
  tested in "device" mode without hardware.

    npm install            (once, for the "ws" package)
    node tools/virtual-device.mjs [--port 8080] [--data ./virtual-data]

  Open http://localhost:8080. Drive the virtual joystick from a script by sending
  {"t":"input","tilt":60,"attack":false} over the WebSocket.
*/
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const arg = (name, def) => { const i = process.argv.indexOf('--' + name); return i > 0 ? process.argv[i + 1] : def; };
const PORT = +arg('port', process.env.PORT || 8080);
const DATA = path.resolve(arg('data', process.env.TWANG_DATA || path.join(ROOT, 'virtual-data')));
fs.mkdirSync(DATA, { recursive: true });

// ------------------------------------------------------------------ engine
const wasmBytes = fs.readFileSync(path.join(ROOT, 'build', 'twang.wasm'));
const { instance } = await WebAssembly.instantiate(wasmBytes, {});
const w = instance.exports;
w.__wasm_call_ctors();
w.tw_init(22050, (Math.random() * 1e9) >>> 0);
const enc = new TextEncoder();
const putText = (s) => { const b = enc.encode(s); new Uint8Array(w.memory.buffer, w.tw_text_buffer(), w.tw_text_capacity()).set(b); return b.length; };
const readStr = (p) => { const m = new Uint8Array(w.memory.buffer); let s = ''; while (m[p]) s += String.fromCharCode(m[p++]); return s; };
const parseMsg = () => ({ message: readStr(w.tw_parse_message()), warnings: w.tw_parse_warnings() });

// ------------------------------------------------------------------ persisted state (mirrors the firmware's files)
const file = (n) => path.join(DATA, n);
const readJson = (n, d) => { try { return JSON.parse(fs.readFileSync(file(n), 'utf8')); } catch { return d; } };
const writeJson = (n, v) => fs.writeFileSync(file(n), typeof v === 'string' ? v : JSON.stringify(v));
const DEFAULTS = { ledCount: 144, brightness: 150, powerLimit: 2500, conveyorBrightness: 8, lavaOffBrightness: 4, gravity: false, bendPoint: 550,
  difficulty: 1, livesPerLevel: 3, livesPerRun: false, idleTimeout: 30, defaultMode: 0, juice: true, telegraph: true, smooth: true, attract: true,
  palette: 0, masterVol: 80, musicVol: 60, sfxVol: 90, music: true, moveSound: true, attractAudio: false, deadzone: 8, attackThreshold: 30000,
  joyAxis: 1, joyInvert: false, joyOffset: [0, 0, 0], apName: 'TWANG', apPass: '12345678', staSsid: '', staPass: '' };
const settings = Object.assign({}, DEFAULTS, readJson('settings.json', {}));
const stockText = fs.readFileSync(path.join(ROOT, 'campaign', 'next.json'), 'utf8');
let campaignText = fs.existsSync(file('campaign.json')) ? fs.readFileSync(file('campaign.json'), 'utf8') : stockText;
let campaign = JSON.parse(campaignText);
const stats = Object.assign({ games: 0, totalScore: 0, bossKills: 0, wins: 0, scores: [], levels: {} }, readJson('stats.json', {}));
let pendingName = -1;

function applySettings() {
  const s = settings;
  const opt = [s.livesPerLevel, s.livesPerRun, s.idleTimeout, s.juice, s.smooth, s.palette, s.difficulty, s.gravity, s.telegraph, s.attract, s.moveSound, s.music];
  opt.forEach((v, i) => w.tw_set_option(i, +v));
  w.tw_set_option(13, s.defaultMode);
  w.tw_set_option(15, +s.attractAudio);
  w.tw_set_option(16, s.conveyorBrightness);
  w.tw_set_option(17, s.lavaOffBrightness);
  w.tw_set_option(12, stats.scores[0] ? stats.scores[0].score : 0);
}
function loadCampaign() {
  const n = w.tw_load_campaign(putText(campaignText));
  if (n < 0) { console.warn('campaign rejected:', parseMsg().message); campaignText = stockText; w.tw_load_campaign(putText(campaignText)); }
  campaign = JSON.parse(campaignText);
}
applySettings();
loadCampaign();
w.tw_boot();

// ------------------------------------------------------------------ game loop at 60 Hz
const input = { tilt: 0, attack: false, until: 0 };
const EV = { LEVEL_START: 2, KILL: 4, ARMOR_HIT: 5, SPLIT: 6, DIE: 8, LEVEL_CLEAR: 9, BOSS_HIT: 10, BOSS_KILLED: 11, GEM: 13, LIFE: 14,
  CHECKPOINT: 15, GAME_OVER: 18, NEW_HIGH: 19, CAMPAIGN_DONE: 20 };
const FORWARD = new Set(Object.values(EV));
let frames = 0, fps = 60;
setInterval(() => { fps = frames; frames = 0; }, 1000);

function levelId() {
  const mode = w.tw_mode();
  if (mode !== 0 && mode !== 2) return null;
  const l = campaign.levels[w.tw_level()];
  return l && l.id;
}
function recordStat(e) {
  const id = levelId();
  const L = id ? (stats.levels[id] = stats.levels[id] || { plays: 0, clears: 0, deaths: 0, best: 0, heat: new Array(20).fill(0) }) : null;
  const mode = w.tw_mode();
  switch (e.type) {
    case EV.LEVEL_START: if (L && !e.kind) L.plays++; break;
    case EV.DIE: if (L) { L.deaths++; L.heat[Math.max(0, Math.min(19, Math.floor(e.pos / 50)))]++; } break;
    case EV.LEVEL_CLEAR: if (L) { L.clears++; if (!L.best || e.value < L.best) L.best = e.value; } break;
    case EV.BOSS_KILLED: stats.bossKills++; break;
    case EV.CAMPAIGN_DONE: stats.wins++; break;
    case EV.GAME_OVER:
      if (mode === 3 || mode === 2) break;
      stats.games++;
      stats.totalScore += Math.max(0, e.value);
      if (e.value > 0 && (stats.scores.length < 10 || e.value > stats.scores[stats.scores.length - 1].score)) {
        const entry = { name: '???', score: e.value, level: e.pos + 1, mode };
        stats.scores.push(entry);
        stats.scores.sort((a, b) => b.score - a.score);
        stats.scores.length = Math.min(stats.scores.length, 10);
        pendingName = stats.scores.indexOf(entry);
      }
      writeJson('stats.json', stats);
      w.tw_set_option(12, stats.scores[0] ? stats.scores[0].score : 0);
      break;
    default: return;
  }
}

setInterval(() => {
  const now = Date.now();
  const tilt = now < input.until ? input.tilt : 0;
  const attack = now < input.until && input.attack;
  w.tw_set_input(tilt, attack ? 1 : 0, tilt || attack ? 1 : 0);
  const n = w.tw_tick();
  for (let i = 0; i < n; i++) {
    const e = { type: w.tw_event_type(i), kind: w.tw_event_kind(i), pos: w.tw_event_pos(i), value: w.tw_event_value(i) };
    recordStat(e);
    if (FORWARD.has(e.type)) broadcast(JSON.stringify({ t: 'e', ...e, mode: w.tw_mode(), lvl: w.tw_level() }));
  }
  frames++;
  w.tw_audio(368);  // keep the synth's command queue drained
}, 1000 / 60);

// ------------------------------------------------------------------ websocket
const server = http.createServer(handle);
const wss = new WebSocketServer({ server, path: '/ws' });
wss.on('connection', (ws) => {
  ws.sub = { mirror: false, joy: false };
  ws.on('message', (m) => {
    let j; try { j = JSON.parse(m.toString()); } catch { return; }
    if (j.t === 'mirror') ws.sub.mirror = !!j.on;
    if (j.t === 'joy') ws.sub.joy = !!j.on;
    if (j.t === 'input') { input.tilt = +j.tilt || 0; input.attack = !!j.attack; input.until = Date.now() + 250; }
  });
});
function broadcast(msg) { for (const c of wss.clients) if (c.readyState === 1) c.send(msg); }
setInterval(() => {
  broadcast(JSON.stringify({ t: 's', stage: w.tw_stage(), mode: w.tw_mode(), level: w.tw_level(), name: readStr(w.tw_level_name()),
    lives: w.tw_lives(), score: w.tw_score(), combo: w.tw_combo(), boss: w.tw_boss_hp(), paused: !!w.tw_paused(), fps }));
}, 200);
setInterval(() => {
  const N = settings.ledCount, n = Math.min(N, 300);
  const src = new Uint8Array(w.memory.buffer, w.tw_render(N), N * 3);
  const frame = Buffer.alloc(3 + n * 3);
  frame[0] = 1; frame.writeUInt16LE(n, 1);
  for (let i = 0; i < n; i++) {
    const a = Math.floor(i * N / n), b = Math.max(a + 1, Math.floor((i + 1) * N / n));
    let best = a, bs = -1;
    for (let k = a; k < b && k < N; k++) { const s = src[k * 3] + src[k * 3 + 1] + src[k * 3 + 2]; if (s > bs) { bs = s; best = k; } }
    frame[3 + i * 3] = src[best * 3]; frame[4 + i * 3] = src[best * 3 + 1]; frame[5 + i * 3] = src[best * 3 + 2];
  }
  const joy = JSON.stringify({ t: 'j', tilt: Date.now() < input.until ? input.tilt : 0, wobble: Date.now() < input.until && input.attack ? 32000 : 1200 });
  for (const c of wss.clients) {
    if (c.readyState !== 1) continue;
    if (c.sub.mirror && c.bufferedAmount < 20000) c.send(frame);
    if (c.sub.joy) c.send(joy);
  }
}, 66);

// ------------------------------------------------------------------ http api
function send(res, code, body, type = 'application/json') {
  res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  res.end(typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body));
}
const ok = (res, extra = {}) => send(res, 200, { ok: true, ...extra });
const fail = (res, code, message) => send(res, code, { ok: false, message });
const readBody = (req) => new Promise((resolve) => { const parts = []; req.on('data', (d) => parts.push(d)); req.on('end', () => resolve(Buffer.concat(parts).toString('utf8'))); });
const json = (s) => { try { return s ? JSON.parse(s) : {}; } catch { return null; } };
const publicSettings = () => { const s = { ...settings, apPassSet: !!settings.apPass, staPassSet: !!settings.staPass }; delete s.apPass; delete s.staPass; return s; };

async function handle(req, res) {
  const url = new URL(req.url, 'http://x');
  const p = url.pathname, m = req.method;
  if ((p === '/' || p === '/index.html') && m === 'GET') {
    const page = fs.readFileSync(path.join(ROOT, 'build', 'web', 'index.html'));
    res.writeHead(200, { 'Content-Type': 'text/html', 'Content-Encoding': 'gzip', 'Cache-Control': 'no-cache' });
    return res.end(zlib.gzipSync(page));
  }
  if (!p.startsWith('/api/')) { res.writeHead(302, { Location: '/' }); return res.end(); }
  const body = m === 'GET' ? '' : await readBody(req);
  const b = json(body);
  try {
    switch (`${m} ${p}`) {
      case 'GET /api/info':
        return send(res, 200, { device: 'twang', version: 'virtual', name: settings.apName + ' (virtual)', ip: `localhost:${PORT}`, joystick: 'virtual',
          heap: 180000, minHeap: 150000, uptime: Math.floor(process.uptime()), fps, ledCount: settings.ledCount, ledType: 'virtual', maxLeds: 1000,
          audio: 'none (virtual device)', audioUnderruns: 0, levels: w.tw_level_count() });
      case 'GET /api/settings': return send(res, 200, publicSettings());
      case 'POST /api/settings':
      case 'POST /api/wifi':
        if (!b) return fail(res, 400, 'settings must be a JSON object');
        for (const [k, v] of Object.entries(b)) if (k in DEFAULTS && !((k === 'apPass' || k === 'staPass') && !v)) settings[k] = v;
        writeJson('settings.json', settings);
        applySettings();
        return ok(res);
      case 'GET /api/campaign': return send(res, 200, campaignText);
      case 'PUT /api/campaign':
      case 'POST /api/campaign': {
        const n = w.tw_validate_campaign(putText(body));
        if (n < 0) return fail(res, 400, parseMsg().message);
        campaignText = body;
        fs.writeFileSync(file('campaign.json'), body);
        loadCampaign();
        return ok(res, { levels: n, warnings: parseMsg().warnings });
      }
      case 'POST /api/campaign/reset': try { fs.unlinkSync(file('campaign.json')); } catch {} campaignText = stockText; loadCampaign(); return ok(res);
      case 'POST /api/play': w.tw_start(Math.max(0, Math.min(2, b.mode | 0)), b.level ?? -1); return ok(res);
      case 'POST /api/control': {
        const a = b && b.action;
        if (a === 'pause') w.tw_pause(w.tw_paused() ? 0 : 1);
        else if (a === 'skip') w.tw_skip();
        else if (a === 'boot') w.tw_boot();
        else if (a === 'attract') w.tw_attract();
        else return fail(res, 400, 'action must be pause, skip, boot or attract');
        return ok(res);
      }
      case 'POST /api/test': {
        if (!b || !b.level) return fail(res, 400, 'expected {"level": {...}}');
        if (w.tw_test_level(putText(JSON.stringify(b.level)), b.spectate ? 1 : 0) < 0) return fail(res, 400, parseMsg().message);
        return ok(res);
      }
      case 'POST /api/sound':
        if (b && b.music !== undefined) w.tw_music(b.music, b.intensity ?? 0.6); else w.tw_test_sound(b ? b.id : 0);
        return ok(res);
      case 'POST /api/calibrate': return ok(res);
      case 'GET /api/stats': return send(res, 200, stats);
      case 'PUT /api/stats': if (!b) return fail(res, 400, 'not a stats file'); Object.assign(stats, b); writeJson('stats.json', stats); return ok(res);
      case 'POST /api/stats/reset': Object.assign(stats, { games: 0, totalScore: 0, bossKills: 0, wins: 0, scores: [], levels: {} }); writeJson('stats.json', stats); return ok(res);
      case 'POST /api/scores/name':
        if (pendingName < 0 || !stats.scores[pendingName]) return fail(res, 409, 'no recent high score to name');
        stats.scores[pendingName].name = String((b && b.name) || '???').slice(0, 12);
        pendingName = -1;
        writeJson('stats.json', stats);
        return ok(res);
      case 'POST /api/reboot': w.tw_boot(); return ok(res);
      case 'POST /api/factory': for (const f of ['settings.json', 'campaign.json', 'stats.json']) try { fs.unlinkSync(file(f)); } catch {} return ok(res);
      case 'POST /api/update': return fail(res, 400, 'the virtual device has no firmware to update');
      default: return fail(res, 404, 'no such endpoint');
    }
  } catch (e) { return fail(res, 500, e.message); }
}

server.listen(PORT, () => console.log(`Virtual TWANG on http://localhost:${PORT}  (data in ${DATA})`));
