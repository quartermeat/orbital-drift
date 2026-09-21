# Interface: skit

A vignette, and the mix that brings it out.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `generateScene()` in `src/scene.hpp`
- **Consumed by:** visibility and the dev overlay in `src/main.cpp`
- **Fixture:** [`testdata/interfaces/skit.json`](../../testdata/interfaces/skit.json)

**Every person belongs to a skit.** A lone wanderer is a `Stroll` of one, so
the rule has no exception.

Visibility is `(playing & wants) == wants && (playing & hides) == 0`. Most
skits want one track and behave like a layer; about a quarter want a pair; a
few want a track **silent**, and those can only be reached by muting something.

A skit belongs to exactly one configuration, so toggling a track adds or
removes whole vignettes rather than half a queue.

The target's own skit always wants exactly its world's track and hides
nothing, or the hunt could need a mix the player has no way to guess.
