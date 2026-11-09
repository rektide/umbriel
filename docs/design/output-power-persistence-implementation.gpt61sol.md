---
type: Report
title: Output power memory — implementation and remaining gaps
description: Right-sized reconnect-dark implementation, selected fail-lit compromises, verification, and possible follow-ups.
status: draft
author: model:openai/gpt-6.1-sol#xhigh
date: 2026-11-09
resource: /docs/design/output-power-persistence-implementation.gpt61sol.md
sources:
  - resource: opencode://ses_e3e57dfcdffevKpkB6V5FcfdNI
    title: Implementation, boundary decisions, regression checks, and isolated cold-output and failure verification.
  - resource: /docs/design/output-power-persistence.md
    title: Original brief and pragmatic v1 addendum.
  - resource: /docs/design/output-power-persistence-review0.gpt61sol.md
    title: Appraisal findings and seven acceptance gates.
  - resource: /docs/design/configuration-reload.md
    title: Selective reload contract.
  - resource: /docs/design/workspace-lifecycle.md
    title: Evacuation and restoration contract.
  - resource: /src/output/power_overrides.cpp
    title: Session identity store.
  - resource: /src/output/output.cpp
    title: Constructor seeding and successful local power/logical transitions.
  - resource: /src/server/server_events.cpp
    title: Wake, reload, management transactions, and restoration.
  - resource: /src/server/server_outputs.cpp
    title: Physical eligibility and virtual-output lifecycle.
  - resource: /src/server/actions.cpp
    title: Explicit global power-on semantics.
---

# Output power memory — implementation and remaining gaps

Follow-up: [output power protocol support](output-power-protocol-implementation.gpt61sol.md) is now implemented in this checkout. This report's original verification and integration deferral describe the memory-only checkpoint; the follow-up report records protocol work and the consumer limitations that remain.

## Implemented contract

The implementation follows the brief's pragmatic addendum: a small in-memory off-choice store, constructor seeding before the first configured commit, and invalidation at existing success boundaries. It adds no timers, firmware rules, restore gate, resident logical outputs, or general state framework. The original brief and appraisal remain design evidence; this report describes the implemented choice, not a deployment claim.

- **Identity:** [`OutputPowerOverrides`](../../src/output/power_overrides.h) owns connector and descriptor strings, compared with the existing case-insensitive helpers. Both must match. No cross-connector migration or ambiguity learning. Missing display metadata stays connector-bound; missing connector identity cannot be remembered. Fresh sessions and unmatched identities start lit, subject to existing logical enablement policy.
- **First commit:** `Output` seeds `m_dpmsOff` beside `initialOutputEnabled`. A remembered-off, logically enabled output takes the existing disabled path, which does not stage a mode. Restoration keeps using existing physical-availability checks in `outputFromName` and `preferredOutput`.
- **Power writes:** successful `setPowered` calls remember off or erase on. Failed commits roll back the local flag without changing memory. Idempotent calls also update the choice: on erases stale memory; off renews a choice forgotten by an output-policy reload.
- **Logical transitions:** successful `applyOutputState` membership changes and `adoptOutputManagerEnabled` resets forget matching power memory. The latter already runs inside the committed `ok && !testOnly` management loop; another clear in that loop would duplicate the same boundary. Test-only, failed, and membership-preserving geometry transactions do not consume the choice.
- **Input wake:** the existing all-dark rule stays intact. Entering its wake branch clears the whole store, including absent heads, before attempting live wakes. Deliberate partial-off survives ordinary input.
- **Explicit global wake:** bare `dpms-on` does the same coarse clear regardless of partial-off and succeeds even when no output objects exist. This inexpensive addition uses the existing bare/named action boundary. Named on still requires a connected, logically enabled output; it does not resolve dormant records by name.
- **Reload:** only `effects.outputState` clears memory, beside logical enable overrides. It does not reset live DPMS flags. Identical content, unrelated changes, and failed parsing preserve both live and dormant requests.

## Cold-output publication is intentionally narrow

A remembered-off cold reconnect may have no current mode, zero geometry, and **no `wl_output` global until compositor-side wake**. This is intended. The patch does not briefly enable the display or manufacture wlroots mode/geometry state to make a bindable global. A surviving power-protocol client cannot assume it will receive a replacement control or an off event while the display remains dark. IPC `dpms-on` and all-dark input remain the recovery routes.

Output management still publishes logical enablement using existing backend metadata. A cold head may therefore be advertised as enabled with a synthetic zero-sized current mode. An exact echo can succeed: the sleeping-state validation accepts matching backend properties and commits the head disabled. A client selecting a real mode instead is rejected while it sleeps. The existing internally constructed logical transactions also echo backend state rather than needing a new cold-head path. This is not full publication coherence; clients must tolerate unusable geometry or wake the output first. No larger publication model was added.

## Remaining gaps and possible approaches

| Gap or deliberate compromise                                                 | Consequence                                                                                                                                                                                                           | Possible follow-up if observed                                                                                                                                                                                                        |
| ---------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Coarse clear before all-dark input or bare on, including failed wake commits | A failed live wake keeps its local dark flag, but loses reconnect memory; a later reconnect comes up lit. Ordinary named failed on retains memory.                                                                    | If preserving failed wakes becomes important, expire only absent identities and let connected successful commits erase their own records. This needs connected-inventory filtering; it is not necessary for normal HPD recovery.      |
| Output-policy reload clears memory, not live flags                           | An unchanged-membership dark output stays off until wake, but reconnecting before wake lights it. An explicit repeated off renews its choice.                                                                         | Add a deliberate live-wake/reset argument to the reload commit path, with rollback on commit failure. Keep identical/unrelated reloads inert.                                                                                         |
| Cold management-head metadata remains backend-derived                        | Layout clients selecting a real mode can reject until the cold output wakes. Exact echoes can succeed; no bindable output-power control is guaranteed before wake.                                                    | First consider withholding never-configured sleeping management heads and omitting them from internally constructed transactions. Independent logical geometry/publication is a much larger project.                                  |
| Incomplete or duplicate panel identity on the same connector                 | Indistinguishable monitor swaps can inherit off. Changed/missing metadata after a previously identified off request instead fails lit. Moving the same monitor to another connector also fails lit.                   | Require stronger identity or disable memory for unidentified panels if actual misidentification warrants losing virtual/no-EDID coverage. Do not copy enable-override descriptor migration without connected-peer ambiguity handling. |
| Physical destruction still destroys output-bound objects                     | The first disconnect evacuates windows and destroys layer/lock surfaces and the workspace group. Repeated pulses still generate backend/protocol object churn, but do not repeatedly return windows to the dark home. | An independent logical-output owner would address first-disconnect disruption. Keep that separate from this boolean-memory fix.                                                                                                       |
| No change to partial-off wake policy                                         | Input does not clear a dormant off choice while another logically enabled output is awake, unless the all-dark wake branch was taken earlier.                                                                         | Use bare `dpms-on`; make wake-any-on-input a separate explicit policy if desired.                                                                                                                                                     |
| Power protocol and afk-dim integration remain separate                       | This upstream-main feature does not provide or verify hotplug-safe `zwlr_output_power_manager_v1` controls or afk-dim's consumer lifecycle.                                                                           | Integrate with `pwr` in `working`, repair client registry/control lifecycle, and test discovery after compositor-side wake before switching providers.                                                                                |

## Verification and limits

Verified against installed wlroots **0.20.2**, debug build with warnings treated as errors. Implementation checkpoint: `4ee5def2`; regression checkpoint: `0668ebb6`.

```sh
meson setup --reconfigure build-debug -Dtests=enabled -Dwerror=true
meson compile -C build-debug -j 4
meson test -C build-debug --print-errorlogs -j 4
tests/harness/check.sh build-debug/umbriel output/ protocol/session_lock session/config_reload -j 4 -v
```

- **152/152 Meson tests passed**, including seven new [power-store unit cases](../../tests/unit/output_power_overrides.cpp): fresh state, idempotent updates, strict descriptor matching, unobserved identical peers, absent metadata/connector, owned strings, case-insensitive matching, and clear.
- **32/32 selected harness checks passed.** The six extended checks add repeated dark reconnects, physical power observations, no enabling/restoration log trail, unchanged refuge membership for ordinary and pinned windows, scratchpad refuge geometry, wake restoration, full output loss, input during a disconnect gap or with no objects, explicit global on while absent, logical disable/enable invalidation, selective reloads, test-only and geometry-only management preservation, dark lock confirmation, and secure-blank wake.
- **Sensitivity checked:** temporarily disabling only constructor seeding caused five extended checks to fail for relight/restoration or lost-off-state reasons (`dpms`, `enable_actions`, `management_disable`, `pinned_hotplug`, `scratchpad_restore`). The lock security check still passed, as expected for that independent invariant. The original seed was restored, rebuilt, and the 32-check run above passed afterward.
- **Supplementary cold/failure probe passed.** A compositor-only `LD_PRELOAD` diagnostic removed initial headless geometry and supplied an advertised preferred mode, adapting fixed-mode commits to the headless backend's custom-mode interface only at the test boundary. Three mode-less dark recreations produced **no enabled or mode-setting commit attempts**; registry traces showed one `wl_output` while the second output slept and two after wake. Injected failed named off/on commits preserved local state and reconnect choices. Exact management echoes and native logical actions preserved cold sleep; `wlr-randr --preferred` failed without consuming memory. A lock with that undiscoverable sleeping head confirmed normally; waking it without a replacement client lock surface displayed opaque black.
- **Document, shell-syntax, and formatting checks passed** for owned changes. Whole-file clang-format also reported pre-existing untouched formatting differences in `output.cpp` and `server_events.cpp`; those were not reformatted. `clang-tidy` is not installed, so no clang-tidy result is claimed.

Supplementary probes live in `.test-agent/power-memory/`; they are diagnostic evidence, not installed behavior or a new production test switch. No permanent mode-less backend or commit-failure injection facility was added. These experiments exercise the real compositor and installed library, but do not substitute for real DRM/KMS timing, link training, or power-series integration. Harness `settle` cannot be used while a sleeping head has pending work: it waits for unavailable frames; dark-state assertions instead use IPC/backend power, registry/commit traces, and restoration observations.

Against the appraisal's gates: **1** has unit coverage; **2–3** have permanent hotplug/logical/reload/management checks plus isolated cold/failure probes; **4** has isolated discovery and both accepted/rejected management transactions; **5** remains explicitly deferred to `working`/`pwr` and the consumer; **6** has permanent lock checks and an isolated mode-less locked wake; **7** has sensitivity coverage but still needs physical-panel observation. The “all-dark failed wake retains its record” and “reload wakes every live output” maximal semantics are not asserted: the deliberate fail-lit differences are listed above.

The acceptance scope is the same-connector HPD bug, preservation of deliberate partial-off, and ordinary fail-lit recovery. Hardware evidence still requires the Acer test: targeted off across several firmware pulses, all-dark input while the Acer is disconnected, and a journal/desktop watch for unwanted relight or repeated displaced-window returns. An overnight observation is needed to determine whether firmware pulses continue indefinitely. No actual panel test, session restart, integration merge, deployment, or push is authorized by this implementation.

## Related contracts

- [Original brief and pragmatic addendum](output-power-persistence.md): motivation and selected patch posture.
- [Implementation appraisal](output-power-persistence-review0.gpt61sol.md): the larger soundness envelope and acceptance gates; the table above accounts for deliberate departures.
- [Configuration reload](configuration-reload.md): unchanged-content and selective-effects boundaries, not wake-on-any-reload.
- [Workspace lifecycle](workspace-lifecycle.md#output-evacuation): first evacuation and saved-home restoration, reused rather than replaced.
- [User power controls](../user/outputs.md#display-power-management): recovery and visible reconnect behavior.
