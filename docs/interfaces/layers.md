# Interface: layers

Visual tuning, reloaded while the app runs.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `assets/layers.conf`, hand written
- **Consumed by:** `loadLayerConfig()` in `src/hotreload.hpp`
- **Fixture:** [`testdata/interfaces/layers.json`](../../testdata/interfaces/layers.json)

Saved changes apply within about 130 ms and the music does not stop. A bad
line keeps the previous value and reports a note rather than failing.

Track names, roles and colours are **not** here. They describe the Bitwig
project, so they live in the [campaign](campaign.md).
