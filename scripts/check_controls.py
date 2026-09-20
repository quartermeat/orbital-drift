"""Exercise real keyboard/mouse/audio/fullscreen: python3 scripts/check_controls.py."""
from pathlib import Path
import json
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    artifacts = ROOT / 'artifacts'
    artifacts.mkdir(exist_ok=True)
    state_path = artifacts / 'controls.json'
    state_path.unlink(missing_ok=True)
    # This checks input plumbing, not progression, so start from a fully
    # unsealed save and resume it: otherwise six of the seven tracks are
    # sealed and correctly refuse to toggle.
    (artifacts / 'progress.json').write_text('{"unlocked":7}\n')
    with (artifacts / 'controls.log').open('w') as log:
        app = subprocess.Popen([str(ROOT/'build/orbital-drift'),'--windowed','--resume',
                                '--state',str(state_path),'--seconds','40'],stdout=log,stderr=subprocess.STDOUT)
        def state():
            try:
                return json.loads(state_path.read_text())
            except (FileNotFoundError,json.JSONDecodeError):
                return {}
        def wait_for(predicate, message, timeout=10):
            end = time.monotonic()+timeout
            while time.monotonic()<end:
                current = state()
                if predicate(current):
                    return current
                if app.poll() is not None:
                    raise RuntimeError(f'App exited ({app.returncode}): {message}; see controls.log')
                time.sleep(.05)
            raise AssertionError(message)
        try:
            initial = wait_for(lambda s:s.get('rendered_frames',0)>10000,'audio did not start')
            assert initial['hardware_accelerated'] and 'NVIDIA' in initial['renderer']
            window = subprocess.check_output(['xdotool','search','--onlyvisible','--pid',str(app.pid),'--name','^Orbital Drift$'],text=True).splitlines()[0]
            def key(name):
                subprocess.run(['xdotool','windowactivate','--sync',window,'windowfocus','--sync',window,
                                'keydown','--clearmodifiers',name],check=True)
                # Hold through several frames: XTEST's default 12 ms tap can
                # begin and end between the 60 Hz renderer's input polls.
                time.sleep(.08)
                subprocess.run(['xdotool','keyup',name],check=True)
            # A run begins in silence now, so wake everything before testing
            # that each key mutes and unmutes its track.
            key('a')
            wait_for(lambda s:s.get('tracks') and all(t['enabled'] for t in s['tracks']),'ALL ON did not wake every track')
            for track in range(7):
                key(str(track+1))
                wait_for(lambda s:not s.get('tracks',[{}]*7)[track].get('enabled',True),f'track {track+1} did not mute')
                key(str(track+1))
                wait_for(lambda s:s.get('tracks',[{}]*7)[track].get('enabled',False),f'track {track+1} did not unmute')
            key('m')
            silent = wait_for(lambda s:s.get('tracks') and not any(t['enabled'] for t in s['tracks']) and s.get('output_rms',1)==0,
                              'all-off is not silent')
            time.sleep(.3)
            assert state()['position_frames']!=silent['position_frames'],'mute reset/stopped playback'
            key('a')
            wait_for(lambda s:s.get('output_rms',0)>.001 and all(t['enabled'] for t in s['tracks']),'all-on failed')
            key('space')
            wait_for(lambda s:s.get('playing') is False and s.get('output_rms',1)==0,'pause did not silence')
            time.sleep(.35)
            paused=state()['position_frames']
            time.sleep(.35)
            assert paused==state()['position_frames'],'pause playhead moved'
            key('space')
            wait_for(lambda s:s.get('playing') and s.get('position_frames')!=paused,'resume failed')
            size=state();w,h=size['width'],size['height'];unit=min(w/1600,h/900)
            margin,gap=52*unit,12*unit
            card_width=(w-2*margin-6*gap)/7
            subprocess.run(['xdotool','mousemove','--window',window,str(round(margin+card_width/2)),
                            str(round(h-169*unit+55*unit)),'mousedown','1'],check=True)
            time.sleep(.08)
            subprocess.run(['xdotool','mouseup','1'],check=True)
            wait_for(lambda s:s.get('tracks') and not s['tracks'][0]['enabled'],'mouse toggle failed')
            key('1')
            wait_for(lambda s:s.get('tracks') and s['tracks'][0]['enabled'],'mouse toggle recovery failed')
            key('F11')
            full=wait_for(lambda s:s.get('fullscreen') is True,'fullscreen failed')
            wm=subprocess.check_output(['xprop','-id',window,'_NET_WM_STATE'],text=True)
            assert '_NET_WM_STATE_FULLSCREEN' in wm,'window manager does not report fullscreen'
            subprocess.run(['import','-window',window,str(artifacts/'fullscreen.png')],check=True)
            key('F11')
            wait_for(lambda s:s.get('fullscreen') is False,'windowed restore failed')
            key('Escape')
            app.wait(timeout=10)
            assert app.returncode==0,'unclean shutdown'
            report={'passed':True,'gpu':full['renderer'],'fullscreen':[full['width'],full['height']],
                    'checks':['all seven keyboard toggles','mouse toggle','silent all-off','phase continuity',
                              'pause/resume','native fullscreen','windowed restore','clean exit']}
            (artifacts/'checks.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report,indent=2))
        finally:
            if app.poll() is None:
                app.terminate()
                try:app.wait(timeout=5)
                except subprocess.TimeoutExpired:app.kill();app.wait()


if __name__ == '__main__':
    main()
