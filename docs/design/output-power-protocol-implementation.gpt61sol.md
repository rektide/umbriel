---
type: Report
title: Output power protocol — integration and limits
description: Small wlroots protocol integration on the power-memory branch, reused pwr work, hotplug-safe test instrumentation, verification, and remaining gaps.
status: draft
author: model:openai/gpt-6.1-sol#xhigh
date: 2026-11-09
resource: /docs/design/output-power-protocol-implementation.gpt61sol.md
sources:
  - resource: opencode://ses_e3e57dfcdffevKpkB6V5FcfdNI
    title: Protocol integration, hotplug-capable harness client, regression checks, and cold-output/failure verification.
  - resource: /docs/design/output-power-protocol-assessment0.gpt61t.md
    title: User-commissioned Terra high assessment of the pwr branch.
  - resource: /docs/design/output-power-persistence-implementation.gpt61sol.md
    title: Existing session power memory and deliberately narrow cold-output publication.
  - resource: /protocols/wlr-output-power-management-unstable-v1.xml
    title: Unmodified protocol XML reused from pwr's 400a8f63.
  - resource: /src/server/server.cpp
    title: Display-owned manager creation and request-listener teardown.
  - resource: /src/server/server_events.cpp
    title: Protocol request adapter to the existing DPMS commit and memory boundary.
  - resource: /src/output/output.cpp
    title: Existing setPowered commit/rollback and session-memory behavior.
  - resource: /tests/harness/clients/output_power_client.cpp
    title: Live registry/control lifecycle and roundtrip barriers.
  - resource: https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/0.20.2/types/wlr_output_power_management_v1.c
    title: Initial state, commit-driven mode events, exclusive control, and failure behavior; read from the locally extracted 0.20.2 source.
---

# Output power protocol — integration and limits

## Architecture and reuse

The `power-memory` checkout now hosts version 1 of `zwlr_output_power_manager_v1`. This adapts `pwr`'s protocol/build plumbing (`400a8f63`) and wlroots request integration (`94102106`), without merging the older branch wholesale or adding another power-state store. Terra's [assessment](output-power-protocol-assessment0.gpt61t.md) records the reviewed four-commit `pwr` tip, `f008bb86`, and its test/documentation limitations. The original `pwr` workspace is unchanged.

The compositor addition is one display-owned wlroots manager and one Server request listener. Manager creation follows the existing startup error convention; the listener is unlinked before display teardown. No manager pointer is retained after initialization because no later code needs it.

```text
Wayland set_mode(on/off)
  → Server::onOutputPowerSetMode
  → Output::setPowered
  → existing configured commit / rollback
  → existing session power-memory update on success

successful physical enabled-state commit
  → wlroots' existing per-control commit listener
  → Wayland mode event
```

There are **no compositor-synthesized mode events, additional per-output watchers, geometry inventions, or restoration changes**. IPC, protocol, and input wake share the existing mechanism. Logical enablement remains separate. wlroots reports physical backend state rather than speculative request flags, including successful child commits in a grouped transaction that later fails elsewhere; a later rollback can report another transition.

The XML is copied unchanged with its license. Meson generates the server header required by the installed wlroots header, propagates its dependency through pure/core users, and generates client code only for the harness helper. The supported dependency range is unchanged; runtime verification uses installed **wlroots 0.20.2**, not an independently validated 0.20.1 installation.

## Discovery and request semantics

- Valid controls receive initial physical on/off. Subsequent enabled-state commits generate mode events, regardless of which control path caused the commit.
- `set_mode` calls the existing `setPowered` boundary, so successful protocol off survives same-identity hotplug and successful on forgets it. Logical disable rejects the request; idempotent requests update memory without a hardware commit.
- Output controls are exclusive across clients. wlroots sends `failed` when another controller already owns the output, the output disappears, or an inert output is used. Clients should destroy failed controls and follow registry removal.
- A mode-less remembered-off output can have **no `wl_output` and no replacement power control until compositor-side wake**. It does not generate a fictional off event while undiscoverable. The first replacement control after wake initially reports on. Use all-dark input or IPC `dpms-on` when no client-addressable output exists.
- The manager is hidden from security-context clients by default, consistent with output configuration. Existing explicit per-application global grants can expose it.

## Deliberate limits and follow-ups

| Limit                                                           | Meaning and possible follow-up                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| --------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| No mandatory acknowledgement for rejected/idempotent requests   | wlroots' request callback carries an output and mode, not a requesting resource or result channel. A logical-policy rejection or failed commit leaves the previously reported state in force, without necessarily producing a fresh mode or `failed`. The XML's unsupported-mode failure wording is stronger than this adapter/library result path. Do not treat silence as success. Stronger targeted failure/inert-control semantics would need an upstream wlroots API improvement or a more involved manager implementation; neither is added for this normal-path integration. |
| Cold discovery remains delayed                                  | No independent logical geometry/global is introduced. Clients need registry lifecycle handling and a compositor-side wake route when the head has no global. The existing cold management-head metadata limitation is unchanged.                                                                                                                                                                                                                                                                                                                                                    |
| Actual afk-dim lifecycle remains separate                       | Its current consumer snapshots outputs rather than tracking removal/recreation; repairing the harness client does not repair that application. Do not switch its provider or claim hotplug-safe afk-dim integration merely because the protocol global is now present.                                                                                                                                                                                                                                                                                                              |
| Existing memory compromises remain                              | Coarse global wake and selective reload semantics are unchanged; see the [memory implementation report](output-power-persistence-implementation.gpt61sol.md#remaining-gaps-and-possible-approaches).                                                                                                                                                                                                                                                                                                                                                                                |
| Hardware and deployment are not covered by source/harness tests | The Acer's real HPD cadence, panel behavior, and journal/desktop outcomes still require the hardware observation from the original brief. No deployment, session restart, provider switch, push, or integration-workspace merge is part of this change.                                                                                                                                                                                                                                                                                                                             |

## Test instrumentation

The new [output-power client](../../tests/harness/clients/output_power_client.cpp) follows the registry continuously rather than taking `pwr`'s startup-only snapshot. Each entry owns its global ID and proxies. Naming and manager discovery both attempt control creation, so discovery order does not matter. Removed entries release their proxies and are excluded from command lookup; listener data remains allocated until disconnect to avoid dangling queued callbacks. Failed controls are destroyed without retrying an exclusive-owner failure in a loop.

The event pump dispatches pending events before waiting, uses Wayland's prepare/read/cancel pattern, and accepts line commands through stdin. `sync LABEL` performs a server roundtrip, letting negative assertions test a completed request without guessing an acknowledgement or sleeping. It also starts successfully with zero outputs, so later first-output discovery is exercised. This is test instrumentation, not a new installed consumer.

Logical disable removes a `wl_output` global without destroying the backend output or invalidating an already-held power control. The helper's explicit `retain`/`release` commands keep one control through that withdrawal solely to exercise policy rejection, then release it before ordinary rediscovery. Default hotplug behavior still retires removed globals promptly.

## Verification

Implementation checkpoint: `4fc78728`; protocol instrumentation/regression checkpoint: `72c3acde`. Debug compilation treats warnings as errors.

```sh
meson setup --reconfigure build-debug
meson compile -C build-debug -j 4
meson test -C build-debug --print-errorlogs -j 4
tests/harness/check.sh build-debug/umbriel output/ protocol/output_power \
  protocol/security_context protocol/session_lock session/config_reload -j 4 -v
just check-stress protocol/output_power 8
```

- **152/152 Meson tests passed** with the generated header dependencies and wlroots 0.20.2.
- **35/35 selected harness checks passed** in the final output, protocol, security, lock, and selective-reload sweep. **8/8 concurrent copies** of the protocol regression also passed.
- The permanent [protocol check](../../tests/harness/checks/protocol/output_power.sh) passed: initial on/off, exclusive-controller failure, requested transitions, idempotence, partial-off preservation, all-dark input wake, IPC events, test-only management silence, logical disable/re-enable and rejection, three remembered-dark hotplugs, targeting the new live same-name control, and zero-output startup followed by first discovery. The final control remains live during the harness's clean compositor-shutdown assertion.
- The default-denial security check failed against the pre-integration compositor because the manager was absent, then passed after integration. Explicit per-application exposure is also covered by the extended security-grant check.
- **Sensitivity checked:** temporarily removing only the `setPowered` routing from the request callback made the protocol regression fail at its first requested on, with initial off unchanged and no on event. The real routing was restored, rebuilt, and the final 35-check sweep and eight-way stress run passed afterward.
- **Supplementary cold/failure integration probe passed** using the existing isolated headless interposer under `.test-agent/power-memory/`. Failed protocol off/on commits produced no false mode or failed event, preserved physical/local state and memory, and left controls unfailed. Three mode-less recreations produced no enabling/mode-setting commits and no replacement control or off event. Compositor-side wake then created a new addressable control reporting initial on. The same client could subsequently request off/on through that new control; both IPC and protocol wake without a replacement lock surface presented secure black while the session remained locked.

The supplementary probe is under `.test-agent/power-protocol/`; it uses the real compositor and installed library but is not a permanent synthetic backend or production failure switch. Real DRM/KMS timing and Acer firmware behavior remain unverified here. Owned Markdown/provenance, shell syntax, and C++ formatting checks passed; no clang-tidy result is claimed because that tool is not installed.
