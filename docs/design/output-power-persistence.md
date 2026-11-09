# Output power persistence

`dpms-off` on an output is undone when the monitor's hotplug activity recreates
the output object: the replacement starts powered-on and immediately modesets.
This note records the problem, the preferred design (session power memory plus
churn-free recreation), and the alternates with their trade-offs. It is the
implementation brief for the `power-memory` workspace — a feature branch cut
from upstream `main` (freshened 2026-11-09) for reintegration through the
`working` integration workspace.

## Problem and evidence

On the workhorse rig, idle power-off works at the commit level and is then
defeated by monitor firmware:

- `msg dpms-off` disables both connectors. DP-2 (Gigabyte M27Q) stays dark for
  hours. DP-3 (Acer XV275K P3, 3840x2160) drops HPD ~31–33 s after signal loss,
  re-asserts it ~1 s later, and the recreated output modesets back on. The
  bounce is deterministic and input-independent (verified 2026-11-08 with a
  targeted `dpms-off:DP-3` and no user input: off at t0, disconnect at t0+33 s,
  reconnect and modeset at t0+34 s).
- Each bounce displaces and then restores every window from the output
  (`restored 50+ displaced windows` per cycle in the journal).
- After a bounce, `wakeDpmsOutputs` sees one lit output and never wakes the
  others — the session locks half-awake (one lit, one dark) until an explicit
  `dpms-on`.
- DDC VCP `0xD6` is not an alternative on this panel: values 2/4 are ignored;
  value 5 powers off but drops HPD immediately and kills DDC, leaving the
  physical power button as the only wake.

The monitor is the trigger, not the cause: any HPD edge (KVM switch, dock,
cable reseat, deep-sleep probe) recreates the output, and umbriel currently
has no memory of the power request that preceded it. The same failure would
occur with any probing monitor.

Evidence and source-level path analysis live in the afk-dim repository:
`~/src/afk-dim/doc/design-plans/2026-11-08-umbriel-power-hotplug.glm53m.md`
(the full study this note compresses),
`~/src/afk-dim/doc/design-plans/2026-10-29-umbriel-protocol-survey.md`, and
`~/src/afk-dim/doc/design-plans/2026-11-08-noctalia-power-survey.glm53m.md`.
Line references below follow that study's series-lineage citations.

## Current behavior

- Umbriel's DPMS is an output-disable commit: `Output::setPowered` toggles
  `m_dpmsOff`, and `applyConfiguredState` commits
  `enabled = desktopEnabled() && !m_dpmsOff` (series `src/output/output.cpp`
  ~439–445; `setPowered` ~1013).
- `m_dpmsOff` is a plain member of the `Output` object (`output.h` ~232). There
  is no server-side power state: when wlroots destroys the output on
  disconnect, the off-request dies with the object.
- On reconnect, `Server::onNewOutput` → `addOutput` (~1748) constructs a fresh
  `Output`; the constructor calls `applyConfiguredState` (~127) with
  `m_dpmsOff == false`, so the initial commit stages enabled-with-mode — the
  relight. `addOutput` also calls `scheduleDisplacedViewRestore`, producing the
  per-bounce window churn (`restored N displaced windows`, ~2899).
- `wakeDpmsOutputs` (~1617–1629) wakes outputs only when every desktop-enabled
  output is dpms-off (the all-dark rule). One relit output defeats it forever.
- The series lineage already carries reconnect-persistent per-output state for
  *logical* enablement: `OutputEnableOverrides`
  (`src/output/enable_overrides.h` 14–29; session-scoped, keyed by connector
  plus display descriptor, with ambiguity handling; consulted by
  `initialOutputEnabled`, ~3182, from the constructor; cleared on config
  reload). Power has no equivalent.

## Goals

- A `dpms-off` request survives output-object recreation: a remembered-off
  output that reappears stays dark, with no modeset and no flash.
- Input wake heals all outputs together, including ones that stayed dark.
- HPD pulses of a dark monitor cause no visible desktop churn (no repeated
  window displacement/restore).
- Identity is stable across recreation (connector + descriptor), and the state
  is fail-lit: fresh sessions and explicit config changes start powered on.
- Upstreamable: the mechanism cites its own precedent (`OutputEnableOverrides`,
  upstream `e6e68f2e`) rather than workhorse-specific behavior.

## Non-goals

- Idle policy of any kind — clients (afk-dim, noctalia) own when to ask.
- Gamma dimming and logical enable/disable semantics (unchanged).
- Monitor firmware workarounds or per-model special cases.
- wlroots changes (0.20's power manager is a stateless pass-through; wlroots is
  pinned and dynamically linked).

## Preferred design

Three parts; parts 1 and 2 are the patch, part 3 is an integration property
the lineage already provides.

### 1. Power memory

An `OutputPowerOverrides` store mirroring `OutputEnableOverrides`:
session-scoped, keyed by connector plus display descriptor, reusing the
ambiguity rules (or extracting a shared keyed-map helper).

- **Write point:** inside `Output::setPowered`, after a successful commit.
  This is the single choke point every lever funnels through — `actionDpms`,
  `onOutputPowerSetMode`, and `wakeDpmsOutputs`. `setPowered(false)` remembers
  off; `setPowered(true)` clears. `setPowered` early-outs on logically-disabled
  outputs, so the store must be keyed independently of that path.
- **Read point:** the `Output` constructor, before the initial
  `applyConfiguredState` call — seed `m_dpmsOff` from the store so the very
  first commit is `enabled=false`. wlroots logs only its "Turning off"; no
  modeset, no flash. This is exactly where the series base already consults
  `initialOutputEnabled`.
- **Clearing:** automatic through the write point (wake, `dpms-on`,
  `set_mode(on)`), plus two fail-lit paths: config reload (a deliberate
  configuration change is user intent) and session end (the store is
  in-memory; a fresh session starts lit).
- **Precedence:** power memory wins over `outputCanAutoEnable` — an output
  with usable modes does not auto-enable past a remembered off.

### 2. Churn-free recreation

While an output is remembered-off, its reappearance must not run the
desktop-facing half of the monitor-arrives path:

- Gate displaced-view restoration on the output actually powering on, not
  merely appearing. Windows stay wherever the disconnect rehomed them until
  `dpms-on`/wake; one displacement per idle period instead of a ping-pong per
  HPD pulse.
- The workspace group may be recreated with the output (it dies with the
  object today), but nothing restores into it while it is remembered-off.
  Whether the group can instead be held resident is an implementation choice;
  the constraint is no repeated per-pulse window migration.

### 3. Accurate client events (rides the power series branch)

Upstream `main` does not host `zwlr_output_power_manager_v1`; the local `pwr`
feature branch (integrated in `working`) does. With parts 1 and 2 landed and
the series reintegrated alongside, bound clients see `mode=off` on the
recreate-disabled commit and `mode=on` at wake — an accurate event trail
instead of silent drift. No new work in this branch: it is a test obligation
(below) and the reason clients can later drop polling-style stopgaps
(afk-dim's `umbriel-msg` provider → its built-in `wlr-power`).

Estimated size: parts 1+2 together roughly 60–150 lines plus a harness check.

## Alternates

- **Minimal memory (part 1 only).** Smallest patch; correct relight fix.
  Prefer only if HPD pulses prove rare or self-limiting — on the observed
  ~30 s cadence, every pulse displaces and restores every window on the
  output, all night.
- **Cycle-count damping.** After K recreate cycles while remembered-off, stop
  the desktop-side restore. A fallback shape if gating restore-on-power-on
  proves invasive; wall-clock debouncing is rejected outright (bounce latency
  is monitor firmware — 31 s here, other panels differ — and a genuine replug
  inside the window would be misclassified).
- **Wake-rule refinement.** Any input clears power memory (fail-lit on
  activity). Deferred: the all-dark rule is intentional — partial-off is a
  presentation mode — and part 1 self-heals the half-awake lock. Ship later as
  a config flag if the idle-daemon UX wants it.
- **Client-side re-assert (no umbriel change).** afk-dim watches `wl_output`
  churn (or `mode` events once the series is deployed) and re-sends
  `dpms-off:DP-3`, capped per idle epoch. Deploys without a session restart;
  keeps fighting the compositor, and each relight still churns windows. The
  fallback if no rebuild window exists.
- **Rejected:** wlroots-native persistence (nothing offered in 0.20; pinned
  and dynamic); DDC `0xD6` on this panel (live-tested 2026-11-08: 2/4 ignored;
  5 is write-only off that drops HPD immediately and kills DDC — no remote
  wake, physical button only).

## Open questions

1. Does the XV275K pulse indefinitely while kept dark (HPD drop every ~31 s
   forever) or settle connected-dark like the M27Q? This decides whether part
   2 is necessity or polish. One instrumented night after landing answers it;
   success signature: monitor stays dark, pulses at most log connector scans,
   zero `restored N displaced windows` lines.
2. Do remembered-off connectors keep advertising heads via
   `zwlr_output_management` (today DPMS-off heads report enabled=1)? Default:
   unchanged.
3. Should any input activity clear power memory (the wake-rule refinement),
   and behind which flag?
4. Ordering between the `setPowered` early-out on logically-disabled outputs
   and the memory write, so logical disable and dpms-off do not tangle.

## Test plan

- Harness: extend `tests/harness/checks/protocol/output_power.sh` and the
  `output_power_client` with a bounce simulation — `dpms-off`, then
  `output-destroy` / `output-create` (harness IPC), asserting no enabled
  commit and `mode=off` held; then `dpms-on` asserting wake. Add a
  displaced-views assertion if the harness can observe window rehoming.
- Manual: a single targeted `dpms-off:DP-3` with journal watch (pre-fix
  baseline captured 2026-11-08: 33 s bounce, deterministic), then the
  instrumented night above.

## Rollout

Parts 1 and 2 are self-contained and upstreamable alone from this feature
branch. Reintegrate through the `working` integration workspace alongside the
`pwr` power-series branch, then carry into the next longcat deploy candidate:
rebuild, restart the session at a chosen moment. Afterward afk-dim switches
its power provider from `umbriel-msg` to `wlr-power` — a config-only change;
the provider is built and sway-verified.

## Provenance

Written 2026-11-09 by model:zai-coding-plan/glm-5.3#max, OpenCode session
`ses_e4ca727dfffeiSFCUWJMOGH9L1`. Derives from the afk-dim design-plans
studies cited above (source verification across the deployed, ARCH, upstream,
and series trees), live tests of 2026-11-08 (DDC `0xD6` probe; controlled
bounce re-test), overnight journals of 2026-11-03/05, and the afk-staged-idle
bead notes.
