# Interfaces

Every boundary between layers is written down here, and every document points
at a structured fixture in `testdata/interfaces/` that the live code produces.

**The rule:** an interface may not change without its fixture changing, and a
fixture may not change without someone updating its document in the same
commit. `make ci` enforces the first half by regenerating the fixtures and
diffing; the second half is on the author, and the diff is what makes it
obvious.

| Interface | Between | Fixture |
| --- | --- | --- |
| [campaign](campaign.md) | `campaigns/*.conf` → C++ | `testdata/interfaces/campaign.json` |
| [layers](layers.md) | `assets/layers.conf` → C++ (hot reloaded) | `testdata/interfaces/layers.json` |
| [progress](progress.md) | C++ ↔ disk | `testdata/interfaces/progress.json` |
| [figure](figure.md) | `person.hpp` → `figure.hpp` | `testdata/interfaces/figure.json` |
| [scene](scene.md) | `generateScene()` → renderer | `testdata/interfaces/scene.json` |
| [skit](skit.md) | generator → visibility + dev overlay | `testdata/interfaces/skit.json` |
| [state](state.md) | C++ → Go tooling | `testdata/interfaces/state.json` |
| [pipeline](pipeline.md) | `ci/pipeline.yaml` → Go runner | `testdata/interfaces/pipeline.json` |
| [dependencies](dependencies.md) | `ci/dependencies.yaml` → Go runner | `testdata/interfaces/dependencies.json` |
| [recognition](recognition.md) | `ci/recognition.yaml` → Go runner + a vision model | `testdata/interfaces/recognition.json` |

`sizeof` is recorded for every C++ struct on purpose. C++ has no reflection,
so a field added without updating `tools/contracts.cpp` would otherwise pass
unnoticed; a changed `sizeof` makes the diff fail and forces a look.

The two cross-language boundaries — `state` and `pipeline` — matter most,
because nothing but this check couples the C++ and the Go.
