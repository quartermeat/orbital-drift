# Interface: pipeline

The stages `make ci` runs.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `ci/pipeline.yaml`, hand written
- **Consumed by:** `runPipeline()` in `tools/od/pipeline.go`
- **Fixture:** [`testdata/interfaces/pipeline.json`](../../testdata/interfaces/pipeline.json)

Stages run in order and stop at the first failure. Steps within a stage run
together when `parallel: true`.

**`parallel` is a correctness setting, not a speed one.** Anything that opens
a window must stay serial: two windows named "Orbital Drift" fight over focus
and xdotool sends input to the wrong one. Headless stages — unit tests, asset
and interface checks — are parallel.

Adding a check means adding a step here, not remembering a new command. That
is the whole point of having one entry point.
