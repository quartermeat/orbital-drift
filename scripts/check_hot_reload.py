"""Prove data hot reload works and never interrupts audio: python3 scripts/check_hot_reload.py

Drives a real windowed session, edits assets underneath it, and checks the
reload counters in --state. The important assertion is that the mixer keeps
rendering frames straight through every reload, including a broken shader.
"""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'assets' / 'layers.conf'
SHADER = ROOT / 'assets' / 'space.fs'


def main():
    artifacts = ROOT / 'artifacts'
    artifacts.mkdir(exist_ok=True)
    state_path = artifacts / 'hot-reload.json'
    state_path.unlink(missing_ok=True)
    config_backup, shader_backup = CONFIG.read_text(), SHADER.read_text()
    failures, notes = [], []

    def state(timeout=6.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                return json.loads(state_path.read_text())
            except (OSError, json.JSONDecodeError):
                time.sleep(.1)
        raise RuntimeError('app never wrote state')

    def settle(seconds=1.6):
        time.sleep(seconds)

    with (artifacts / 'hot-reload.log').open('w') as log:
        app = subprocess.Popen([str(ROOT / 'build/orbital-drift'), '--windowed',
                                '--state', str(state_path), '--seconds', '26'],
                               stdout=log, stderr=subprocess.STDOUT)
        try:
            settle(3)
            first = state()
            if not first['playing']:
                failures.append('audio was not playing at start')

            # 1. Config reload.
            CONFIG.write_text(config_backup.replace('star.count   = 260', 'star.count   = 40')
                                           .replace('track.1.color = af95f6', 'track.1.color = ff2288'))
            settle(2.5)
            after_config = state()
            if after_config['hot_reload']['config_reloads'] <= first['hot_reload']['config_reloads']:
                failures.append('layers.conf edit did not trigger a reload')
            if after_config['hot_reload']['last_error']:
                failures.append(f"clean config reported an error: {after_config['hot_reload']['last_error']}")
            if after_config['rendered_frames'] <= first['rendered_frames']:
                failures.append('audio stalled across the config reload')

            # 2. Shader reload with a valid edit.
            SHADER.write_text(shader_backup.rstrip() + '\n// touched by check_hot_reload\n')
            settle(2.5)
            after_shader = state()
            if after_shader['hot_reload']['shader_reloads'] <= first['hot_reload']['shader_reloads']:
                failures.append('space.fs edit did not trigger a reload')
            if after_shader['rendered_frames'] <= after_config['rendered_frames']:
                failures.append('audio stalled across the shader reload')

            # 3. The one that matters: a shader that does not compile must not
            #    take down the session or the audio.
            SHADER.write_text('#version 330\nthis is not glsl at all;\n')
            settle(2.5)
            broken = state()
            if app.poll() is not None:
                failures.append('app exited when a broken shader was saved')
            if not broken['hot_reload']['last_error']:
                failures.append('broken shader did not report an error')
            if broken['rendered_frames'] <= after_shader['rendered_frames']:
                failures.append('audio stalled after a broken shader')
            if not broken['playing']:
                failures.append('playback stopped after a broken shader')
            notes.append(f"broken-shader error: {broken['hot_reload']['last_error']!r}")

            # 4. Recovery: restoring a good shader reloads again.
            SHADER.write_text(shader_backup)
            settle(2.5)
            recovered = state()
            if recovered['hot_reload']['shader_reloads'] <= after_shader['hot_reload']['shader_reloads']:
                failures.append('good shader did not reload after a broken one')
            if recovered['hot_reload']['last_error']:
                failures.append('error was not cleared after recovery')

            summary = {
                'config_reloads': recovered['hot_reload']['config_reloads'],
                'shader_reloads': recovered['hot_reload']['shader_reloads'],
                'frames_rendered': recovered['rendered_frames'] - first['rendered_frames'],
                'audio_continuous': not any('stall' in f or 'stopped' in f for f in failures),
                'survived_broken_shader': app.poll() is None,
            }
        finally:
            CONFIG.write_text(config_backup)
            SHADER.write_text(shader_backup)
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()

    for note in notes:
        print(f'note: {note}')
    print(json.dumps(summary, indent=2))
    if failures:
        for failure in failures:
            print(f'FAIL: {failure}', file=sys.stderr)
        return 1
    print('PASS: config reload, shader reload, broken-shader survival, recovery, audio never stalled')
    return 0


if __name__ == '__main__':
    sys.exit(main())
