# Interface: progress

How far a campaign has been played.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced and consumed by:** `saveProgress()` / `loadProgress()` in `src/progress.hpp`
- **Fixture:** [`testdata/interfaces/progress.json`](../../testdata/interfaces/progress.json)
- **Path:** `artifacts/progress-<campaign>.json`, one per campaign

Deliberately one integer. The track count and the unlock direction come from
the campaign and are never stored, so a save cannot disagree with the campaign
it belongs to.

One file per campaign: a shared file let a seven-track run resume into a
three-track campaign and clamp straight to complete.

A missing or corrupt save starts a fresh run rather than failing, and an
out-of-range value is clamped. Losing progress must never block launching.
