# Interface: scene

A generated world: terrain, places, people.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `generateScene(track, colour, layerCount, campaignSeed)` in `src/scene.hpp`
- **Consumed by:** the world view in `src/main.cpp`
- **Fixture:** [`testdata/interfaces/scene.json`](../../testdata/interfaces/scene.json)

Deterministic from the track index and the campaign seed, with its own PRNG
because `std::uniform_*_distribution` is not specified to match across
implementations.

Only the terrain is baked into a texture. Everything with a hard edge is drawn
live at the current zoom, or it turns to magnified mush the moment you zoom in.

Coordinates are image pixels in a `SceneWidth` × `SceneHeight` space, so what
the generator says and what gets drawn are the same numbers.
