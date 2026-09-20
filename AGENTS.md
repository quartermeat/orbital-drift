# Agent guide — Orbital Drift

Applies to `/home/quartermeat/work/orbital-drift`. The home `AGENTS.md` still
governs anything not covered here.

## What this is

A puzzle game where **the mix is the game state**. Each track of a Bitwig
project is bound to a visual layer; toggling a track toggles its layer, so
puzzles are solved by choosing which layers play. Bitwig is the level editor —
a `.dawproject` export carries the tracks, names, colours, and tempo that
define a level.

Read `design_ideas.md` for the reasoning, the stem-to-layer mapping, and the
open questions. It is design rationale, not spec; this file holds the rules.

## Commands

```
make setup    # fetch raylib 5.5 (checksum-pinned) + stems + font
make          # build build/orbital-drift
make test     # mixer + config unit tests, no audio device needed
make check    # test + --check-assets
make run      # fullscreen

python3 scripts/check_controls.py     # drives real input against a live window
python3 scripts/check_hot_reload.py   # edits assets under a live session
```

Runtime: fullscreen by default. `--windowed`, `--seconds N`, `--capture f.png`,
`--state f.json`, `--assets DIR`, `--check-assets`.
Keys: `1`–`7` toggle, `Space` pause, `M` all off/on, `A` all on, `+`/`-` volume,
`F11` fullscreen, `Esc` exit.

## Audio invariants — do not regress these

`src/mixer.hpp` is already correct. It is easy to "simplify" into something
broken, so treat the following as load-bearing:

- **One `AudioStream`, one shared playhead** (`Mixer::cursor`), all tracks
  decoded to RAM as float. Never `LoadMusicStream`/`LoadSound` per track —
  independent streams carry independent positions and will drift apart. Sync
  must stay true by construction, not maintained.
- **Looping is `cursor = (cursor+1) % frames`.** No seam handling, no restart.
- **Toggling is a gain change, never a stop.** A muted track keeps advancing, so
  unmuting drops back in where the music is. Visual layers must behave the same
  way — reappear mid-phrase, never restart.
- **Gains ramp** (`Mixer::approach`, ~20 ms). Hard jumps click. The stems' reverb
  and delay tails wrap across the loop boundary, so a stopped track also loses
  the tail belonging to the next loop's downbeat.
- **`Mixer::render` is the only function on the audio thread.** No allocation,
  file I/O, locks, or UI work in it. Cross-thread state stays `std::atomic`.
- Tracks must be equal length; `load()` rejects otherwise.

**The playhead is the game clock.** Drive animation from `Mixer::position`, not
frame time, so a pulsing platform and a kick drum are the same number. At 72 BPM:
beat 0.8333 s, bar 3.3333 s, loop 53.3333 s / 2,560,000 frames.

## Hot reload

`assets/space.fs` and `assets/layers.conf` are watched by mtime and re-read
while the app runs. `scripts/check_hot_reload.py` verifies the whole contract.

Rules:

- **Reloading data must never touch the mixer, the stems, or the playhead.**
  That is the entire point: the music plays straight through a reload. Anything
  that would re-decode audio or reset `Mixer::cursor` does not belong on a
  reload path.
- **Bad input keeps the previous value, never crashes.** A shader that fails to
  compile keeps the old shader; a malformed config line keeps the old value and
  reports a note. A typo saved during a live session must not kill the window
  or the audio. This is covered by a test — keep it that way.
- **Writes are debounced ~120 ms** (`Watched::changed`) so a half-saved file is
  never parsed mid-write.
- In `layers.conf`, a `#` comment must be on its own line — a trailing one is
  indistinguishable from a `#rrggbb` colour.
- Never call `UnloadShader` on a shader that failed to load without checking
  `rlGetShaderIdDefault()`; raylib returns the default program on failure and
  destroying it breaks all rendering.

### If code hot reload (a reloadable .so) is added later

The intended shape is a thin host plus a `dlopen`ed module. Before starting,
note the constraint that decides the whole design:

- **The audio callback must never live in the reloadable module.** The audio
  thread calls that pointer continuously; `dlclose` while it is executing is an
  immediate crash. `Mixer`, the stems, and `SetAudioStreamCallback` stay in the
  host permanently.
- Host owns window, GL context, audio device, and the state arena. The module
  gets a pointer and nothing else.
- Plain C ABI (`extern "C"`) across the boundary — C++ mangling is not a stable
  dlsym contract.
- No state in the module: no globals, no `static` locals, no singletons.
- No virtuals or `std::function` stored across a reload; their pointers refer
  into the old `.so` and dangle the moment it unloads.
- Copy the `.so` to a temp path before `dlopen` so a rebuild cannot write into
  the mapped file, and keep running the old module when `dlopen` fails.
- Put a version field at the top of the state struct and refuse the swap when
  the layout changed, rather than reinterpreting old bytes.

## Stay agent-inspectable

Per the home guide's mission, this app is driven headlessly by agents:
`--seconds N --capture out.png --state out.json` renders, screenshots, dumps
state, and exits.

**Every new feature must remain observable through `--state`.** When adding
layers, puzzles, or player state, extend that JSON in the same pass — an agent
that cannot read the result cannot verify its own work. Keep `--check-assets`
honest about anything newly required at load.

## Current state (v0.2.0)

First pass is working: fullscreen, hardware accelerated (verified on the
RTX 3090 Ti), seven stems in sync, per-track toggles with metering.

Known gaps, roughly in order:

1. **Tracks are still partly hardcoded.** Colours and role labels now come from
   `assets/layers.conf` and reload live, but `Files`/`Names` in `mixer.hpp` and
   `TrackCount = 7` are still compile-time constants. The design calls for
   reading names, colours, tempo, and initial mute states from the project's
   `project.xml`; `layers.conf` then becomes an override rather than the source.
   The colours in it are hand-picked approximations, not Bitwig's actual values.
2. **No `.dawproject` loading.** A `.dawproject` is a zip containing
   `project.xml` and `audio/NN Name.wav` — the stems are already in there, so
   loading the zip directly replaces the `assets/audio` copy. miniz ships with
   raylib. Make track count dynamic at the same time.
3. **`scripts/setup.py` is a fixture, not a loader.** It copies seven specific
   stems from `~/Music/Orbital Drift` and hard-fails otherwise. Keep it for
   raylib/font and as a known-good test fixture; it must not stay the only way
   to get audio in.
4. **No visual layers bound to tracks yet.** The current display visualises the
   mixer; it does not implement the mechanic. Start with Starlight Echoes —
   solid after-images at its dotted-eighth delay (0.625 s) — which demonstrates
   the whole thesis on one screen.
5. **No puzzle sidecar.** `project.xml` cannot carry goal state, mix budget, or
   layer geometry. That needs a sidecar keyed by track id, beside the project,
   leaving the Bitwig file untouched and re-exportable.

The WAV loader accepts only 48 kHz / 24-bit stereo PCM. Fine for the fixture;
revisit when arbitrary projects load.

## Conventions

- C++20, raylib 5.5 static, built with `-Wall -Wextra -Wpedantic`. Keep it warning-clean.
- Match the existing dense style in `src/`; don't reformat wholesale.
- Cover mixer behaviour in `tests/mixer_test.cpp` and config parsing in
  `tests/config_test.cpp`; both run without an audio device or a display.
- Per the home guide: **Python** for one-off setup scripts, **Go** for anything
  long-lived. Game code is C++ because raylib is.
- Generated output (`build/`, `artifacts/`, `assets/audio/`, `assets/font.ttf`)
  is gitignored. Don't commit stems.

## Versioning

Home guide rule applies: every commit advances `MAJOR.MINOR.PATCH` in `VERSION`,
the subject is prefixed `vX.Y.Z: `, and an annotated tag `vX.Y.Z` points at it.

**This is not a git repository yet.** `VERSION` says `0.1.0`. Initialising it is
a user decision — ask before running `git init`, and don't push without explicit
intent.
