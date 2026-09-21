# Interface: dependencies

Everything third-party that links, and why.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `ci/dependencies.yaml`, hand written
- **Consumed by:** `checkDeps()` in `tools/od/deps.go`
- **Fixture:** [`testdata/interfaces/dependencies.json`](../../testdata/interfaces/dependencies.json)

`od deps` lists what actually links — `go list -deps`, filtered by Go's own
rule that a stdlib import path has no dot in its first element — and fails if
anything is missing from the config, if anything in the config no longer
links, if a count exceeds its limit, or if a native library is not pinned to a
checksum.

**The budget today is one Go module and one native library.**

| | | |
| --- | --- | --- |
| `gopkg.in/yaml.v3` | 11,285 lines | the pipeline is YAML on purpose |
| raylib 5.5 | vendored, checksum-pinned | window, GL, audio device, input |

`gopkg.in/check.v1` appears in `go.sum` as yaml.v3's *test* dependency and is
never linked, which is why it is not listed here.

Adding a dependency means adding it to the config first, with a reason someone
can argue with. That is the entire mechanism: creep is easy to do by accident
and hard to undo later, so it is made visible rather than trusted.
