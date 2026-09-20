# Orbital Drift

A native C++ music-game foundation: fullscreen OpenGL space graphics and seven
independently switchable, synchronized stems from the original Orbital Drift session.
Version **0.1.0**. This first milestone is an interactive sound scene; gameplay comes next.

## Build and run

```sh
python3 scripts/setup.py
make -j4
./build/orbital-drift
```

Starts fullscreen with all seven tracks playing. No root privileges or system
package installation is needed on this workstation. The build uses g++, make,
OpenGL and X11 development headers already installed here. The setup script
downloads checksum-pinned raylib 5.5 into `build/deps/`, copies the original
48 kHz / 24-bit stems from `~/Music/Orbital Drift/audio/`, and copies the installed
DejaVu Sans font. Assets are local to this project after setup, so Bitwig is not
required to run the app. Raylib's license is in `build/deps/raylib-5.5/LICENSE`;
the font license is `assets/FONT-LICENSE.txt`. `assets/manifest.json` records stem hashes.

## Controls

| Input | Action |
| --- | --- |
| Click a sound source or its track card | Toggle that track |
| 1–7 | Toggle Pad, Echoes, Dust, Sub, Kick, Snare, Hats |
| Space / Play-Pause button | Pause or resume the whole loop |
| M | All off / all on |
| A / All On button | Enable every track |
| All Off button | Mute every track |
| + / - / volume slider | Master volume |
| F11 | Switch fullscreen/windowed |
| Escape | Exit |

Muted tracks continue moving through the same 16-bar, 72 BPM loop. Every stem is
mixed in one audio callback against one sample cursor. Enabling a track restores
its current point in the song, with a 20 ms gain ramp to avoid abrupt clicks.
Pausing fades the whole mix and freezes that cursor. Output begins at 80% master
volume; original stem levels and the mixer leave headroom. Audio uses raylib's
miniaudio backend (PulseAudio/PipeWire on this machine), independently of Bitwig.

The space background is a GLSL fragment shader; stars, orbits, audio-reactive
nodes and UI are rendered through OpenGL 3.3. The app checks `GL_RENDERER` and
refuses known software rasterizers. Fullscreen follows the current monitor's
native resolution, with a 60 FPS target. The shader renders at 960 × 540 and is
upscaled; track controls and orbit geometry render at native resolution.

## Verification and agent controls

```sh
make check
python3 scripts/check_controls.py
```

`make check` exercises the mixer without audio hardware and verifies all seven
real assets, lengths, stereo format and mix headroom. The UI check opens the real
app, sends keyboard/mouse input, tests silence, pause/resume and fullscreen,
then exits. It requires the desktop's X11 session and `xdotool`, `xprop`, and
ImageMagick `import`. It briefly takes focus. Results and screenshots are saved
under `artifacts/`.

For a timed smoke run:

```sh
./build/orbital-drift --windowed --seconds 5 --capture artifacts/smoke.png --state artifacts/smoke.json
```

The default live status file is `artifacts/state.json`, atomically refreshed five
times a second: GPU/vendor, fullscreen dimensions, FPS, audio health, shared
sample position, output RMS, master volume, and every track's enabled state and
level. Startup/errors and track transitions are also written to stdout/stderr.
`--assets /path/to/assets` overrides the asset root; `--help` lists all options.
SIGINT/SIGTERM stop the app cleanly. Missing or incompatible assets and graphics
or audio startup failures return a nonzero exit status.

Source layout: `src/mixer.hpp` is the independent audio engine, `src/main.cpp`
owns input/rendering/audio-device lifecycle, and `assets/space.fs` is the GPU
background. No project publishing, autostart service, or login behavior is configured.
