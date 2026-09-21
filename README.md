# Orbital Drift

A music puzzle game where the mix is the world. Switch on a track, enter its
island, and find someone in the crowd to unlock the next layer of the song.
Seven synchronized stems become seven overlapping casts of characters.

**Early development · v0.22.0 · Linux x86-64**

[Website and downloads](https://quartermeat.github.io/orbital-drift/) ·
[Releases](https://github.com/quartermeat/orbital-drift/releases)

## Download and play

Download `orbital-drift-0.22.0-linux-x86_64.tar.gz` from the release page.
It includes the executable, all seven original music stems, the font, shaders,
and campaign files. Bitwig is not needed.

```sh
tar -xzf orbital-drift-0.22.0-linux-x86_64.tar.gz
cd orbital-drift-0.22.0-linux-x86_64
./build/orbital-drift --dev
```

Keep the extracted folder together. Run from a writable location: saves and
live status go into its `artifacts/` directory. Dev mode draws skit outlines
and lets you press `G` inside a world to jump to the target. Add `--windowed`
for a window or `--resume` to load the previous run. Omit `--dev` for the
regular game view.

The prebuilt binary was built and tested on Debian 13 x86-64 with NVIDIA
hardware. It requires glibc 2.38+, a C++ runtime providing GLIBCXX_3.4.32+
(GCC 13.2 or newer), X11 or XWayland, hardware OpenGL 3.3, and desktop audio
(PulseAudio/PipeWire or ALSA). Other Linux distributions are not yet tested.
If your C/C++ runtime is older, build the source bundle on your machine.
Windows, macOS, and ARM builds are not supplied.

Typical Debian/Ubuntu runtime packages, if missing:

```sh
sudo apt install libgl1 libstdc++6 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6 libasound2t64 libpulse0
```

Use the graphics driver appropriate to your GPU. Software rendering is rejected
by the game. If startup fails, launch it from a terminal and read the error.

## Build the full source bundle

Download `orbital-drift-0.22.0-source.tar.gz` from the release page. This is the
complete build bundle, including music, font, and the checksum-pinned raylib
5.5 source archive. GitHub's automatically generated "Source code" ZIP/tar.gz
and a Git clone do **not** contain the music or font.

On Debian/Ubuntu, install the build prerequisites if needed:

```sh
sudo apt install build-essential python3 xorg-dev libgl1-mesa-dev
```

These X11 development packages follow the upstream
[GLFW build guidance](https://www.glfw.org/docs/latest/compile.html).
Use Python 3.12+ (or a Python 3.11 build with tar extraction filters), make,
and a C++20 compiler.

```sh
tar -xzf orbital-drift-0.22.0-source.tar.gz
cd orbital-drift-0.22.0-source
python3 scripts/setup.py
make -j4
make run
```

`make run` launches in dev mode. The complete source bundle builds without a
network connection once system prerequisites are installed. Setup verifies the
bundled stems against `assets/manifest.json` and the raylib archive against its
pinned SHA-256. It does not require a personal Music directory. For a Git clone,
copy `assets/audio/` and `assets/font.ttf` from either full release bundle first;
setup can also copy the original stems from `~/Music/Orbital Drift/audio/`.

## How to play

A fresh run starts silent, with only Orbit Hats unlocked. Switch it on, then
enter its world by clicking its orbit node or pressing `Z`. Match the person
shown in the find panel to someone in the crowd. Claiming the correct person
unlocks the next track and adds more music and people to the worlds.

| Input | Action |
| --- | --- |
| 1–7 / track cards | Toggle unlocked tracks |
| Click an orbit node / Z | Enter a world / the frontier world |
| Drag / arrow keys | Pan inside a world |
| Wheel / W / S | Zoom inside a world |
| Click the matching person | Claim the find |
| G, in dev mode | Jump to the target |
| Space | Pause or resume the shared loop |
| M / A | All off/on / all unlocked tracks on |
| + / - | Master volume |
| F11 | Toggle fullscreen |
| Escape | Back to the galaxy; exit from there |

Muted tracks keep advancing on the same sample clock, so bringing one back
never restarts its phrase. The campaign is 16 bars at 72 BPM.

## Checks and release packaging

```sh
make ci
```

This is the full pipeline: build, mixer and scene tests, asset and campaign
checks, interface contracts, dependency checks, object recognition, and live
controls/progression checks. It also needs Go, `xdotool`, `xprop`, an idle X11
desktop, and Ollama with a configured vision model (see `ci/recognition.yaml`).
Those tools are **not** required to play or compile the game.

For a timed dev smoke run:

```sh
./build/orbital-drift --dev --windowed --seconds 5 --capture artifacts/smoke.png --state artifacts/smoke.json
```

The live state file exposes GPU, fullscreen dimensions, FPS, audio health,
playhead, track states, and progression. Startup errors return nonzero exit
status; SIGINT/SIGTERM stop the app cleanly.

After validation and committing the release version:

```sh
go run tools/release/main.go
```

This writes binary and complete-source archives plus `SHA256SUMS.txt` into
`build/releases/vVERSION/`. It refuses a dirty tree, checks the binary version
and stem hashes, and packages only an explicit file list. Saves, screenshots,
personal paths in archive metadata, and Git data are excluded. Existing archives
are never overwritten. Run `sha256sum -c SHA256SUMS.txt` beside the downloads to
verify them.

## Third-party components

The release includes raylib's license at `build/deps/raylib-5.5/LICENSE`, its
embedded dependency sources and notices under `build/deps/raylib-5.5/src/external/`,
and the font notices at `assets/FONT-LICENSE.txt`.
