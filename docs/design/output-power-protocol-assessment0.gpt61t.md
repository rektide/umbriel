---
type: Assessment
title: Wlr output power protocol — pwr branch assessment
description: Source review of the pwr implementation, wlroots behavior, integration with session power memory, and a scoped test recommendation.
status: draft
author: model:openai/gpt-6.1-terra#high
date: 2026-11-09
resource: /docs/design/output-power-protocol-assessment0.gpt61t.md
sources:
  - resource: opencode://ses_e3e2a6050ffeXghTNrvA3JjaLf
    title: Read-only pwr branch and wlroots 0.20.2 source assessment for power-memory integration.
  - resource: /docs/design/output-power-persistence-implementation.gpt61sol.md
    title: Implemented memory contract and intentionally narrow cold-output publication.
  - resource: /docs/design/output-power-persistence-review0.gpt61sol.md
    title: Prior appraisal including client lifecycle and cold output findings.
  - resource: file:///home/rektide/src/umbriel-pwr/src/server/server_events.cpp
    title: Reviewed pwr request callback at revision 94102106.
  - resource: file:///home/rektide/src/umbriel-pwr/tests/harness/clients/output_power_client.cpp
    title: Reviewed pwr test client at revision 5cc3321e.
  - resource: https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/0.20.2/types/wlr_output_power_management_v1.c
    title: wlroots 0.20.2 manager implementation, checked against the locally extracted release source.
---

# Wlr output power protocol — pwr branch assessment

## Recommendation

**Reuse the small wlroots manager integration, adapting it onto the memory branch; do not reimplement the protocol or port a second power-state store.** Its callback already calls `Output::setPowered`, the right success/rollback and memory boundary. Reuse the protocol XML, build plumbing, security check, and baseline test as useful starting points, but repair the test client's lifecycle and narrow the branch's documentation claims. Make no synthetic output global, temporary enabling commit, or additional restoration path. This is source review, not a build, integration run, hardware result, or acceptance of the branch.

The clean read-only `~/src/umbriel-pwr` tip `f008bb86` contains four commits: `400a8f63` vendors XML and header generation; `94102106` adds the manager and callback; `5cc3321e` adds the harness client/test; `f008bb86` adds the security test and user documentation. The target already has the independent memory implementation (`4ee5def2`, regressions `0668ebb6`, report `e5fb2118`). Apply changes deliberately against that destination, not as an assumption that the old branch's full context is current.

## Actual protocol behavior

- **No manual success reporting at this tip.** `pwr/src/server/server_events.cpp:1921–1937` maps wlroots' valid on/off request to `setPowered`; it emits no protocol event. The old concern that this branch itself manually reports successful changes is not borne out by these files. The comment in `server.h:785–788` correctly attributes reporting to wlroots commits, within the qualifications below.
- wlroots 0.20.2's `types/wlr_output_power_management_v1.c:53–69,111–165` sends initial mode from `output->enabled` when a valid control is created, then sends mode on a successful output commit carrying `WLR_OUTPUT_STATE_ENABLED`. It reports physical state, not speculative `m_dpmsOff` or logical desktop membership. An enabled-state commit can be caused by IPC, input, output management, or the protocol. There is no need for a second event listener or explicit success event. The commit is the appropriate observation boundary, even though backend commit success is not a guarantee of physical panel illumination.
- `setPowered` on the target (`src/output/output.cpp:1216–1248`) returns false for logical disable, rolls the flag back on a failed commit, writes memory only on success, and makes idempotent requests update memory without committing. Thus a failed or logically disabled request changes no memory and normally produces **neither a new mode nor `failed`**; an idempotent request likewise has no required acknowledgement. A client retains its previously received mode, not a new off response to each rejected on request. `pwr/docs/user/outputs.md:385–393` overpromises an off report on rejected on and “after every change” without explaining commit/discovery limits. Qualify these claims. The XML says unsupported modes send `failed`; the wlroots set-mode event (`wlr_output_power_management_v1.h:37–40`) exposes the output and mode, not the requesting control, and the library callback has no result path. The branch logs rejection and does not implement that failure notification. This is a real limitation; distinguish logical policy rejection/commit failure from wlroots' own control failure. For this right-sized integration prefer truthful documentation and a test of no false success rather than custom manager internals unless a real consumer requires stronger failure semantics.
- wlroots validates invalid enum values as protocol errors before the callback (`.c:71–94`). A second control for an output fails, and control creation for an inert output fails; destruction of the underlying output sends `failed` and unlinks its listeners (`.c:19–50,138–164`). Client manager destruction alone does not destroy its controls (vendored XML:60–64). Do not report duplicate controls as supported.
- `pwr/src/server/server.cpp:762–764,1024,1101–1120` installs one manager listener, removes it before destroying clients and the display; the wlroots display destroy handler asserts no listeners and destroys the global (`.c:192–227`). This ordering is appropriate on the normal fully initialized path. Creation can return null on allocation/global failure and branch dereferences it immediately; either follow the project's startup failure convention or explicitly guard it. No extra per-output compositor listener ownership is needed.

## Memory, discovery, and security

The target's `outputFromWlr` (`src/server/server_outputs.cpp:132`) resolves managed outputs independently of physical enabled status; do not replace it with name/preferred lookup, which filters sleeping heads. A protocol off reaching `setPowered` therefore writes the same strict connector+descriptor session memory as IPC. Successful on erases it; rejected requests do not. Existing logical transitions, selective reload, and coarse all-dark/bare-on clearing retain their documented behavior, including reload clearing the store without waking live heads. Commit callbacks report actual backend enabled transitions; they must not be wired to mutate memory independently.

On output destruction the old control becomes failed/inert. A headless recreation may offer a fresh named `wl_output` and a new control whose initial mode is off. A genuinely cold remembered-off output may have zero geometry and **no global at all until compositor-side wake**; initial off and even a replacement control cannot be promised in that case. Input all-dark wake or bare IPC `dpms-on` remain recovery routes; a client cannot power on an output for which it cannot bind a `wl_output`. See [implementation report](output-power-persistence-implementation.gpt61sol.md#cold-output-publication-is-intentionally-narrow).

The global filter is default-deny for security-context clients, with explicit per-app grants (`pwr/src/server/server.cpp:80–112,153–189`). The manager is not in the default allowlist; `pwr/tests/harness/checks/protocol/security_context.sh:61–64` checks it is hidden while the normal client sees it. It is **restricted by default**, not categorically unavailable to a sandbox with an explicit configured grant; avoid the unqualified term “restricted-only” if it suggests otherwise. Test both default denial and explicit grant where the existing harness supports it.

## Dependency and client assessment

`pwr/meson.build:56–59,185–196,239–249,771,913` assumes wlroots `>=0.20.1,<0.21.0`, generates the server header locally because the installed wlroots header includes it, and generates client header/private code for the harness. `src/wlr.h:51` includes the manager header. The installed **0.20.2** headers and locally extracted 0.20.2 implementation support this path; this review did not independently verify the 0.20.1 implementation or compile either revision. Do not claim broader version validation. Preserve generated-header dependency propagation to all targets including core users and tests.

The existing test client is **not hotplug safe**: `pwr/tests/harness/clients/output_power_client.cpp:76–93` binds newly advertised outputs but drops global removal and does not retain registry IDs; `:188–193` creates controls only for the startup snapshot; `:95–105` selects the first same-name entry, including an old failed entry. Its `failed` handler destroys the control (`:43–50`) but leaves the entry and output proxy. It also requires at least one startup output (`:173–186`), so cannot observe a later first output. Repair it to associate live entries with registry global IDs, create one control for each eligible new output after manager discovery and naming, retire/release removed outputs and failed controls safely, and select live controls by name. Handle ordering between manager and output discovery, plus delayed name and manager removal where applicable; avoid callbacks referring to freed entries with queued events. The event pump must dispatch queued events correctly, not only poll for new socket readability. This is test instrumentation, not a substitute for a real consumer.

The separate afk-dim consumer still snapshots outputs and does not track hotplug (`~/src/afk-dim/src/brightness/wlr_power.rs:304–325,340–380`); `with_session` drops the session on connection-level errors but not `ProtocolFailed` or `OutputAbsent` (`:393–415`). Rebinding on an old proxy is not discovery. Do not claim afk-dim hotplug support or change its provider configuration based on compositor protocol support alone.

## Scoped verification matrix for integration

1. Build with the generated header dependencies and installed wlroots 0.20.2; run the existing pwr protocol harness plus relevant output/memory and security checks. Verify initial on and initial off for already-addressable heads, requested off/on, IPC-driven transitions, partial-off input preservation, and all-dark input wake. The original pwr test (`tests/harness/checks/protocol/output_power.sh:40–90`) covers the basic lit startup and transitions, not hotplug or failures.
2. Assert no speculative on/off event after injected failed commits, logically disabled requests, or idempotent requests; assert memory and physical state remain correct. Do not require an extra response where none is emitted. Check logical disable/re-enable and output-management commits observe actual physical changes, not mere staged/test-only state.
3. With a continuously connected repaired client, destroy and recreate the same named output repeatedly: old control fails, old registry entry is retired, a newly advertised head gets one new control and the correct initial physical mode, and on/off targets the new live object. Include exclusive second-controller failure and clean shutdown with live controls. Test startup without outputs and subsequent appearance.
4. Separately exercise the real mode-less cold condition if a suitable isolated probe exists: assert no premature global or enabling commit, then compositor-side wake and discovery; do **not** demand an off event for an undiscoverable head. Headless's preexisting geometry cannot alone prove this condition.
5. Verify the restricted default global and any configured grant, and run locked-output wake coverage so first visible content remains securely blanked. Test the actual afk-dim lifecycle separately before rollout; physical HPD/panel behavior remains hardware verification, not established by this review.
