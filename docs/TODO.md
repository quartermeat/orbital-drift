# TODO — open work for the next agent

Read this before starting work in Orbital Drift. It lists handed-off work that is not done yet. Remove an item once it lands; add new handoffs at the top.

---

## 0. Agent-token sand moved to its own project (2026-10-11)

The user split the wallpaper visualizer out to keep this repo's slow `make ci` off its path. **token-sand** (`~/work/token-sand`) now owns the agent-token sand, rainbow grains, agent ships and wallpaper mode; it starts from `codex/agent-ships` v0.28.0 (4738410). User decision: **Orbital Drift keeps its audio sand table as a game feature, but without agent tokens.** So do **not** merge `codex/token-sand` (v0.26.0), `claude/rainbow-sand` (v0.27.0) or `codex/agent-ships` (v0.28.0) into `main`; they stay as history. `main`'s next version continues from its own line (v0.25.x). Item 1 below is superseded by token-sand.

## 1. Token ship: agent-token sand flies out of a spaceship (live desktop)

_Handed off 2026-10-10 ~20:20 EDT by a Claude session that was asked to stop. Requested by the user._

> **Superseded 2026-10-11 ~10:00 EDT:** the user widened this to **one ship per active agent session**, each shooting its own session's tokens. Codex job `agent-ships` (20261011T135559-2317078) is building it as v0.28.0 in worktree `~/worktrees/orbital-drift-agent-ships` (branch `codex/agent-ships`, based on `claude/rainbow-sand`), starting from this item's WIP patch (`~/worktrees/token-ship-wip.patch`). The single-ship worktree below can be removed once that lands.

### What the user asked for
> "On my live desktop, I want tokens/grains to get generated from a specific point. That point should be a spaceship flying around the desktop with no interface to sound."

- **Live desktop:** the sound table (`--listen`) runs as `orbital-sand-desktop-token-v6.service` (a transient systemd-run unit, `DISPLAY=:0`). The command is `build/orbital-drift --dev --listen --windowed`, from `~/worktrees/orbital-drift-token-sand` (branch `codex/token-sand`, v0.26.0). The window is transparent over the desktop.
- **Today:** each new agent token (agent-bridge `token-feed.tsv`) becomes one grain at a **random** spot on the tray (`src/listening.hpp`, the "Spread token bursts over frames" loop). Codex grains are cyan and Claude grains are violet.
- **Wanted:** a small spaceship cruises around the tray, and every token grain is emitted from it (its exhaust), so the sand visibly pours out of the ship. **No interface to sound:** audio never steers the ship, and the Chladni plate never pushes it. It is not a grain and not part of the sand physics. The grains it emits then behave like all other sand.

### Partial work, uncommitted
- **Location:** worktree `~/worktrees/orbital-drift-token-ship`, branch `claude/token-ship`, based on `codex/token-sand` (a96b1cd, v0.26.0).
- **`src/token_ship.hpp` (new):**
  - `TokenShip` cruises on a time-only Lissajous path inside 8–92% of the tray in both axes, with its heading taken from the velocity.
  - `update(t, frame, width, height, cellPx, tailPx)` keeps the exhaust point behind the hull.
  - `emitPoint(seed, w, h, origin, gx, gy)` gives a grain position behind the exhaust with a small sideways spread, raises the engine `glow`, and records `lastOrigin` (1 = Codex, 2 = Claude).
- **`src/listening.hpp`:**
  - Includes the header and calls `ship.update(...)` every frame.
  - The token loop now uses `ship.emitPoint(...)` instead of random x/y. Mouse placement is unchanged.
  - `drawTokenShip(...)` draws a silver hull with an outline and a cockpit, plus an engine flame in the emitting agent's colour that flares with emission. It's drawn after the grains and before the UI.
  - The state JSON gains `ship_x`/`ship_y` (tray cells), and the version string is now 0.27.0.
- **`tests/listening_test.cpp`:** a ship test covering determinism, bounds, real travel, and grains that stay inside the tray and come from behind the nose.
- **`Makefile`:** `src/token_ship.hpp` added to the `build/orbital-drift` and `build/listening-test` dependencies. **`VERSION`** is 0.27.0.

### Still to do
0. **Version and base (added 2026-10-10 ~21:00 by another Claude session):** `v0.27.0` is now taken by the rainbow grain palette on branch `claude/rainbow-sand` (built on `codex/token-sand`; see its `docs/HANDOFF.md`). Rebase token-ship onto `claude/rainbow-sand` and ship it as `0.28.0`. The ship's flame colour should follow `grain_colors` (in rainbow mode, use the emitted grain's hue or a neutral flame).
1. **State interface for `ship_x`/`ship_y`:**
   - Add `ShipX`/`ShipY float64` (json `ship_x`/`ship_y`) to the listening state struct in `tools/od/app.go`, near `RecycledGrains`.
   - Add `"listening.ship_x"`/`"listening.ship_y": "float64"` to `testdata/interfaces/state.json`.
   - Document both in `docs/interfaces/state.md`, next to `recycled_grains`.
   - The contracts check in `tools/od` diffs these, so update all three together. Alternatively, drop the two state fields.
2. **Build and test:** `make` and `make test`, then `make ci` (the full gate, including the live listening check, which places grains and feeds a test token feed).
3. **Live check:** the listening check may assume token grains land anywhere. Confirm it still passes when grains come from the ship. Ideally assert that new token grains appear near `ship_x`/`ship_y`.
4. **Look at it:** capture with `--capture`, or F2 in the app. Confirm the ship reads clearly on a transparent background in both glow colours, at the default 4 px grain and at 1 px.
5. **Ship it to the desktop, only with the user's OK:**
   - Commit `v0.27.0: ...` with an annotated tag on `claude/token-ship`. Follow the home AGENTS.md versioning, and check `git log` first.
   - Decide with the user how it relates to `codex/token-sand`. That branch still awaits integration with the main checkout's uncommitted v0.25.1 work; see `docs/HANDOFF.md` "Agent token sand".
   - Rebuild, then restart the desktop unit from the new build. Don't push unless asked.

### Don't touch
- The main checkout's uncommitted changes (v0.25.1 work: `AGENTS.md`, `VERSION`, `src/listening.hpp`, `src/main.cpp`, `tools/od/*.go`, and the untracked `src/listening_garden.hpp`). They are the user's or another session's work.
- The running `codex/token-sand` worktree, until the new build is verified.
