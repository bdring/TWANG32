"""Headless browser smoke test of the web UI in simulator mode.
Usage: python3 tests/ui_smoke.py [out_dir]"""
import sys, pathlib, time
from playwright.sync_api import sync_playwright

root = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "build" / "shots")
out.mkdir(parents=True, exist_ok=True)
page_url = (root / "build" / "web" / "index.html").as_uri()
errors = []

with sync_playwright() as p:
    b = p.chromium.launch(args=["--autoplay-policy=no-user-gesture-required"])
    for name, vp, scheme in [("desktop", {"width": 1280, "height": 900}, "dark"), ("phone", {"width": 390, "height": 844}, "light")]:
        ctx = b.new_context(viewport=vp, color_scheme=scheme, has_touch=name == "phone", device_scale_factor=2 if name == "phone" else 1)
        pg = ctx.new_page()
        pg.on("console", lambda m: errors.append(f"{name} console {m.type}: {m.text}") if m.type in ("error", "warning") else None)
        pg.on("pageerror", lambda e: errors.append(f"{name} pageerror: {e}"))
        pg.goto(page_url)
        pg.wait_for_timeout(1500)
        pg.click("body", position={"x": 5, "y": 300})
        # start a campaign and play with the keyboard for a bit
        pg.click("text=Campaign")
        pg.wait_for_timeout(2300)
        pg.keyboard.down("ArrowRight")
        for i in range(8):
            pg.wait_for_timeout(350)
            pg.keyboard.press("Space")
        pg.keyboard.up("ArrowRight")
        pg.wait_for_timeout(400)
        pg.screenshot(path=str(out / f"{name}-play.png"), full_page=False)
        state = pg.evaluate("JSON.stringify(TWANG.app.engine.state())")
        print(name, "state after playing:", state)
        for tab in ["levels", "sound", "setup", "stats"]:
            pg.evaluate(f"location.hash = '{tab}'")
            pg.wait_for_timeout(900)
            pg.screenshot(path=str(out / f"{name}-{tab}.png"), full_page=True)
        # editor interactions: select level 8, add an armored enemy, drag it, undo
        pg.evaluate("location.hash = 'levels'")
        pg.wait_for_timeout(500)
        pg.click("#edList li:nth-child(9)")
        pg.wait_for_timeout(400)
        before = pg.evaluate("TWANG.Editor.level().enemies.length")
        pg.click("button.chip:has-text('Armored')")
        pg.wait_for_timeout(300)
        after = pg.evaluate("TWANG.Editor.level().enemies.length")
        sel = pg.locator("#edTrack .ent.sel").first
        pg.evaluate("document.querySelector('#edTrack').scrollIntoView({block:'center'})")
        pg.wait_for_timeout(200)
        bb = sel.bounding_box()
        pg.mouse.move(bb["x"] + bb["width"] / 2, bb["y"] + bb["height"] / 2)
        pg.mouse.down(); pg.mouse.move(bb["x"] + 60, bb["y"] + 8, steps=6); pg.mouse.up()
        pg.wait_for_timeout(500)
        moved = pg.evaluate("TWANG.Editor.level().enemies[TWANG.Editor.level().enemies.length-1].pos")
        pg.screenshot(path=str(out / f"{name}-editor-edit.png"), full_page=True)
        for _ in range(2):
            if pg.evaluate("!document.querySelector('#edUndo').disabled"):
                pg.evaluate("document.querySelector('#edUndo').click()"); pg.wait_for_timeout(250)
        undone = pg.evaluate("TWANG.Editor.level().enemies.length")
        print(name, f"editor: enemies {before} -> {after}, dragged to {moved}, after 2 undos {undone}")
        assert after == before + 1 and undone == before, "editor add/undo failed"
        # preview should be running the level on the strip
        st = pg.evaluate("JSON.stringify(TWANG.app.engine.state())")
        print(name, "preview state:", st)
        ctx.close()
    b.close()

print("\n".join(errors) if errors else "no console errors")
sys.exit(1 if errors else 0)
