# Session handoff — September 20, 2026

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
