# Orbital Drift — Design Ideas

A puzzle game where the mix is the game state. Each track in a Bitwig project is
bound to a visual layer; toggling the track toggles the layer. You solve puzzles
by choosing which layers are playing — which means you solve them by mixing.

## The thesis

**Bitwig is the level editor.**

A `.dawproject` export already contains everything a level needs: a set of named,
coloured tracks, a tempo, a time signature, and the audio itself. Writing a new
song with seven stems produces a new puzzle with seven layers. The app reads the
project; the music and the level arrive together, by construction.

This is why the Bitwig interfacing matters, and it's worth protecting as the
project's spine. Anything that requires authoring levels outside Bitwig is
working against it.

## Core mechanic

Seven tracks, seven toggles, seven visual layers. State is a 7-bit word — 128
combinations, of which a puzzle accepts some small number.

**Decided: a run begins in silence, tracks unlock right to left, and each
track is a world.** Only track 7 is available; everything else is sealed.
Switching a track on exposes its layer and marks its orbit with a sigil — click
that to **descend** into the track's world: a 2D artwork you pan and zoom into
endlessly. Somewhere in there is the beacon, the one motif that is both a spire
and wearing the track's own colour. Right-click it and the next track unseals.

The canvas is a recursive grid generated from path hashes, so detail keeps
arriving however far you zoom and nothing is ever stored. A 3D globe was tried
first and discarded: the zoom had a floor, and Waldo books are flat for a
reason — a plane lets a scene keep unfolding.

The conjunction is what makes it a search rather than a glance. Spires in other
colours and other shapes in the track colour are everywhere, so neither half of
the description narrows it down — the same trick that makes Waldo's stripes
work in a crowd. Generation refuses the beacon pairing everywhere else, so the
answer is globally unique without anything being stored.

That also means the run *starts* sparse and rhythmic and *ends* with the
harmonic bed, so the world gains atmosphere as it fills in.

What makes it a puzzle rather than a toy:

- **Layers combine.** One layer draws part of a route, another draws the rest;
  neither is sufficient. Two overlapping layers produce a third thing in their
  intersection — an XOR region is cheap in a shader and reads, musically, like
  two rhythms forming a composite pattern.
- **Layers obscure.** Cosmic Dust on = a veil that hides what's behind it. So
  some solutions require turning things **off**, and that's the half that makes
  this interesting. It's also true to mixing, which is mostly about what you
  leave out.
- **Layers move like they sound.** The strongest rule available. A layer's
  animation derives from its stem's rhythm, so once the player has heard a
  track they can predict what its layer does — and eventually solve by ear,
  including for layers currently off screen or hidden.

## Mapping the seven stems

At 72 BPM: beat 0.8333 s, bar 3.3333 s, loop 53.333 s (16 bars).

| Track | Sound | Layer behaviour |
| --- | --- | --- |
| Nebula Pad | Sustained detuned chords, 4 bars each | The ground field. Shifts with the progression — a different world per chord, four of them per loop |
| Starlight Echoes | Sparse FM bells, dotted-eighth stereo delays | Points of light that leave **solid after-images** at 0.625 s offsets. You platform on the echoes |
| Cosmic Dust | Filtered noise swelling across the stereo field | A veil that sweeps left to right, hiding what it crosses |
| Warm Sub | Sine bass on the chord roots | Mass and solidity. With it off, things that looked solid aren't |
| Soft Kick | Downbeats plus syncopated ghost hits | Platforms that pulse in and out on the beat, including the off-beat ghosts |
| Distant Snare | Quiet half-time, beat 3 | A hazard or sweep that comes around every two bars |
| Orbit Hats | Swung eighths, alternating velocity | Fine detail, fast flicker — and the swing means it's *not* on the grid |

Starlight Echoes is the one to prototype first. A delay line you can stand on is
a mechanic by itself, the offset is already in the audio, and it demonstrates the
whole thesis in one screen.

## Mix budget

The natural constraint: you can't have everything on at once.

Making the currency **headroom** rather than an abstract counter keeps it
diegetic — loud layers cost more, quiet ones are cheap. Per-track peaks are
already measured in `audio-check.json`:

| Track | Peak dBFS | Relative cost |
| --- | --- | --- |
| Soft Kick | −10.5 | expensive |
| Nebula Pad | −13.2 | |
| Warm Sub | −14.9 | |
| Distant Snare | −18.4 | |
| Starlight Echoes | −19.2 | |
| Orbit Hats | −24.2 | |
| Cosmic Dust | −29.1 | cheap |

**Honest check:** the full mix with all seven on peaks at −5.6 dBFS, so a literal
"stay under 0 dBFS" rule constrains nothing — everything fits. The budget has to
be an authored number per puzzle, not the physics. But the physics still gives a
principled **cost curve**, and "you have 3 layers" or "you have this much
headroom" is a clean difficulty dial either way.

## Audio architecture

This carries more weight now: the audio clock is the game clock. If the visuals
are driven by the same playhead that mixes the audio, sync is not something to
maintain, it's something that can't break.

Stems are 48 kHz / 24-bit stereo, 2,560,000 frames, 53.333 s — ~20 MB each as
float32, ~143 MB for seven. Decode all of them into RAM at load.

**Do not use seven raylib `Music` streams.** Each carries its own buffer and
position and nothing keeps them sample-locked. Instead: one `AudioStream` with a
custom mix callback, seven in-RAM buffers, one shared playhead.

```
for each frame:
    for each track t:  out += buf[t][playhead] * gain[t]
    playhead = (playhead + 1) % frame_count
```

- Sync is true by construction. There is one playhead.
- Looping is a modulo. No seam, no restart.
- Toggle is `gain[t] -> 0`; the track keeps advancing, so unmuting drops back in
  where the music is, not at the top of the stem. The visual layer must behave
  the same way — reappearing mid-phrase, not restarting.
- Rendering reads `playhead` for beat/bar/section, so a pulsing platform and a
  kick drum are the same number.

**Ramp gains over 5–10 ms.** A hard jump from mid-waveform to zero clicks, and
with seven of them it will sound broken. The stems' reverb and delay tails also
wrap across the loop boundary, so a stopped track would lose the tail belonging
to the next loop's downbeat.

Whether the *visual* toggle should ramp too, or snap while the audio fades, is a
feel question to try both ways.

## Reading the project

`.dawproject` is a zip: `project.xml`, `metadata.xml`, `audio/NN Name.wav`.
`project.xml` is readable XML carrying tempo, time signature, and per track an
`id`, `name`, `color`, initial `Mute`, `Pan`, `Volume`, and a master track.

Track colours come from Bitwig, so the on-screen layers can be **the colours the
user chose while writing the song**. Names label the toggles. Zip reading is the
only new dependency; miniz is already vendored with raylib.

What `project.xml` cannot carry is the puzzle — goal state, budget, layer
geometry. That wants a sidecar (`orbital-drift.json` beside the project) keyed by
track id, so the Bitwig file stays untouched and re-exportable. Worth designing
that schema early, since it's the thing the level editor doesn't give us.

`scripts/setup.py` currently copies the seven stems from `~/Music/Orbital Drift`
and hard-fails unless it finds exactly seven. That's a fixture, not a loader —
and the `.dawproject` already contains those same WAVs. Suggest keeping it for
the raylib and font fetch, and loading audio from the zip at runtime. The hash
manifest stays useful for the fixture.

## First pass — mostly built (v0.1.0)

Done: fullscreen, hardware accelerated, seven stems decoded to RAM, one stream,
one shared playhead, ramped per-track toggles with metering, headless
`--capture`/`--state` for agent verification.

Still open, in order:

1. Read names, colours, tempo, and initial mutes from `project.xml` instead of
   the hardcoded tables in `mixer.hpp` and `main.cpp`; make track count dynamic.
2. Load the `.dawproject` zip directly rather than the copied `assets/audio`.
3. Draw beat/bar/section from the playhead and confirm it holds over many loops
   without sliding.
4. Bind one visual layer to one track — Starlight Echoes and its after-images.

Only after 4 is it worth asking what the first actual puzzle is.

Data hot reload is in: `space.fs` and `layers.conf` re-read on save while the
music keeps playing, so layer tuning is a save-and-look loop rather than a
restart. Code hot reload (a reloadable `.so`) is sketched in `AGENTS.md` and
worth doing once there is real layer code to reload.

See `AGENTS.md` for the invariants an implementing agent must not regress.

## Open questions

- Is there a player avatar moving through this, or is the whole game the toggles?
  Everything above works either way, and it's the biggest fork remaining.
- Do toggles apply instantly or quantize to the next bar? Quantized feels better
  and costs little once the playhead is shared — but it makes timing part of the
  challenge, which edges toward a rhythm game. Keep the first puzzles untimed.
- One project per level, or a session that moves between projects?
- Does a puzzle have one solution or several? Several fits "mixing" better.
- What does failure look like? Possibly nothing — you just keep mixing.
