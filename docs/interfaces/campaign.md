# Interface: campaign

One Bitwig project as a scenario. Read once at boot; never reloaded.

> **This document is checked.** `make ci` regenerates the fixture below from
> the live code and fails if it differs from what is committed. If this file
> and the code disagree, the build stops.

- **Produced by:** `campaigns/*.conf`, hand written
- **Consumed by:** `loadCampaign()` in `src/campaign.hpp`
- **Fixture:** [`testdata/interfaces/campaign.json`](../../testdata/interfaces/campaign.json)
- **Selected with:** `--campaign file.conf`

Syntax is `key = value`, one per line. A `#` comment must be on its own line,
because a trailing one cannot be told apart from a `#rrggbb` colour.

Tracks are numbered from 1 and must be contiguous: numbering stops at the
first gap rather than skipping it, because silently dropping a stem from the
mix is worse than refusing to load it.

`seed` is optional and defaults to a hash of the title. It exists because
worlds seeded from the track index alone made every campaign generate the same
islands in different colours.
