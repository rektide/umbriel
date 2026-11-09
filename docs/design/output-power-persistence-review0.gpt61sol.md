---
type: Assessment
title: Output power persistence — implementation appraisal
description: Code-grounded review of power memory, hotplug restoration, cold-output discovery, identity, wake boundaries, and the proposed alternatives.
status: draft
author: model:openai/gpt-6.1-sol#high
date: 2026-11-09
resource: /docs/design/output-power-persistence-review0.gpt61sol.md
sources:
  - resource: opencode://ses_e41ff2005ffeU5ZXaZRvP6LDjt
    title: Implementation appraisal, baseline build and harness verification, and installed-wlroots lifecycle probes.
  - resource: /docs/design/output-power-persistence.md
    title: Proposal being appraised.
  - resource: /src/output/output.cpp
    title: Constructor, configured commits, logical enablement adoption, and DPMS transitions.
  - resource: /src/output/enable_overrides.cpp
    title: Existing session identity matching and descriptor migration.
  - resource: /src/server/server_events.cpp
    title: Reload, input wake, hotplug, restoration, and output-management transactions.
  - resource: /src/server/server_outputs.cpp
    title: Live-output lookup and headless creation semantics.
  - resource: /docs/design/configuration-reload.md
    title: Selective reload and unchanged-content contract.
  - resource: /docs/design/workspace-lifecycle.md
    title: Evacuation, saved homes, and restoration contracts.
  - resource: file:///home/rektide/src/umbriel-pwr/tests/harness/clients/output_power_client.cpp
    title: Power-series test client's discovery and control lifecycle.
  - resource: file:///home/rektide/src/afk-dim/src/brightness/wlr_power.rs
    title: Actual consumer hotplug and failed-control handling.
  - resource: https://deb.debian.org/debian/pool/main/w/wlroots/wlroots_0.20.2.orig.tar.bz2
    title: wlroots 0.20.2 source; output initialization, layout advertisement, power controls, and disabled-state validation.
---

# Output power persistence — implementation appraisal

## Judgment

**Keep session power memory; revise the implementation brief before coding it.** The diagnosis is correct, and the existing power/logical-enable separation is a good foundation. However, the proposed second part is largely already enforced, the third part is not automatic on a cold DRM reconnect, and the clearing rules omit state transitions outside `setPowered`.

This is an appraisal of the output-power/hotplug slice, not an audit of the whole compositor. No feature implementation or live compositor changes were made. Code locations below refer to this workspace's reviewed parent, `406a55f3`; the proposal's older series line numbers are not the current locations.

## Findings that change the design

### 1. Cold reconnect is not ordinary DPMS-off

`Output::applyConfiguredState` correctly skips mode/scale/transform staging when physical enablement is false (`src/output/output.cpp:612–656`). Seeding `m_dpmsOff` before its constructor call is therefore the right way to avoid an enabling modeset.

But an output that has never been enabled can have **no current mode and zero dimensions**. In wlroots 0.20.2, `backend/drm/drm.c:1640–1698` initializes geometry from an existing CRTC mode only when one exists. A reconnect after an off/disconnect can have none. Merely enumerating preferred modes does not establish current geometry.

Consequences, verified against the matching wlroots source and an executable probe using the installed library:

- `types/wlr_output_layout.c:125–132` advertises a `wl_output` only when width and height are positive. Adding a cold disabled output to the layout does not make a client-addressable global.
- Power controls require a `wl_output` resource. An old control fails on destruction; there need not be a replacement to bind while the monitor remains dark. The proposed `mode=off` event on recreation is therefore **not guaranteed**. The constructor commit also predates any newly bound control; its initial mode would come from control creation, not a replay of that commit.
- Umbriel's `updateOutputManagerConfig` copies backend geometry but overwrites `head.enabled` with logical enablement (`server_events.cpp:3845–3850`). A cold remembered-off output would consequently advertise an enabled head with no real current mode; wlroots can synthesize a `0×0` custom current mode from that state.
- Staging the preferred mode in the disabled commit is not a fix. wlroots rejects mode changes while the resulting output is disabled (`types/output/output.c:665–683`). The installed-library probe confirms rejection.
- Headless tests hide this distinction: `backend/headless/output.c:132–137` initializes a custom mode before Umbriel receives the output. The same probe produces `1280×720` and a global for headless, versus `0×0` and no global for a cold mode-list output.

**Implementation choice:** either explicitly allow a cold sleeping output to remain undiscoverable through `wl_output` until compositor-side wake, or commission independent logical geometry/advertisement. The latter is materially more than a boolean store. Do not mutate wlroots' current-mode fields or briefly enable the connector to manufacture metadata.

My lean for this bug fix is the narrower choice: keep it dark, accept delayed client discovery, and handle cold heads coherently in output-management publication and internally constructed transactions. One possible concrete policy is to withhold never-configured sleeping heads until wake, while retaining them internally and in IPC. That is a deliberate public-behavior change, not the proposal's “unchanged” default. It needs agreement and coverage; it is not an implemented conclusion here. If maintaining a logically enabled, fully described head and a bindable power control while dark is a hard requirement, choose the larger logical-output design instead.

### 2. Repeated restoration is already gated by physical availability

`Server::outputFromName` returns only physically enabled outputs (`src/server/server_outputs.cpp:147–155`), and `preferredOutput` likewise excludes sleeping outputs (`117–129`). The restoration pass resolves homes, saved active selections, and scratchpad homes through these functions. Refuge allocation checks physical enablement too (`server_events.cpp:3053–3059`).

Once a recreated output actually remains disabled, it is already unavailable as a restoration target. `setPowered(true)` already schedules a restoration pass after a successful wake (`output.cpp:1228–1235`).

Therefore **power memory should also eliminate repeated window return/evacuation on this base**, without adding a separate gate to `addOutput`. Retain the global restore scheduling: another output's arrival or a late client may make unrelated displaced work restorable. A gate on this one caller would not establish the invariant anyway; there are many restore scheduling sites.

The first physical disconnect still evacuates windows, closes output-bound layer surfaces, and destroys the workspace group. Memory does not eliminate that first disruption, protocol churn, or group recreation. The observed “50+ windows restored every pulse” should stop because the dark home never becomes eligible, not because hotplug has been suppressed. If every other output is dark, evacuation can leave windows workspace-less rather than on another output; the existing restoration machinery handles that after wake.

### 3. `setPowered` is the request choke point, not the only reset point

The method's speculative flag update and rollback are sound (`output.cpp:1211–1225`). Recording persistent state only after commit success is right. But two other methods clear the local flag:

- `adoptOutputManagerEnabled` clears DPMS on logical disable and disable-to-enable transitions (`1183–1190`).
- `applyOutputState` clears DPMS when configured logical membership changes (`1135–1161`).

If the persistent store is updated only in `setPowered`, `off → logical disable → logical enable → destroy/recreate` can resurrect an obsolete off choice. Successful logical transitions must invalidate the matching power memory too. Test-only output-management requests and failed transactions must not do so. Geometry-only management transactions on an already sleeping logical output must preserve it.

An idempotent power-on should also be able to remove stale matching memory without requiring another hardware commit. A failed wake must retain both local off state and its remembered choice.

### 4. Wake must invalidate disconnected choices

`wakeDpmsOutputs` loops only over existing objects (`server_events.cpp:2017–2028`). Consider both monitors off, A temporarily disconnected, input wakes B, then A reconnects. With the proposal's literal implementation A still inherits “off”; B is now awake, so subsequent input preserves the partial-off state indefinitely.

This does **not** require the proposed “wake on every input even during partial-off” option. Preserve the all-dark rule, but when its wake branch is taken, expire off choices for absent outputs as part of that wake operation. Existing connected outputs still clear their own records only after successful commits. Input with no output objects should end the dormant all-dark request too. A bare explicit `dpms-on` should have a defined equivalent rule for absent records; its existing “unknown/no enabled outputs” return behavior needs an explicit decision if only remembered outputs exist.

The request distinction is important: deliberately powering off one output while another remains awake must continue to survive ordinary input.

### 5. Blind reuse of enable-override ambiguity rules is unsafe

`OutputEnableOverrides` is compact, owns its strings, matches case-insensitively, and avoids transferring a known descriptor to a different identified monitor on the same connector. Those are useful properties.

Its ambiguity knowledge, however, comes from **remembered entries**, not the connected-monitor inventory (`enable_overrides.cpp:37–51`). A probe of existing code shows that remembering off for A and resolving identical-descriptor B on another connector transfers A's off choice to B if B never received an override. `resolve` mutates the saved connector during that transfer (`80`). A store containing only off requests makes this case especially easy to hit. Erasing “on” entries would also discard evidence used to detect ambiguity.

For the first power-memory patch I prefer **exact connector plus descriptor matching, fail-lit on connector changes**. Same-connector HPD is the actual bug; cross-port persistence is not needed to solve it. This avoids hiding an unrelated identical panel and avoids pretending a missing/duplicate serial is a reliable identity. It is narrower than the proposal's descriptor-migration behavior.

If cross-port persistence is wanted, observe all relevant identities before resolving migrations, reject simultaneous matching peers, and preserve learned ambiguity separately from active off choices. Share that identity mechanism with logical overrides rather than duplicating their current approximation under a new class name. Neither approach should merge logical enablement and power into one boolean.

### 6. Reload and consumer rollout need tighter promises

The existing reload contract is selective: identical content is inert; layout-only or appearance-only changes do not reapply output state. Logical overrides clear only for `effects.outputState` (`server_events.cpp:835–859`; see [configuration reload](configuration-reload.md)). Preserve that boundary for power memory rather than waking displays on any successful reload.

Clearing the server store alone is insufficient: live outputs keep `m_dpmsOff` when their logical membership is unchanged. If an output-policy reload is chosen as a fail-lit boundary, reset the live flag through an explicit commit path too, preserving truthful local state on commit failure. Failed config parsing must change neither live nor dormant power state.

The power-series test client is not hotplug-ready: it creates controls only in its startup loop, ignores registry removal, and addresses the first matching saved name. After recreation it can retain an old same-name entry with a dead control. Extending the test requires dynamic control creation after output naming, global-ID-based removal, and live-entry selection—not just appending destroy/create commands. It was inspected first in `working`, then re-read in `~/src/umbriel-pwr` after `working` changed during this review; the cited client behavior is the same in that feature workspace.

The proposed config-only afk-dim switch is also premature. Its current `WlrPowerSession::connect` explicitly snapshots globals once (`~/src/afk-dim/src/brightness/wlr_power.rs:304–325`); registry dispatch does nothing. `ensure_power` can rebind a control on the **old inert output proxy**, while `with_session` does not discard sessions on `ProtocolFailed` or `OutputAbsent` (`356–415`). It is synchronous and transition-driven, not a continuously dispatched hotplug observer. Consumer lifecycle work and integration testing are needed before claiming hotplug-safe protocol adoption.

## Options after code appraisal

| Option                                                       | Assessment and implementation surface                                                                                                                                                                                                                                                                                                |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Session memory with existing restoration                     | Best destination. Server-owned identity state; constructor seeding before the first commit; successful power writes; logical-transition invalidation; dormant wake invalidation; selective reload handling. Decide cold-output publication explicitly.                                                                               |
| Separate restore-on-power gate                               | Do not add by default. The eligibility checks and wake trigger already exist. Test them across repeated pulses, ordinary/pinned/scratchpad windows, active selection, and all-output loss. Fix any demonstrated missing eligibility check at the consumer, not just at `addOutput`.                                                  |
| Cycle-count damping                                          | No benefit on this base once dark outputs cannot be restoration targets. It adds counters, reset rules, and a knowingly disruptive first K cycles. It also cannot stop destruction of real protocol objects after genuine backend destruction.                                                                                       |
| Wake every off output on any input                           | Mechanically small in `wakeDpmsOutputs`, but changes tested presentation behavior. Keep separate from persistence. A future flag belongs in config declaration/parsing/schema/docs plus DPMS and protocol tests; do not infer it from the current bug.                                                                               |
| Client reassertion                                           | Emergency workaround only. Requires registry lifecycle handling, an epoch-scoped cap, asynchronous dispatch or an explicit polling mechanism, and queued IPC/protocol requests once names are known. Cannot prevent the initial enable, first restoration, or flash. Current afk-dim power code is not already that watcher.         |
| Retain workspace/output identity across physical destruction | Potentially valuable for zero first-disconnect disruption, but not a small substitute for memory. Workspace groups, scene outputs, layer surfaces, lock surfaces, reservations, and backend pointers currently depend on `Output` lifetime. Requires an independent logical-output owner and explicit backend attachment/detachment. |
| wlroots persistence or DDC                                   | No persistence facility in the reviewed power manager. Keep firmware-specific DDC experimentation outside this patch; the panel's reported failed DDC tests were not repeated here.                                                                                                                                                  |

## Landing shape and acceptance gates

I would implement the narrow memory mechanism first, without a new wake policy, timers, cycle counters, resident workspace groups, or a general-purpose state framework. Share matching machinery only where its contract is actually common. The cold-publication choice is the main remaining product/architecture fork, not an unknown code location.

Concrete work sites are `src/output/` for the store, `server.h` and `server_events.cpp` for ownership and lifecycle boundaries, `Output` construction/DPMS/logical adoption for state updates, `actions.cpp` for explicit global wake semantics, and `meson.build` plus existing output tests for registration and coverage. Protocol-client changes belong with the power-series integration, not as an unbuildable dependency of this upstream-main-based branch.

Before landing:

1. Unit-test identity updates, clear, descriptor mismatch, missing identity, duplicate panels, and the explicitly selected connector-migration policy.
2. Extend `output/dpms` with repeated same-name destroy/create, no transient enabled commits, partial-off preservation, wake restoration, and disconnected-at-wake cases. Include an initially mode-less output test; headless alone is insufficient.
3. Extend logical enable/reload tests with `off → disable → enable → recreate`, unchanged and unrelated reloads, output-policy reloads, failed parsing, and failed power commits. Preserve memory on test-only transactions.
4. Exercise client discovery and output-management echo transactions while a cold remembered-off output exists. Assert the selected publication contract, not a fabricated current mode.
5. In `working`, extend the protocol client for real hotplug and require the event sequence appropriate to the chosen discovery contract. Test afk-dim's real consumer lifecycle separately.
6. Run locked-session wake/reconnect coverage: the existing secure mask in configured commits and lock presentation bookkeeping must still protect the first enabled frame. A dark output must not stall lock confirmation.
7. Verify the regression checks fail for the intended reason with memory deliberately absent, then run them with the fix. On hardware, observe repeated dark reconnects, wake during the disconnect interval, and journal/desktop behavior. The monitor's indefinite-pulse behavior remains unmeasured here.

## Verification performed

- Built this workspace's compositor against installed wlroots **0.20.2**, debug, harness enabled, warnings treated as errors.
- Existing `output_identity` **10 cases** and `output_enable_overrides` **7 cases** passed, compiled directly from current sources.
- Existing harness checks **`output/dpms`, `output/enable_actions`, `output/hotplug_restore`, and `output/management_disable` all passed** against the newly built compositor and relevant clients built from this checkout.
- An isolated baseline bounce probe reproduced relight, window restoration, and subsequent input leaving the other monitor off.
- An installed-library probe confirmed cold mode-list output `0×0/no global`, rejected disabled mode staging, and the headless geometry contrast. A separate existing-store probe demonstrated the unobserved-identical-peer migration case.
- Full Meson test configuration is blocked by missing **`libei-1.0` development metadata**. Tests were disabled for the compositor build; relevant clients and the two pure test binaries were compiled separately. No package installation, deployment, real DRM modeset, or physical panel test was performed.

Scratch probes and the fetched matching wlroots source are under `.test-agent/power-review/`; they are not a feature patch or permanent regression coverage. Build/harness success establishes the reviewed baseline, not correctness of an unimplemented fix.
