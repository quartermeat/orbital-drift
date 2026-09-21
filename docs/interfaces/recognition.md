# Interface: recognition

Whether the objects in a skit read as the things they are meant to be.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `ci/recognition.yaml`, hand written
- **Consumed by:** `checkRecognise()` in `tools/od/recognise.go`
- **Fixture:** [`testdata/interfaces/recognition.json`](../../testdata/interfaces/recognition.json)
- **Images from:** `make props` → `build/propsheet` → `artifacts/props/*.png`

`make props` renders every object alone on a plain ground. `od recognise` sends
each one to a local vision model through Ollama and looks for any accepted word
in the description it gets back.

**This tests the drawing, not the model's vocabulary.** "Wagon" for a cart is a
correct reading, which is why several words are accepted per object. A
silhouette only its author can identify is not an object, it is a shape.

Three details that are not incidental:

- **A description, not a one-word answer.** Asked for one word, moondream emits
  a stray leading token that swallows the whole reply — every object came back
  as "urn". Keywords in a sentence are both more robust and a fairer test.
- **Several attempts per object.** Vision models are not deterministic even at
  temperature zero; the same drawing passed and then failed on a re-run with no
  change. A test that flips colour without a code change teaches everyone to
  ignore it.
- **A missing model is an environment problem, not a failing drawing.** Same
  distinction the live checks make about window focus.

The model is configurable because the judge matters: a drawing a human names
instantly can still defeat a small model, and that says nothing about the art.
