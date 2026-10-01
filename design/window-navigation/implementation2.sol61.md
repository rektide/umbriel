---
type: Verification
title: Automatic removal focus cancels visual travel
description: Implements the parent-selected removal policy while preserving replacement focus and reveal, with passing rendered and Meson verification.
status: draft
generated: { by: agent:gpt-6.1-sol, at: 2026-10-01 }
sources:
  - id: recovery-checkpoint
    resource: /design/window-navigation/implementation1.sol61.md
    title: Recovery verification and destination-removal policy finding
  - id: runtime-review
    resource: /design/window-navigation/review0.sol61.md
    title: Leading-focus runtime review
---

# Removal policy resolved

Parent decision: **cancel visual travel when automatic focus replacement follows selected-view unmap**. The requested destination no longer exists; redirecting its visual journey to an unrequested survivor is not authorized navigation. Preserve all existing replacement seat focus, reveal, activation and layout behavior.

Source checkpoint: `767cb305`, **Cancel visual travel on automatic removal focus without changing reveal**. The prior state and failing regression remain recorded in [`implementation1.sol61.md`](/design/window-navigation/implementation1.sol61.md); this is a new checkpoint, not a rewrite of that evidence.

## Exact change

- [`FocusReason`](/src/server/focus.h) adds `Removal` for automatic replacement when the focused view unmaps. `Directional` now describes intentional navigation, no longer automatic close replacement.
- [`View::handleUnmap`](/src/view/view.cpp) requests replacement focus with `Removal` at its existing call site. Replacement selection, sequencing, active-workspace/session-lock gates and the inactive-workspace remembered-focus path are unchanged.
- [`FocusManager`](/src/server/focus.cpp) names the reason `removal` in diagnostics and includes it in the same reveal switch branch as `Directional`: activate the focused column and `markArrange(true)`. Seat application, registry promotion, activation, hover invalidation and layout mechanics retain the prior behavior.
- [`Workspace::requestFocusTravel`](/src/workspace/workspace.cpp) does not admit Removal as a journey initiator. Different-target removal requests take the existing cancel/no-initiation path; same-target requests explicitly cancel too, rather than entering the quiet replay exemption.

All `FocusReason` usages and switches in `src/` were searched. Both exhaustive focus-manager switches include the new enumerator; the workspace same-target switch handles it explicitly. Intentional Directional, Startup and layer-dismissal replay behavior were not broadly changed. No new timer, focus stepping, renderer change, suppression flag or replacement-focus abstraction was introduced.

## Verification

1. `just check 226_focus_travel_destination -v`: **pass**. The predecessor regression is unchanged: a visible ring is established, the destination closes, the immediate post-removal scan must have no supplementary ring, actual frames converge, and survivor C alone is focused/activated.
2. `just check focus_travel 225_dwindle_close 230_transient_close 235_floating_close 110_scrolling -j4 -v`: **15/15 pass**, including **all 11 focus-travel checks**. This covers same-camera distinct retarget, real same/different-target clicks, same-target compositor grab, already-held exclusive layer and dismissal replay, live disable, destination lifetime, equal-size column swap, workspace departure/return and cross-output cancellation with both seat contexts.
3. Existing neighbor checks passed: [`110 scrolling layout`](/tests/harness/checks/110_scrolling_layout.sh), [`225 Dwindle close`](/tests/harness/checks/225_dwindle_close_focus.sh), [`230 transient close`](/tests/harness/checks/230_transient_close_focus.sh), [`235 floating close`](/tests/harness/checks/235_floating_close_focus.sh). These observe existing layout, stationary-pointer close replacement, transient parent restoration and floating-overlay MRU restoration behavior.
4. `meson test -C build-debug --print-errorlogs`: **62/62 pass** after the source change. The full compositor and affected core targets compiled successfully through the harness/Meson runs.
5. Formatting: `focus.h`, `focus.cpp` and `workspace.cpp` pass `clang-format --dry-run --Werror`. The changed `view.cpp` line passes the same check restricted to line 2920. A full-file formatting check reports an untouched pre-existing expression at lines 1252–1255; it was not opportunistically reformatted. No clang-tidy result is claimed.

No immediate-cancel threshold or assertion was weakened, and no harness script changed for this policy resolution. Settling remains measured by consecutive matching rendered frames. The selected lifecycle now ends visual travel immediately while preserving ordinary replacement focus and reveal.

## Remaining boundaries

The removal blocker is resolved. Existing scope limits remain: opt-in/default-disabled supplementary ring, potential duplicate focus accent near handoff, no independent focus-only timeline, no cross-workspace/output travel, and no live human visual trial. Same-target Gesture cancellation is source-reviewed rather than exercised by a synthetic runtime gesture here. No blanket full-harness run is claimed; the requested full Meson and targeted neighbor checks passed. No install, push, desktop operation or live configuration change occurred.

## Cross-references

- [Recovery checkpoint](/design/window-navigation/implementation1.sol61.md): predecessor failing evidence, source explanation and architecture choice now resolved.
- [Runtime review](/design/window-navigation/review0.sol61.md): required runtime corrections and verification gates; this checkpoint completes its destination-lifetime cancellation gate.
- [First-slice contract](/design/window-navigation/focus-travel0.sol61.md): visual travel rather than actual intermediate activation, shared camera clock and deliberate scope limits.
- [Parent groundwork review](/design/window-navigation/checkpoint0.sol61.md): source/presentation ownership and human-trial boundary.
