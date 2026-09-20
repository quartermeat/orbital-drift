"""Save the first-level background of every world: python3 scripts/preview_worlds.py

One short run per world using the app's own screenshot path, which is the only
capture that reliably works under a compositor.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts' / 'worlds'
NAMES = ['01-nebula-pad', '02-starlight-echoes', '03-cosmic-dust', '04-warm-sub',
         '05-soft-kick', '06-distant-snare', '07-orbit-hats']


def shoot(index, name, extra=()):
    target = OUT / f'{name}.png'
    subprocess.run([str(ROOT / 'build/orbital-drift'), '--windowed', '--world', str(index),
                    '--capture', str(target), '--capture-after', '2.5', '--seconds', '4', *extra],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    return target


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob('*.png'):
        old.unlink()
    made = []
    for index, name in enumerate(NAMES):
        target = shoot(index, name)
        if target.exists():
            made.append(target)
            print(f'{name:26} {target.stat().st_size:>9,} bytes')
        else:
            print(f'{name:26} MISSING', file=sys.stderr)
    digests = {subprocess.check_output(['md5sum', str(p)], text=True).split()[0] for p in made}
    print(f'\n{len(made)} images, {len(digests)} distinct, in {OUT}')
    return 0 if len(made) == len(NAMES) and len(digests) == len(NAMES) else 1


if __name__ == '__main__':
    sys.exit(main())
