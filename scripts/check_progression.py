"""Play the unlock loop for real: python3 scripts/check_progression.py

Starts a fresh run, confirms sealed tracks refuse to sound, waits for the
frontier sigil to appear, clicks it, and confirms the unlock persists across a
restart. Uses xdotool against a real window.
"""
from pathlib import Path
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'build/orbital-drift'
STATE = ROOT / 'artifacts' / 'progression.json'
PROGRESS = ROOT / 'artifacts' / 'progress.json'


def read(path, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            return json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            time.sleep(.1)
    raise RuntimeError(f'no state at {path}')


def launch(log, extra=()):
    return subprocess.Popen([str(APP), '--windowed', '--state', str(STATE),
                             '--seconds', '70', *extra], stdout=log, stderr=subprocess.STDOUT)


def window_for(pid):
    for _ in range(50):
        try:
            found = subprocess.check_output(['xdotool', 'search', '--onlyvisible', '--pid', str(pid),
                                             '--name', '^Orbital Drift$'], text=True).splitlines()
            if found:
                return found[0]
        except subprocess.CalledProcessError:
            pass
        time.sleep(.2)
    raise RuntimeError('window never appeared')


def main():
    failures = []
    ROOT.joinpath('artifacts').mkdir(exist_ok=True)
    STATE.unlink(missing_ok=True)

    with (ROOT / 'artifacts' / 'progression.log').open('w') as log:
        app = launch(log, ['--reset-progress'])
        try:
            window = window_for(app.pid)
            time.sleep(2.5)
            first = read(STATE)

            if first['progress']['unlocked'] != 1:
                failures.append(f"fresh run opened {first['progress']['unlocked']} tracks, expected 1")
            if first['progress']['frontier'] != 'Orbit Hats':
                failures.append(f"frontier was {first['progress']['frontier']}, expected the rightmost track")
            open_now = [t['name'] for t in first['tracks'] if t['unlocked']]
            if open_now != ['Orbit Hats']:
                failures.append(f'unlocked set was {open_now}')
            if any(t['enabled'] for t in first['tracks'] if not t['unlocked']):
                failures.append('a sealed track was sounding')

            # A sealed track must refuse the key that would toggle it.
            geometry = subprocess.check_output(['xdotool', 'getwindowgeometry', '--shell', window], text=True)
            info = dict(line.split('=', 1) for line in geometry.strip().splitlines())
            subprocess.run(['xdotool', 'windowactivate', '--sync', window, 'key', '--window', window, '1'], check=True)
            time.sleep(1.0)
            if read(STATE)['tracks'][0]['enabled']:
                failures.append('pressing 1 enabled a sealed track')

            # Wait for the sigil, then click it.
            x, y, w, h = (int(info[k]) for k in ('X', 'Y', 'WIDTH', 'HEIGHT'))
            radius = min(w * .31, h * .34)
            sigil_x = x + w * .5 - radius * (.43 + 6 * .092)
            sigil_y = y + h * .435
            deadline = time.time() + 30
            clicked = False
            while time.time() < deadline and not clicked:
                if read(STATE)['progress']['sigil_visible']:
                    subprocess.run(['xdotool', 'mousemove', str(int(sigil_x)), str(int(sigil_y)),
                                    'click', '1'], check=True)
                    time.sleep(.8)
                    clicked = read(STATE)['progress']['unlocked'] > 1
                time.sleep(.15)
            if not clicked:
                failures.append('sigil never became clickable within 30s')

            after = read(STATE)
            if after['progress']['unlocked'] != 2:
                failures.append(f"after unseal unlocked was {after['progress']['unlocked']}, expected 2")
            if after['progress']['frontier'] != 'Distant Snare':
                failures.append(f"frontier did not move left: {after['progress']['frontier']}")
            if not any(t['name'] == 'Distant Snare' and t['enabled'] for t in after['tracks']):
                failures.append('the newly unsealed track did not start sounding')
            if after['rendered_frames'] <= first['rendered_frames']:
                failures.append('audio stalled across the unlock')
            saved = json.loads(PROGRESS.read_text())
        finally:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()

        # Progress must survive a restart.
        app = launch(log)
        try:
            window_for(app.pid)
            time.sleep(2.5)
            resumed = read(STATE)
            if resumed['progress']['unlocked'] != 2:
                failures.append(f"restart lost progress: {resumed['progress']['unlocked']}")
        finally:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()

    print(json.dumps({'saved_file': saved, 'unlocked_after_click': after['progress']['unlocked'],
                      'frontier_after_click': after['progress']['frontier'],
                      'unlocked_after_restart': resumed['progress']['unlocked']}, indent=2))
    if failures:
        for failure in failures:
            print(f'FAIL: {failure}', file=sys.stderr)
        return 1
    print('PASS: sealed tracks refuse input, sigil unseals, audio continuous, progress persists')
    return 0


if __name__ == '__main__':
    sys.exit(main())
