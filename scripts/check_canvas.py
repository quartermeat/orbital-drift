"""Travel into a world and find the beacon: python3 scripts/check_canvas.py

Descends into a track's artwork, then repeatedly zooms and re-centres the way a
player would until the beacon is big enough to click, right-clicks it, and
checks that finding it unseals the next track. Screenshots the canvas.
"""
from pathlib import Path
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / 'artifacts' / 'canvas.json'
SHOT = ROOT / 'artifacts' / 'canvas.png'


def read(timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            return json.loads(STATE.read_text())
        except (OSError, json.JSONDecodeError):
            time.sleep(.08)
    raise RuntimeError('no state')


def main():
    failures, summary = [], {}
    ROOT.joinpath('artifacts').mkdir(exist_ok=True)
    STATE.unlink(missing_ok=True)
    with (ROOT / 'artifacts' / 'canvas.log').open('w') as log:
        app = subprocess.Popen([str(ROOT / 'build/orbital-drift'), '--windowed', '--state', str(STATE),
                                '--capture', str(SHOT), '--capture-after', '12', '--seconds', '80'],
                               stdout=log, stderr=subprocess.STDOUT)
        launched = time.time()
        try:
            window = None
            for _ in range(50):
                try:
                    found = subprocess.check_output(['xdotool', 'search', '--onlyvisible', '--pid', str(app.pid),
                                                     '--name', '^Orbital Drift$'], text=True).splitlines()
                    if found:
                        window = found[0]
                        break
                except subprocess.CalledProcessError:
                    pass
                time.sleep(.2)
            time.sleep(2.5)
            geometry = subprocess.check_output(['xdotool', 'getwindowgeometry', '--shell', window], text=True)
            info = dict(line.split('=', 1) for line in geometry.strip().splitlines())
            width, height = int(info['WIDTH']), int(info['HEIGHT'])
            midX, midY = width // 2, height // 2

            def key(name, hold=.08):
                subprocess.run(['xdotool', 'windowactivate', '--sync', window, 'windowfocus', '--sync', window,
                                'keydown', '--clearmodifiers', name], check=True)
                time.sleep(hold)
                subprocess.run(['xdotool', 'keyup', name], check=True)

            def at(x, y):
                subprocess.run(['xdotool', 'windowactivate', '--sync', window,
                                'mousemove', '--window', window, str(int(x)), str(int(y))], check=True)

            def wheel(clicks, x, y):
                at(x, y)
                time.sleep(.12)
                for _ in range(abs(clicks)):
                    subprocess.run(['xdotool', 'click', '4' if clicks > 0 else '5'], check=True)
                    time.sleep(.05)

            def drag(dx, dy):
                at(midX, midY)
                time.sleep(.12)
                subprocess.run(['xdotool', 'mousedown', '1'], check=True)
                for stepIndex in range(1, 7):     # move across frames or the delta is never seen
                    at(midX + dx * stepIndex / 6, midY + dy * stepIndex / 6)
                    time.sleep(.045)
                subprocess.run(['xdotool', 'mouseup', '1'], check=True)
                time.sleep(.2)

            def rclick(x, y):
                at(x, y)
                time.sleep(.2)
                subprocess.run(['xdotool', 'mousedown', '3'], check=True)
                time.sleep(.08)
                subprocess.run(['xdotool', 'mouseup', '3'], check=True)
                time.sleep(.5)

            subprocess.run(['xdotool', 'windowactivate', '--sync', window,
                            'windowfocus', '--sync', window], check=True)
            time.sleep(.6)   # settle the focus before the first input
            key('7'); time.sleep(1.0)
            key('z'); time.sleep(1.5)
            landed = read()
            if landed['planet']['view'] != 'planet':
                failures.append(f"Z did not descend: {landed['planet']['view']}")
            if abs(landed['planet']['zoom'] - 1.0) > .2:
                failures.append(f"a world should open fitted to the window, zoom was {landed['planet']['zoom']}")
            start_zoom = landed['planet']['zoom']

            # Travel in: centre on the beacon, zoom, repeat. Exactly the loop a
            # player runs by hand.
            for _ in range(14):
                state = read()
                scale = state['planet']['zoom'] * min(width, height) * .92
                dx = (state['planet']['view_x'] - state['planet']['beacon_world_x']) * scale
                dy = (state['planet']['view_y'] - state['planet']['beacon_world_y']) * scale
                if abs(dx) > 6 or abs(dy) > 6:
                    drag(max(-420, min(420, dx)), max(-420, min(420, dy)))
                state = read()
                if state['planet']['beacon_on_screen']:
                    bx, by = state['planet']['beacon_x'], state['planet']['beacon_y']
                    if abs(bx - midX) < 90 and abs(by - midY) < 90 and state['planet']['zoom'] > 40:
                        break
                wheel(4, midX, midY)

            reached = read()
            summary = {'zoom_start': round(start_zoom, 2), 'zoom_reached': round(reached['planet']['zoom'], 1),
                       'beacon_on_screen': reached['planet']['beacon_on_screen'],
                       'beacon_xy': [reached['planet']['beacon_x'], reached['planet']['beacon_y']]}
            if not reached['planet']['beacon_on_screen']:
                failures.append('never brought the beacon on screen')
            else:
                rclick(reached['planet']['beacon_x'], reached['planet']['beacon_y'])
                after = read()
                summary['beacon_found'] = after['planet']['beacon_found']
                summary['unlocked'] = after['progress']['unlocked']
                if not after['planet']['beacon_found']:
                    failures.append('right-clicking the beacon did not find it')
                if after['progress']['unlocked'] != 2:
                    failures.append(f"finding it did not unseal: {after['progress']['unlocked']}")
                if after['rendered_frames'] <= landed['rendered_frames']:
                    failures.append('audio stalled inside the world')
            # Stay in the world until the capture has definitely fired.
            time.sleep(max(3.0, 16.0 - (time.time() - launched)))
            key('Escape'); time.sleep(.8)
            if read()['planet']['view'] != 'system':
                failures.append('Escape did not return to the system view')
        finally:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()
    print(json.dumps(summary, indent=2))
    print(f'screenshot: {SHOT} ({SHOT.stat().st_size if SHOT.exists() else 0} bytes)')
    if failures:
        for failure in failures:
            print(f'FAIL: {failure}', file=sys.stderr)
        return 1
    print('PASS: descend, pan and zoom in, beacon found, unseal, audio continuous, escape back')
    return 0


if __name__ == '__main__':
    sys.exit(main())
