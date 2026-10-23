"""Device-mode UI test against tools/virtual-device.mjs (same API as the firmware)."""
import json, os, pathlib, subprocess, sys, tempfile, time, urllib.request
from playwright.sync_api import sync_playwright

root = pathlib.Path(__file__).resolve().parent.parent
shots = root / "build" / "shots"
shots.mkdir(parents=True, exist_ok=True)
port = 8091
data = tempfile.mkdtemp(prefix="twang-virtual-")
dev = subprocess.Popen(["node", str(root / "tools" / "virtual-device.mjs"), "--port", str(port), "--data", data], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
base = f"http://localhost:{port}"
def api(path, body=None, method=None):
    req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(), method=method or ("POST" if body is not None else "GET"), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read().decode())
for _ in range(50):
    try: api("/api/info"); break
    except Exception: time.sleep(0.1)
errors, fails = [], []
def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond: fails.append(msg)
try:
    with sync_playwright() as p:
        b = p.chromium.launch()
        ctx = b.new_context(viewport={"width": 390, "height": 844}, device_scale_factor=2, has_touch=True, color_scheme="dark")
        pg = ctx.new_page()
        pg.on("pageerror", lambda e: errors.append(str(e)))
        pg.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
        pg.goto(base)
        pg.wait_for_timeout(2000)
        conn = pg.inner_text("#conn")
        check("Connected to" in conn, f"connection badge: {conn!r}")
        check(pg.evaluate("TWANG.app.mode") == "device", "UI detected device mode")
        check(pg.evaluate("!!TWANG.app.dev.mirror"), "receiving live strip frames over WebSocket")
        pg.click("text=Campaign")
        pg.wait_for_timeout(2500)
        st = api("/api/info")
        remote = pg.evaluate("TWANG.app.remote")
        check(remote and remote["mode"] == 0 and remote["stage"] in (1, 2), f"campaign started on the device: {remote}")
        # steer the virtual joystick forward through level 1
        for _ in range(40):
            pg.evaluate("TWANG.app.dev.ws.send(JSON.stringify({t:'input', tilt:60, attack:false}))")
            pg.wait_for_timeout(100)
        remote = pg.evaluate("TWANG.app.remote")
        check(remote["level"] >= 1, f"virtual joystick moved the player to level {remote['level'] + 1}")
        ticker = pg.inner_text("#ticker")
        check("Level 2" in ticker or "Cleared" in ticker, "event ticker shows device events")
        pg.screenshot(path=str(shots / "device-play.png"))
        # settings round trip
        pg.evaluate("location.hash='setup'")
        pg.wait_for_timeout(800)
        pg.fill("#set-livesPerLevel", "5")
        pg.press("#set-livesPerLevel", "Tab")
        pg.wait_for_timeout(900)
        check(api("/api/settings")["livesPerLevel"] == 5, "setting saved to the device")
        pg.screenshot(path=str(shots / "device-setup.png"), full_page=True)
        # rename a level and save the campaign to the device
        pg.evaluate("location.hash='levels'")
        pg.wait_for_timeout(800)
        pg.fill(".lvl-name", "Renamed On Phone")
        pg.press(".lvl-name", "Tab")
        pg.wait_for_timeout(300)
        check("Unsaved" in pg.inner_text("#edStatus"), "editor shows unsaved changes")
        pg.click("#edSave")
        pg.wait_for_timeout(800)
        camp = api("/api/campaign")
        check(camp["levels"][0]["name"] == "Renamed On Phone", "campaign saved to the device")
        pg.click("button:has-text('Test on strip')")
        pg.wait_for_timeout(1200)
        check(pg.evaluate("TWANG.app.remote.mode") == 3, "level is being tested on the strip")
        pg.screenshot(path=str(shots / "device-levels.png"), full_page=True)
        pg.evaluate("location.hash='stats'")
        pg.wait_for_timeout(800)
        check("High score" in pg.inner_text("#v-stats"), "stats view renders device stats")
        b.close()
finally:
    dev.terminate()
print("\n".join("  console: " + e for e in errors) if errors else "  no console errors")
sys.exit(1 if fails or errors else 0)
