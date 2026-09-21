# Interface: figure

What a person is, separated from how they are drawn.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `rollFigure()` in `src/person.hpp`, and by skits, which override the pose
- **Consumed by:** `drawFigure()` in `src/figure.hpp`
- **Fixture:** [`testdata/interfaces/figure.json`](../../testdata/interfaces/figure.json)

`person.hpp` holds no raylib, which is what lets scene generation own people
and be unit-tested without a window. `figure.hpp` only draws.

The skit sets the pose; `rollFigure`'s random pose is overwritten. That is
what makes a queue look like a queue.
