"""Play the unlock loop for real: python3 scripts/check_progression.py

Confirms a run starts silent with one track unsealed, that a sealed track
refuses input, that switching the open track on exposes its sigil, that a right
click unseals the next track, and that a launch resets unless --resume is
given. Uses xdotool against a real window.
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
        app = launch(log)
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
            if any(t['enabled'] for t in first['tracks']):
                failures.append('a run did not start silent')
            if first['progress']['sigil_visible']:
                failures.append('the sigil was exposed before its track was switched on')

            geometry = subprocess.check_output(['xdotool', 'getwindowgeometry', '--shell', window], text=True)
            info = dict(line.split('=', 1) for line in geometry.strip().splitlines())

            def key(name):
                subprocess.run(['xdotool', 'windowactivate', '--sync', window, 'windowfocus', '--sync', window,
                                'keydown', '--clearmodifiers', name], check=True)
                # Hold through several frames: XTEST's default 12 ms tap can
                # begin and end between the 60 Hz renderer's input polls.
                time.sleep(.08)
                subprocess.run(['xdotool', 'keyup', name], check=True)

            def click(button):
                # --window makes the move client-relative, avoiding the frame
                # offset that xdotool's absolute X/Y carries on reparented
                # windows. Press and release are held apart for the same reason
                # keys are: a 12 ms XTEST tap can land entirely between two of
                # the 60 Hz renderer's input polls.
                subprocess.run(['xdotool', 'windowactivate', '--sync', window,
                                'mousemove', '--window', window, str(sigil_x), str(sigil_y)], check=True)
                time.sleep(.2)
                subprocess.run(['xdotool', 'mousedown', str(button)], check=True)
                time.sleep(.08)
                subprocess.run(['xdotool', 'mouseup', str(button)], check=True)

            # Where the app actually drew it, not a second copy of the layout maths.
            sigil_x = sigil_y = 0

            # Wake the open track FIRST. This proves key injection works, so the
            # sealed-track check below cannot pass just because nothing arrived.
            key('7')
            time.sleep(1.2)
            woken = read(STATE)
            if not any(t['name'] == 'Orbit Hats' and t['enabled'] for t in woken['tracks']):
                failures.append('pressing 7 did not wake the unsealed track')
            if not woken['progress']['sigil_visible']:
                failures.append('sigil was not exposed once its track was on')

            # Now a sealed track must refuse the same, proven mechanism.
            key('1')
            time.sleep(1.0)
            if read(STATE)['tracks'][0]['enabled']:
                failures.append('pressing 1 enabled a sealed track')

            # Switching the layer off must hide the sigil with it.
            key('7')
            time.sleep(1.2)
            if read(STATE)['progress']['sigil_visible']:
                failures.append('sigil stayed exposed after its track was switched off')
            key('7')
            time.sleep(1.2)
            if not read(STATE)['progress']['sigil_visible']:
                failures.append('sigil did not come back when the track returned')

            # The sigil is a doorway now: clicking it descends to that world.
            # Unsealing is earned on the planet, covered by check_planet.py.
            live = read(STATE)
            sigil_x, sigil_y = live['progress']['sigil_x'], live['progress']['sigil_y']
            print(f'sigil at ({sigil_x},{sigil_y}) in a {info["WIDTH"]}x{info["HEIGHT"]} window')
            click(3)
            time.sleep(1.4)

            after = read(STATE)
            if after['planet']['view'] != 'planet':
                failures.append(f"clicking the sigil did not descend: view {after['planet']['view']}")
            if after['planet']['track'] != 'Orbit Hats':
                failures.append(f"descended to the wrong world: {after['planet']['track']}")
            if after['progress']['unlocked'] != 1:
                failures.append('the sigil unsealed a track directly; it should only open the world')
            if after['rendered_frames'] <= first['rendered_frames']:
                failures.append('audio stalled across the descent')
        finally:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()

        # A plain launch starts over; --resume picks the saved run back up.
        app = launch(log)
        try:
            window_for(app.pid)
            time.sleep(2.5)
            restarted = read(STATE)
            if restarted['progress']['unlocked'] != 1:
                failures.append(f"a plain relaunch resumed at {restarted['progress']['unlocked']}, expected a reset")
        finally:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()

        # The reset above overwrote the save, so re-earn an unlock to test resume.
        PROGRESS.write_text('{"unlocked":3}\n')
        app = launch(log, ['--resume'])
        try:
            window_for(app.pid)
            time.sleep(2.5)
            resumed = read(STATE)
            if resumed['progress']['unlocked'] != 3:
                failures.append(f"--resume gave {resumed['progress']['unlocked']}, expected 3")
            if any(t['enabled'] for t in resumed['tracks']):
                failures.append('a resumed run did not start silent')
        finally:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()

    print(json.dumps({'view_after_sigil_click': after['planet']['view'],
                      'world_entered': after['planet']['track'],
                      'unlocked_still': after['progress']['unlocked'],
                      'plain_relaunch': restarted['progress']['unlocked'],
                      'with_resume': resumed['progress']['unlocked']}, indent=2))
    if failures:
        for failure in failures:
            print(f'FAIL: {failure}', file=sys.stderr)
        return 1
    print('PASS: silent start, sealed input refused, layer exposes sigil, sigil descends without unsealing, reset by default, --resume works')
    return 0


if __name__ == '__main__':
    sys.exit(main())
