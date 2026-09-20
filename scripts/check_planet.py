"""Descend to a planet and find the beacon: python3 scripts/check_planet.py

Wakes the open track, zooms into its world, orbits until the beacon comes into
view, right-clicks it, and checks that finding it unseals the next track.
Captures a screenshot of the planet for eyeballing.
"""
from pathlib import Path
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / 'artifacts' / 'planet.json'
SHOT = ROOT / 'artifacts' / 'planet.png'


def read(timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            return json.loads(STATE.read_text())
        except (OSError, json.JSONDecodeError):
            time.sleep(.1)
    raise RuntimeError('no state')


def main():
    failures = []
    summary = {}
    ROOT.joinpath('artifacts').mkdir(exist_ok=True)
    STATE.unlink(missing_ok=True)
    with (ROOT / 'artifacts' / 'planet.log').open('w') as log:
        app = subprocess.Popen([str(ROOT / 'build/orbital-drift'), '--windowed', '--state', str(STATE),
                                '--capture', str(SHOT), '--capture-after', '7', '--seconds', '80'], stdout=log, stderr=subprocess.STDOUT)
        try:
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

            def key(name, hold=.08):
                subprocess.run(['xdotool', 'windowactivate', '--sync', window, 'windowfocus', '--sync', window,
                                'keydown', '--clearmodifiers', name], check=True)
                time.sleep(hold)
                subprocess.run(['xdotool', 'keyup', name], check=True)

            def rclick(x, y):
                subprocess.run(['xdotool', 'windowactivate', '--sync', window,
                                'mousemove', '--window', window, str(int(x)), str(int(y))], check=True)
                time.sleep(.2)
                subprocess.run(['xdotool', 'mousedown', '3'], check=True)
                time.sleep(.08)
                subprocess.run(['xdotool', 'mouseup', '3'], check=True)

            if read()['planet']['view'] != 'system':
                failures.append('did not start in the system view')
            key('7'); time.sleep(1.0)          # wake the open track
            key('z'); time.sleep(2.0)          # descend to its world
            landed = read()
            if landed['planet']['view'] != 'planet':
                failures.append(f"Z did not descend: view is {landed['planet']['view']}")
            if landed['planet']['track'] != 'Orbit Hats':
                failures.append(f"descended to {landed['planet']['track']}")
            if not (1.5 < landed['planet']['camera_distance'] < 12):
                failures.append(f"camera distance looks wrong: {landed['planet']['camera_distance']}")

            # Orbit until the beacon rotates into view, the way a player would.
            spun = 0
            while spun < 40 and not read()['planet']['beacon_on_screen']:
                key('Right', .25)
                time.sleep(.25)
                spun += 1
            state = read()
            summary = {'orbit_steps': spun, 'beacon_on_screen': state['planet']['beacon_on_screen'],
                       'beacon_xy': [state['planet']['beacon_x'], state['planet']['beacon_y']],
                       'camera_distance': round(state['planet']['camera_distance'], 2),
                       'view': state['planet']['view'], 'track': state['planet']['track']}
            if not state['planet']['beacon_on_screen']:
                failures.append('beacon never came into view after a full orbit')
            else:
                rclick(state['planet']['beacon_x'], state['planet']['beacon_y'])
                time.sleep(1.2)
                after = read()
                if not after['planet']['beacon_found']:
                    failures.append('right-clicking the beacon did not find it')
                if after['progress']['unlocked'] != 2:
                    failures.append(f"finding it did not unseal: unlocked {after['progress']['unlocked']}")
                if after['rendered_frames'] <= landed['rendered_frames']:
                    failures.append('audio stalled while on the planet')
                summary.update({'beacon_found': after['planet']['beacon_found'],
                                'unlocked': after['progress']['unlocked']})
            time.sleep(4)   # let the capture land while still on the planet
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
    print('PASS: descend, orbit, beacon found by right click, unseal, audio continuous, escape back')
    return 0


if __name__ == '__main__':
    sys.exit(main())
