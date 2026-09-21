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

**Two identities, and both are needed.** `figureId()` is the exact depiction,
pose included — a specific drawing. `castId()` is the *person*: everything
except the pose, so the same character queueing here and sitting by a fire
there shares a `castId` while every `figureId` differs. `figureTag()` prints
the short form, `FIG-XXXX`, shown in the find box under `--dev`.

The cast requirement is expressed in `castId`: every skit contains someone who
also appears in another skit. The target is the exception, and must appear
exactly once — that is the whole hunt.

The skit sets the pose; `rollFigure`'s random pose is overwritten. That is
what makes a queue look like a queue.
