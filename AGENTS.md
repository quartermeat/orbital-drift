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
python3 scripts/check_progression.py  # sigil opens the world without unsealing
python3 scripts/check_canvas.py       # descends, zooms in, finds the beacon, screenshots it
```

Runtime: fullscreen by default. `--windowed`, `--seconds N`, `--capture f.png`,
`--capture-after N`, `--state f.json`, `--assets DIR`, `--check-assets`, `--resume`.
System view: `1`–`7` toggle, click an orbit node to descend, `Z` drops into the
frontier world, `Space` pause, `M` all off/on, `A` all on, `+`/`-` volume,
`F11` fullscreen, `Esc` exit.
World view: drag to pan, wheel or `W`/`S` to zoom at the cursor, arrows to pan,
right-click the beacon, `Esc` back. A minimap shows where you are.

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

## Progression

**A run begins in silence.** Nothing plays until the player switches on the one
available track. Tracks unlock **right to left**: a fresh run opens only track 7
(Orbit Hats); every other track is sealed — silent, un-toggleable, its name
hidden. The leftmost unlocked track is the *frontier*.

A track's **visual layer is exposed exactly while that track is switched on**,
and the layer carries a **sigil** on its orbit, at the point nearest the sealed
tracks. **Right-click the sigil to unseal** the next track, which joins the mix
immediately and moves the frontier one step left.

Rules:

- **Exposure is the only condition.** The sigil is visible whenever its track is
  on, and hidden when it is off. There is no timing window and no rhythmic
  gate — the clue is found by switching a layer on and looking, not by waiting
  for a moment. An earlier build gated it on the track being audibly sounding;
  that was removed deliberately, so do not reintroduce a timing condition.
- **Left click toggles, right click unseals.** Keeping them on separate buttons
  is what lets the sigil sit anywhere in a layer without colliding with the
  node and card hit boxes.
- **Unlock order is fixed and derived, never stored per track.** `Progress`
  holds one integer; `isUnlocked(i)` is `i >= TrackCount - unlocked`. Do not
  add a per-track unlocked flag — one integer is the whole save file.
- **A sealed track must never sound.** It cannot be toggled, and `ALL ON` / `M`
  use `progress.mask()`, not `AllTracks`. A sealed track appearing in the mix is
  the bug this design most needs protecting from.
- **State is saved every run, but a launch starts over.** `--resume` opts into
  the previous run. Resuming will matter once progression is long enough to be
  worth keeping; until then a predictable fresh start is worth more in testing.
  A missing or corrupt save starts fresh rather than failing, and out-of-range
  values are clamped.

The musical consequence is worth knowing: right-to-left means a run *starts*
with Orbit Hats (swung eighths) and *ends* with Nebula Pad (the harmonic bed),
so the world gains atmosphere as it fills in, inverting the usual build. If the
intent was to begin with the pad, the order reverses and the sigil moves to the
innermost orbit.

## Worlds

Every track owns a world: a 2D artwork you pan and zoom into without end.
Waking a track lets you click its orbit node (or press `Z`) to descend. Drag to
pan, wheel or `W`/`S` to zoom toward the cursor. Somewhere in there is the
beacon; right-click it to unseal the next track.

**Nothing is stored.** The canvas is a recursive grid — the root square is
[0,1]x[0,1], every node holds a handful of motifs plus `Branch`^2 children, and
a node's contents come from hashing its path. Zooming makes deeper nodes large
enough to draw, so detail keeps arriving for as long as you keep going, at no
memory cost. This is why it can be endless.

**The hunt is a conjunction search, and that is the whole design.** The beacon
is the only motif that is both a **spire** *and* wearing the **track's own
colour**. Generation *refuses that pairing everywhere else*, which is what makes
the beacon globally unique without anything being stored or searched. Plenty of
spires wear other colours and plenty of other shapes wear the track colour, so
neither feature alone narrows it down. Remove either guarantee and the search
becomes a pop-out, and the game becomes nothing.

Rules:

- **The sigil is a doorway, not a shortcut.** Clicking the sigil in the system
  view descends into that world; it must never unseal a track on its own. The
  unseal is only ever earned by finding the beacon.
- **Composition, not noise.** Each node lays out one backdrop form, two mid
  forms, then detail, and sorts largest-first so detail lands on top. Each
  depth also leans on two palette entries. Without both, every zoom level looks
  identical and the canvas reads as camouflage rather than artwork — that was
  the first version and it was not worth keeping.
- **Generation is deterministic and raylib-free.** `canvas.hpp` uses its own
  small PRNG because `std::uniform_*_distribution` is not specified to give the
  same sequence across implementations, and a world must be identical every run
  and on every machine. Keeping raylib out is what lets it be unit-tested with
  no window.
- **The view transform is `double`.** Deep zoom runs out of `float` precision
  within a few levels.
- **Cull before drawing.** Nodes off screen or under ~11 px are skipped, and
  motifs under ~1.15 px are skipped. Motifs fade in as they become legible and
  out as you pass through them, so zooming never pops.
- **Difficulty is data.** `beacon.depth` in `layers.conf` sets how far in the
  beacon hides — each level is 3x further — and hot-reloads.
- **Read the mixer live, not the frame's `mask` snapshot,** when deciding
  whether a track can be descended into. A track woken earlier in the same
  frame is already on; using the stale snapshot made `7` then `Z` fail.

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

`--state` also publishes the sigil's position and hover state and the pointer
location, so a driving script never has to duplicate the layout maths to know
where to click. Extend that habit rather than recomputing geometry in Python.

**Driving the app with xdotool:** hold press and release ~80 ms apart for both
keys (`keydown`/`keyup`) and mouse buttons (`mousedown`/`mouseup`). A default
XTEST tap is ~12 ms and can begin and end between two of the 60 Hz renderer's
input polls, so `xdotool key` and `xdotool click` silently do nothing. Position
with `mousemove --window` to stay client-relative; absolute X/Y carries the
frame offset on reparented windows. Order assertions so an input that proves
the mechanism works comes before one that expects input to be *refused* —
otherwise a broken harness looks like a passing lock.

**Every new feature must remain observable through `--state`.** When adding
layers, puzzles, or player state, extend that JSON in the same pass — an agent
that cannot read the result cannot verify its own work. Keep `--check-assets`
honest about anything newly required at load.

## Current state (v0.6.0)

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
4. **Worlds ignore their track's rhythm.** Every canvas is generated the same
   way regardless of what its stem sounds like, and nothing in a world moves.
   The obvious next step is making a world breathe with its music — Soft Kick
   pulsing motifs on the beat, Starlight Echoes leaving after-images at its
   0.625 s delay — which would tie the artwork back to the audio clock.
5. **One beacon per world, and it never changes.** Finding it is a single
   puzzle; a world has nothing more to offer afterwards.
6. **Motifs are six primitives.** They compose well in bulk but there is no
   representational content — nothing to recognise, only shapes and colour.
5. **No puzzle sidecar.** `project.xml` cannot carry goal state, mix budget, or
   layer geometry. That needs a sidecar keyed by track id, beside the project,
   leaving the Bitwig file untouched and re-exportable.

The WAV loader accepts only 48 kHz / 24-bit stereo PCM. Fine for the fixture;
revisit when arbitrary projects load.

## Conventions

- C++20, raylib 5.5 static, built with `-Wall -Wextra -Wpedantic`. Keep it warning-clean.
- Match the existing dense style in `src/`; don't reformat wholesale.
- Cover mixer behaviour in `tests/mixer_test.cpp`, config parsing in
  `tests/config_test.cpp`, unlock rules in `tests/progress_test.cpp`, and world
  generation in `tests/canvas_test.cpp`; all four run without an audio device
  or a display.
- The vendored raylib headers are included with `-isystem` so their warnings
  stay out of our build. Keep our own code warning-clean under
  `-Wall -Wextra -Wpedantic`.
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
