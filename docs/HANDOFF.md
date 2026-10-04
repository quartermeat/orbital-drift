# Session handoff — September 20, 2026

## Update — October 4: empty sound table and v0.25.0

The table now starts with zero grains. Holding left click on the tray places a
grain immediately and another every 20 ms, up to the grid capacity; release
stops placement. Clicks on the source picker and grain slider are not treated
as placement. `C`, a resize, and a grain-size change clear the tray. State
reports `bed=0`, and grain count and mass reflect only user-placed sand.

The earlier uncommitted source-picker improvements were retained: the picker
shows Auto desktop audio and physical microphones, hiding output monitors and
virtual loopbacks. The sound-table live check now places grains, verifies their
motion and conservation, and handles window-manager resizing during a tone.
Full `make ci` passed in 114 seconds, including all six live checks; recognition
met its 9-of-12 floor. The user authorized a versioned commit, tag, and push for
v0.25.0. The unrelated untracked `src/listening_garden.hpp` remains outside
that commit.

## Update — October 4: individually stateful sand

The sound table now stores a persistent position, velocity, and local vibration
for every visible grain. The default 1280×900 window at 4 px grain size has
72,000 independent grains. A force grid only supplies the standing-wave slope;
an occupancy grid supplies crowding pressure. Neither grid stores sand or moves
counts between cells. The CPU updates every grain on every active frame and a
GPU point draw shows the grains directly. Silence and pause preserve positions.

`listening.grain_count`, `moving_grains`, and `grain_updates` expose the model.
At the default size, the first live run was around 52 FPS on the RTX 3090 Ti;
the finest grain setting creates many more grains. Efficiency is a separate next
step. The older 2×2 block shader and test described below are superseded but
their uncommitted source files were left untouched.

## Update — September 21: listening island prototype

The user asked for a playful fiddling world aware of whatever music is playing,
and accepted a first island with three movable musical creatures. Local changes
now implement `make listen` / `./build/orbital-drift --dev --listen`: Thump reacts
to low pulses, Willow to the middle band, Glimmer to bright sounds. Gathering
them while music plays grows flowers; Backspace resets, R reconnects, Esc exits.

Capture uses parec output monitors only; no microphone, audio recording,
playback modification or campaign-save changes. Automatic routing follows the
first playback stream's sink (Spotify currently uses `easyeffects_sink.monitor`,
while the default physical output monitor was silent). Route queries run off
the drawing thread. An explicit `--monitor NAME.monitor` overrides selection.
This is frequency/transient awareness, not stem separation or song recognition.

The final full `make ci` passed in 104.9 seconds, including new DSP/placement
tests and an inaudible temporary-output test of capture, silence, dragging and
reset. Real Spotify capture was separately verified. New state fields are
documented and the generated state fixture is updated. README covers launch and
requirements. New files are `src/listening*.hpp`, `src/desktop_monitor.hpp`,
`tests/listening_test.cpp` and `tools/od/listening.go`; changes remain uncommitted.
Public release v0.22.0 and the public website were not changed by this task.
Preserve all prior local memory changes. Prefer the listening island when
continuing this music-companion task; ordinary `make run` still opens the campaign.

## Update — September 23: the Chladni plate

The user watched a short on how sand reacts to sound and asked for that, keeping
the existing ball ("there may be interesting possibilities there other than what
interests me at the moment"). The sand table now has two mechanisms sharing one
tray, swapped with `P`: the magnetic ball that rakes, and a Chladni plate that
shakes. Pitch picks the plate's standing wave and the sand walks off the shaking
ground onto the still lines. `make plate` or `--chladni` starts on it.

New: `src/chladni.hpp` (plate model and the transport rule, testable without a
GPU), `assets/chladni-update.fs` (the same rule on the real field),
`assets/sand-reduce.fs` (counts the sand). Changed: a pitch bank in
`src/listening_audio.hpp`, float fields and both surfaces in `src/listening.hpp`,
`--chladni` in `src/main.cpp`, an optional ball and a bed in `assets/sand-render.fs`.

Four things cost real time and are now written down in AGENTS.md: the field has
to be floating point (eight bits quantises every exchange to nothing), the rule
has to scale with the grid (tuned at 96 square it does nothing at 1536), a
settled figure is far deeper than its bed so nothing may clip it from above, and
the surface toggle cannot be `TAB` because Alt-Tab leaks a Tab press into the
focused window — which silently swapped the surface mid-run twice.

The stale half of the previous session was finished at the same time: the Go
`ListeningState`, the state document, the fixture and `tools/od/listening.go`
still described the three-creature garden that the sand table had replaced, so
`make ci` could not have passed. The live check now drives both mechanisms on a
private inaudible sink and asserts the plate conserves its sand.

Verified: full `make ci` green, and real Spotify capture through
`easyeffects_sink.monitor` on both surfaces. Captures in `artifacts/`:
`chladni-tone.png` (a steady 300 Hz tone) and `chladni-music.png` (live music,
softer and moving). Superseded by the grain work below and released as v0.23.0.

## Update — September 23, later: grains, one to the pixel

The user asked for the sand to be granular to the pixel, framing the tray as a
sandbox where matter is placed on a map and left to interact -- sand being the
only matter in it so far. The plate's continuous depth field is gone. The tray
is now a grid of cells, each holding a whole number of grains, moved in 2x2
Margolus blocks so conservation is exact and integer rather than merely close.
A grain slider (bottom left, drag it) sets how big a cell is, from a grain a
pixel up to about three pixels, by rebuilding the tray.

Coarse grain looks markedly better than fine: the sand has less ground to cross,
so the nodal lines come out sharp and the antinodes clear completely. Fine grain
is dustier and slower. Worth knowing before tuning anything.

Two traps, both now in AGENTS.md: a block automaton needs an integer hash (the
usual `fract(sin(dot(...)))` correlates along diagonals and prints hatching
straight onto the tray), and one Margolus sweep moves far less than the old
continuous exchange, so a frame is worth several sweeps.

Also fixed a real annoyance of my own making: scratch test runs left `pacat`
playing a tone into a null sink, and unloading that sink moves the stream to the
real output. Kill the tone before unloading the module.

`make ci` green, including a live check that drags the grain slider and asserts
the rebuilt tray comes back level. Captures: `artifacts/grain-coarse.png` (the
clean figure) and `grain-tone.png` (a grain a pixel). The tray opens on the
coarse end because a grain a pixel reads as dust. Released as v0.23.0.

## User intent and preferences

- Orbital Drift is the current focus. The user repeatedly loved playing it and
  said "the bones are getting there." Preserve the direction; no new gameplay
  feature was requested at shutdown.
- Always launch interactive sessions with `--dev`. `make run` builds and does
  this automatically. Direct launch: `./build/orbital-drift --dev` from the repo.
- Dev mode shows skit outlines and enables `G` to jump to the target inside a
  world. It is the same executable with a runtime flag, not a separate binary.
- Do not auto-launch on greetings. Check whether the game is already running
  before starting another instance. Live checks must remain serial.

## Published state

- Game repo: https://github.com/quartermeat/orbital-drift (public, `main`).
- Game version: **0.22.0**, commit `e56f6a4`, annotated tag `v0.22.0`.
- Previous dev-launch preference: commit `9579544`, annotated tag `v0.21.1`.
  Both commits and tags were pushed during the authorized publication task.
- Public website: **https://quartermeat.github.io/orbital-drift/**.
- Public release: https://github.com/quartermeat/orbital-drift/releases/tag/v0.22.0.
- Website source: `/home/quartermeat/work/quartermeat.github.io/orbital-drift/`.
  Existing Pages repository, root of `master`; no Sites/Cloudflare deployment.
- Website version **0.3.0**, commit `37e3c7d`, annotated tag `v0.3.0`, all pushed.
- Page is static HTML/CSS with an actual dev-mode screenshot (`world.png`),
  download links, requirements, build/run instructions and controls. It has no
  tracking, external fonts, framework dependencies or client-side JavaScript.
- Existing personal homepage, Mood Player and Crewmate pages were preserved.

## Downloads and reproducibility

The release contains:

- `orbital-drift-0.22.0-linux-x86_64.tar.gz` (67,436,040 bytes).
- `orbital-drift-0.22.0-source.tar.gz` (108,679,841 bytes).
- `SHA256SUMS.txt`.

Both bundles contain all seven music stems, the font, campaign data, shaders,
and third-party notices. The complete source bundle also includes the pinned
raylib 5.5 source archive. GitHub's automatic source ZIP/tar.gz and a Git clone
do not include the music or font: direct visitors to the complete source bundle.

`scripts/setup.py` now verifies bundled audio against the committed manifest
without requiring `~/Music/Orbital Drift/audio`. Original local stems remain a
fallback for missing assets. Corrupt existing assets are rejected and preserved.

Packaging: `go run tools/release/main.go` from a clean, committed tree after
building the version and validating it. Writes versioned archives and checksums
under `build/releases/vVERSION/`; refuses to overwrite existing archives. Checks
binary version and stem hashes, uses an explicit file list, and strips personal
archive ownership metadata. Do not commit stems, build products or saves.

Linux binary was tested on Debian 13 x86-64 with NVIDIA RTX 3090 Ti. It needs
glibc 2.38+, GLIBCXX_3.4.32+, X11/XWayland, hardware OpenGL 3.3 and desktop audio.
Do not claim Windows, macOS, ARM or broad distro compatibility. Older Linux
runtimes should build the source bundle. Default documented interactive launch
is `./build/orbital-drift --dev`; `--resume` explicitly opts into old progress.

## Verification completed

- Full `make ci` passed in about 94 seconds, including live tests. Recognition
  passed its configured floor: 9 of 12 objects recognized by `llava:7b`; three
  misses remain, so do not report perfect recognition.
- Both archive checksums and file lists verified. Source extracted outside the
  checkout, setup run with home redirected by a test mock to an empty directory
  and network downloads forbidden; then built successfully from scratch.
- Rebuilt source passed asset validation; corruption rejection was checked.
- Extracted binary ran from `/tmp` in dev mode with GPU and audio initialized
  and exited successfully. Paths resolve relative to the binary as intended.
- Page anchors, image references and download filenames checked. Desktop
  (1440px) and mobile (390px) rendered and inspected using isolated headless
  Firefox profiles. CUA was unavailable (`CUA_REPL_ENABLED_SURFACES` missing).
- Public page and image returned HTTP 200 and matched the local files byte for
  byte. Both binary/source download URLs returned HTTP 200 without sign-in.
- Pages workflow `35555822156` succeeded for website commit `37e3c7d`.

## Publishing lesson and next session

The page did not auto-deploy after the push. It was fixed with:

```sh
gh api --method POST repos/quartermeat/quartermeat.github.io/pages/builds
```

Check the build is for the intended commit, wait for deployment success, then
verify the public URL. The user tried the planned link before deployment and
got a 404; do not present an address as ready before HTTP verification.

Both repositories were clean after publication. This shutdown handoff and its
AGENTS.md pointers are intentionally local, uncommitted memory updates; no new
commit or push was requested at shutdown. Preserve them in subsequent work and
apply normal version/tag rules if committing later. No remaining publication
work is required. The temporary preview server on port 8765 was stopped.

## Update — October 3: uncommitted capture fix, and the ball is going

Written by Claude Code for whoever picks this up next (Codex is removing the
ball).

**Uncommitted work in the tree, keep it.** Late on October 2 a fix to the sand
table's audio capture was made and not committed: `src/desktop_monitor.hpp`,
`src/listening.hpp`, `tests/listening_test.cpp`, `README.md`,
`docs/interfaces/state.md`, `AGENTS.md`. Full `make ci` passed on it on
October 3 (120s, recognition 9 of 12). Build on it and commit it; do not reset
or check those files out. What it does:

- `--monitor auto` now captures `@DEFAULT_MONITOR@`, the desktop's own output,
  by default. Only after five seconds of silence there does it hunt for a
  player routed through its own sink.
- The hunt reads the long `pactl list sink-inputs` form and skips corked and
  muted streams, so a paused player no longer wins the route.
- `parec` captures two channels, folded to mono in-process. Asking for mono
  made PulseAudio average all eight channels of the 7.1 output, dividing the
  music by the silent surrounds, which made the monitor look dead.

Checked live on October 3: windowed `--listen` connected to
`@DEFAULT_MONITOR@` with real signal (rms ~0.01–0.08) from the user's music.

`src/listening_garden.hpp` is an untracked leftover from September 21, the
three-creature garden the sand table replaced. Nothing includes it; it can be
deleted.

**Direction from the user.** Drop the magnetic ball; work with the sound
(Chladni) table only. The user likes the one-grain-per-pixel granularity
because it reminds them of their cellular automaton, Life's Sandbox
(`~/work/cell_auto_port`, Go/Ebitengine, Life/Zombie/Sick/Dead/Wall cells).
The tray-as-grid-of-matter framing is the way forward: more kinds of matter
with their own interaction rules, moved by sound.

A sand table window may be open on the desktop (`--dev --listen --windowed`);
`make ci`'s live checks will close it.

## Update — October 3: AGENTS.md now requires these notes

Claude Code, at the user's request, added two rules near the top of
`AGENTS.md` (uncommitted, alongside the capture fix above):

- Re-read this file before every response; Claude Code and Codex both work in
  this tree and leave each other notes here.
- Any work in the repo requires a dated `## Update` section here: what you are
  about to change before starting, then what changed, what was verified, what is
  uncommitted, and what must not be undone when you stop.

No code changed. Not verified by `make ci` (documentation only).

## Update — October 3: Codex removes the rolling ball

The user's request is to abandon the ball altogether and keep only the sound
table. Changes are in `src/listening.hpp`, `src/main.cpp`, the sand renderer,
`Makefile`, listening tests, Go state/check code, the generated state fixture,
README and state documentation. The ball motion header and carving shader are
removed. Both `--listen` and `--chladni` now open the plate; `P` does nothing.
Grain size, tuning, pause, clear, and the Chladni transport remain.

The new shared-agent note rule arrived while these edits were underway; this
entry records the work as soon as that rule was read. The prior capture fixes
and Claude Code's notes are preserved, as is the unrelated untracked garden
header. `make interfaces` and full `make ci` passed (123.4 seconds), including
live checks of plate startup through `--listen`, the retired P shortcut leaving
the figure intact, pitch response, conserved sand, silence, grain size and
clear. Recognition passed its existing 9-of-12 floor. `git diff --check` passed.
All changes remain uncommitted; no version bump, commit, tag or push was made.
Do not restore the ball or its surface toggle.

## Update — October 3: Codex expands the sound table to the window

Requested: remove the circular boundary and cover the entire window with the
vibrating sand surface. Plan: update `src/listening.hpp`, `src/chladni.hpp`,
the sand/plate shaders, listening tests and state documentation for a rectangular
field, including corners and resize behavior. Preserve the capture fixes and
ball removal.

Implemented: sand covers every window pixel, with a rectangular grid and no
circular mask/rim/shadow in rendering, transport, or measurement. Grain size is
1–4 screen pixels; resizing refills the grid at the selected size. Both CPU and
GPU use paired cosine modes in normalized window coordinates, selected by pitch,
tuning and aspect-dependent relative frequency. Modes do not rotate. State now
reports `field_width`, `field_height`, `mode_n`, `mode_m`, `mode_sign`, and
`mode_frequency`; the generated fixture and interface documentation match.
Controls have outlined light text to remain readable over sand and bare surface.

Unit tests cover rectangular nodes, aspect-dependent modes, exact conservation
on a 97x63 grid, and transport in all four corners. The live listening check
passed including screenshot corner coverage, pitch response, conservation,
silence, grain size, and a 901x701 resize. Inspected the rectangular figure in
`artifacts/sound-table.png`. Full CI initially hit campaign startup segfaults;
a direct campaign run passed. The next full run passed listening but failed
the campaign revisit claim check. A final full run with the caption contrast
fix is underway. All changes remain uncommitted; preserve earlier capture
fixes, ball removal, and the unrelated untracked garden header.

## Update — October 3: Claude Code, for Codex: the window needs rectangular-plate patterns

The user chose this explicitly. Removing the ring is not enough; the figures
themselves must be those of a rectangular plate, not the circular ones.
`src/chladni.hpp` currently builds circular-membrane modes (`besselJ` in r,
`cos(lobes*angle)`), and extending those into the corners would only give
rings stretched across a rectangle. Replace the mode shape for the window field:

- Use normalized coordinates over the window, u = x/W, v = y/H in [0,1].
- Use the classic free-edge Chladni approximation:
  `cos(n*pi*u)*cos(m*pi*v) ± cos(m*pi*u)*cos(n*pi*v)`. The ± pair gives the
  crossed and diagonal figures from the square-plate photographs; on a
  non-square window the shapes stretch with the aspect ratio, as a real
  rectangular plate's do.
- Pitch should select (n,m) by plate frequency, which goes as
  (n/W)^2 + (m/H)^2. Then the window's proportions decide which figures exist,
  and resizing changes them.
- Keep what AGENTS.md already says about the plate: a standing wave stands
  still (no rotation; `spin` has no meaning here), and sand settles on the
  nodal lines. Corners and edges are antinodes for free edges; sand collects
  away from them.

This is a direction note only; no code changed by Claude Code.

## Update — October 3: keep sound-table verification focused

The user objected directly to time spent on orbital-game tests while requesting
sound-table changes. Updated AGENTS.md to use focused build, listening unit,
interface and live-listening checks for sound-table-only work. Do not continue
investigating campaign revisit failures in this task. Final focused sound-table
check is running; its private output sink needed desktop audio access outside
the sandbox. The P regression test now waits for the GPU gauge's last update
before comparing the held figure, avoiding a stale measurement race. The focused
live check passed once during full CI, then later runs failed at low-tone
capture or steady-tone timing although the resulting live state and screenshot
showed the plate hearing and sorting the test tone. The harness now waits for
measured bands and allows more time for the steady-tone state to arrive. Do not
claim the final live rerun passed without running it; the table's unit, shader
build, interface, screenshot corner and earlier live checks passed. No changes
were committed or pushed.
