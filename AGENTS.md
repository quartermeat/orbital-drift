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
make ci       # THE ONE TO RUN: build, unit, assets, campaigns, every live check
make          # build build/orbital-drift
make test     # unit tests only, no window or audio device needed
make figures  # regenerate artifacts/figures.png
make worlds   # save a PNG of every world background
make setup    # fetch raylib 5.5 (checksum-pinned) + stems + font
```

**Run `make ci`, not the individual checks.** It is the higher-level entry
point and it verifies everything against the same build; running one check by
hand proves only that one thing still works. It takes about 70 seconds.
`ci/pipeline.yaml` defines the stages, so adding a check means adding a step
there rather than remembering a new command.

Individual pieces, when you are iterating on one: `tools/od/od check controls |
hot-reload | progression | find`, `tools/od/od campaigns`, `tools/od/od worlds`.

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

## Campaigns

A campaign is one Bitwig project turned into a scenario: `campaigns/*.conf`
holds the title, tempo, key, bar count, which folder the stems live in, the
unlock direction, and every track's name, file, role and colour. A second
project is a second config file, not a second build. `--campaign file.conf`
picks one; the default is `campaigns/orbital-drift.conf`.

`campaigns/three-signals.conf` is a **test fixture, not content**: three
tracks unlocking left to right over a subset of the same stems, kept as living
proof that count and direction really are per campaign. There is still only
one real campaign, because there is only one Bitwig project. A genuine second
one needs its own stems -- which is what `bitwig-session-builder` exists to
produce.

- **Worlds are seeded from the campaign as well as the track.** Seeding from
  the track index alone made every campaign generate the same islands in
  different colours. `Campaign::seed` comes from the title, or from an explicit
  `seed =` in the config.
- **Nothing about a specific project may be hardcoded again.** `MaxTracks` is a
  compile-time ceiling for array sizing only; the real count is
  `Mixer::trackCount` / `Campaign::count()`, and the unlock direction lives in
  `Progress::rightToLeft`. Titles, tempo, key and bar count all come from the
  campaign and are drawn from it.
- **`Mixer::render`'s parameter is `frameCount`, never `count`.** A member
  called `count` shadowed it once and sent the per-track loops off the end of
  the array.
- The campaign is completed when every track is unlocked, which opens the
  finale. `Esc` leaves it for the galaxy; `V` there re-opens it.
- **The last world unlocked is deliberately kept.** Finding its target unlocks
  nothing, but the world stays open and searchable -- it is reserved for easter
  eggs and future campaign work, so do not skip generating it or block entry.

## Worlds

Every track owns a world: one generated background image, panned and zoomed.
`scene.hpp` lays out an island — coast, beach, woodland, farmland, roads and
towns — and `bakeScene` draws it once into a 2560x1440 render texture. After
that the view is only ever panning and zooming an image, with zoom bounded
between fitting the frame and 9x.

**Layers are the whole idea.** Every world holds one crowd per track, and a
crowd is only drawn while its track is playing. Unlock a track and every world
gains people it never had, including ones you already searched; the second
world opens showing two crowds because two tracks are on. Toggling a track in
the galaxy view cascades straight through, because the draw reads
`mixer.enabled` rather than any copied state — keep it that way.

**The hunt is for a person.** One of the world's ~500 people is the target; the
find box renders that exact figure at the size it reaches at full zoom, so what
you are shown is what you are looking for. They carry an invisible hit box
(never smaller than a comfortable click); clicking it unseals the next track,
starts it playing, and returns you to the galaxy view to hear what changed. Generation guarantees nobody else wears the same outfit — shirt,
trousers, pattern, stripe and cap — or the hunt has two right answers and no
fair one. Spire waymarks on the land are scenery only.

`person.hpp` holds people as pure data with no raylib so the scene generator
can own them and stay testable; `figure.hpp` only draws.

Zones that override the background at close zoom are the next layer and do not
exist yet. The background is meant to still read at a distance.

Rules:

- **Only the terrain is baked; everything with a hard edge is drawn live** at
  the current zoom. Baking the buildings, trees and people turned them into
  magnified mush the moment you zoomed in. The terrain is baked per pixel, not
  in cells, or it shows as blocks.
- **Worlds bake lazily and are cached.** Generating the terrain image is not
  free; do it on first visit, not at startup.
- **Level of detail is what buys the frame rate.** Below ~9 px a person is
  drawn as a single mark and below ~6 px a tree is a 7-sided poly with no
  shadow. Without both, a populated world runs at 45 fps instead of 60.
- **Nothing sits in the sea.** Towns, buildings, trees and markers are all
  rejected below `ShoreLevel`, which is what makes a scene read as a place.
- **Generation is deterministic and raylib-free.** `scene.hpp` uses its own
  PRNG, so a world is identical every run and on every machine, and the layout
  is unit-testable without a window.
- `--dev` adds `G`, which jumps the view onto the target at full zoom and
  outlines its hit box in red. It is the only cheat; keep it behind the flag.
- **The hit box extends past the feet.** A person's ground point is where the
  eye says they are, so clicking their feet or shadow has to count; a box that
  stops at the feet misses the most natural click by a pixel.
- `--gallery` opens straight into a world with 1-7 switching between them, and
  `--world N` opens one directly. `scripts/preview_worlds.py` saves a PNG of
  each.
- **Capture with the app's own `--capture`.** ImageMagick `import -window`
  silently returns the same stale frame under a compositor; seven "different"
  worlds came back byte-identical that way.

## People

`figure.hpp` draws a person: flat shapes, soft offset shadow, no outlines, no
face, a half-circle skull cap for headwear, and 24 poses. `make figures` writes
a sheet to `artifacts/figures.png` for judging it by eye.

- **Oblique, never top-down.** Seen from straight above a person is a cap and
  two shoulders with no character at all; the top-down variant was tried and
  discarded. This means the top-down background map and the people want
  different projections, so **zones should be drawn oblique** when they land.
- **Limbs pivot at the joint**, not their middle, or a raised arm just spins in
  place.
- **~30px is the floor** for a figure to read; 40px crowds work well.
- Legs with opposite signs stride, legs with the same sign fold to one side,
  which is the only way a one-segment leg reads as sitting.
- Lying down is deliberately absent. A standing figure turned on its side is
  incoherent however it is pivoted, because the limbs turn with it; sunbathers
  need their own draw path.

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

## Current state (v0.7.0)

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
- **Prefer getting the thing working over building the pipes around it.** The
  user has asked for less "future interface piping": fewer flags, state fields,
  harness scripts and invariants written in advance of a need, and more of the
  actual result on screen. Add observability and tests where they earn their
  keep now, not where they might later. When in doubt, ship the visible change
  and let the scaffolding follow the second time it is needed.
- Cover mixer behaviour in `tests/mixer_test.cpp`, config parsing in
  `tests/config_test.cpp`, unlock rules in `tests/progress_test.cpp`, and world
  layout in `tests/scene_test.cpp`; all four run without an audio device or a
  display.
- The vendored raylib headers are included with `-isystem` so their warnings
  stay out of our build. Keep our own code warning-clean under
  `-Wall -Wextra -Wpedantic`.
- Per the home guide: **Python** for one-off scripts, **Go** for anything
  long-lived. Game code is C++ because raylib is. **Frequent reuse is itself
  the trigger to switch**: a script written as a throwaway that now runs on
  every change has stopped being a throwaway. The check harnesses crossed that
  line and are now Go, in `tools/od`. Python is fine to start a script in;
  port it once it is being run rather than written. `scripts/setup.py` stays
  Python because fetching dependencies really is a one-off.
- **Live checks never run in parallel.** Two windows named "Orbital Drift"
  fight over focus and xdotool sends input to the wrong one. Unit and asset
  stages are parallel because they are headless.
- **Park the pointer before sending keys.** `xdotool --clearmodifiers` can
  restore a held button as a synthesized click, so a pointer left over an orbit
  node turns the next keystroke into a descent. `Launch` parks it.
- **`Process.Signal(nil)` reports every live process as dead.** Use
  `syscall.Signal(0)`; the nil version made a healthy app look like a crash.
- Generated output (`build/`, `artifacts/`, `assets/audio/`, `assets/font.ttf`)
  is gitignored. Don't commit stems.

## Versioning and pushing

Home guide rule applies: every commit advances `MAJOR.MINOR.PATCH` in `VERSION`,
the subject is prefixed `vX.Y.Z: `, and an annotated tag `vX.Y.Z` points at it.

**Run `make ci` before every push.** Not the individual checks — the whole
pipeline, so everything is verified against the same build. It takes about 70
seconds. If it fails, fix it before pushing rather than pushing and mentioning
the failure. The first time it ran it found a segfault that none of the
individual checks covered, which is the entire argument for this rule.

**This is not a git repository yet.** `VERSION` says `0.1.0`. Initialising it is
a user decision — ask before running `git init`, and don't push without explicit
intent.
