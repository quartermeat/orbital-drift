"""Prepare bundled assets and pinned build dependencies: python3 scripts/setup.py."""
from pathlib import Path
import hashlib
import json
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / 'build' / 'deps'
URL = 'https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz'
SHA256 = 'aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e'


def main():
    DEPS.mkdir(parents=True, exist_ok=True)
    archive = DEPS / 'raylib-5.5.tar.gz'
    if not archive.exists():
        print('Downloading raylib 5.5...', flush=True)
        temp = archive.with_suffix('.part')
        urllib.request.urlretrieve(URL, temp)
        temp.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SHA256:
        raise RuntimeError('raylib archive checksum mismatch; refusing extraction')
    print('raylib archive SHA256:', digest, flush=True)
    if not (DEPS / 'raylib-5.5' / 'src' / 'raylib.h').exists():
        with tarfile.open(archive) as source:
            source.extractall(DEPS, filter='data')
    audio = ROOT / 'assets' / 'audio'
    audio.mkdir(parents=True, exist_ok=True)
    source = Path.home() / 'Music' / 'Orbital Drift' / 'audio'
    manifest = json.loads((ROOT / 'assets' / 'manifest.json').read_text())
    if len(manifest) != 7:
        raise RuntimeError('Expected seven entries in the asset manifest')
    for entry in manifest:
        name = entry['file']
        if Path(name).name != name:
            raise RuntimeError('Invalid asset filename in manifest')
        target = audio / name
        if not target.exists():
            file = source / name
            if not file.exists():
                raise RuntimeError(
                    f'Missing {name}. Download the full source bundle (includes music) from '
                    'https://github.com/quartermeat/orbital-drift/releases')
            shutil.copyfile(file, target)
        if hashlib.sha256(target.read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError(f'Existing asset differs; preserved: {target}')
    font = ROOT / 'assets' / 'font.ttf'
    if not font.exists():
        shutil.copyfile('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',font)
    license_file = ROOT / 'assets' / 'FONT-LICENSE.txt'
    if not license_file.exists():
        shutil.copyfile('/usr/share/doc/fonts-dejavu-core/copyright',license_file)
    print('Seven original tracks and font ready.', flush=True)


if __name__ == '__main__':
    main()
