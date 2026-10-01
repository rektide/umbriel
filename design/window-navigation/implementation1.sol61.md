---
type: Verification
title: Focus travel recovery and lifecycle checkpoint
description: Completed split rendered-frame checks, verified landed runtime review fixes, and an unresolved destination-removal policy regression.
status: draft
generated: { by: agent:gpt-6.1-sol, at: 2026-10-01 }
sources:
  - id: runtime-review
    resource: /design/window-navigation/review0.sol61.md
    title: Leading-focus runtime review
  - id: implementation-predecessor
    resource: /design/window-navigation/implementation0.glm53.md
    title: Initial implementation and historical evidence
---

# Recovery checkpoint

This replaces neither the predecessor's argument nor its historical test record. The previous agent stopped at provider quota after source fixes through `80f023a2` and three unfinished harness drafts. Drafts were inspected and preserved in `e3c88d7b` before revision. Shared screenshot probes and the split checks were committed in `9a1ac006`; corrected probes and headless keyboard setup in `4d059f21`. No compositor source was changed during this recovery. No installation, push, desktop operation, or live focus-travel setting change occurred.

## Landed runtime behavior reviewed

- [`FocusManager::focusView`](/src/server/focus.cpp) captures the old seat selection before mutation, using registry recency when a layer owns the seat, and cancels source and target workspace rings across workspace/output boundaries.
- [`Workspace::requestFocusTravel`](/src/workspace/workspace.cpp) cancels same-target Grab, PointerPress, PointerHover and Gesture while preserving quiet Startup/DragDrop replays.
- Bind and rebase share `focusTravelJourneyAccepted`: all participating tiles must form one common strip displacement, and constrained endpoints must equal the natural boxes exactly. Equal-size same-axis column swaps are no longer mistaken for camera pans.
- Bind onto unchanged running camera targets stores the geometry fraction at binding; presentation normalizes the remaining borrowed progress. No second timer was added.
- [`focus_travel.cpp`](/src/layout/focus_travel.cpp) translates fitting rings without resizing and explicitly skips oversized boxes. Workspace viewport intersects usable and tiled regions; presenter destruction precedes parent-tree destruction; [`FocusTravelRing::present`](/src/scene/focus_travel_ring.cpp) refreshes colors.

These correct several now-stale descriptions in the predecessor (any-reason same-target preservation, destination-only rebase gate, and fixed settling sleeps). Its earlier green harness does not establish these later fixes.

## Verification results

`meson test -C build-debug --print-errorlogs`: **62/62 passed**, including geometry, motion, config and focus-travel tests. `bash -n` passed for all new helpers/checks. `just check focus_travel -j4 -v`: **10/11 passed**; destination removal remains red. Targeted rerun of destination removal also fails, but reaches framebuffer convergence, one activated/focused survivor C, and no supplementary ring at settlement. No new C++ edits required formatting; no clang-tidy result is claimed.

| Check | Observed result |
| --- | --- |
| [`221 journey`](/tests/harness/checks/221_focus_travel_journey.sh) | Leading visible ring, no intermediate activation, same-target replay, destination-border handoff; pass. |
| [`222 retarget`](/tests/harness/checks/222_focus_travel_retarget.sh) | Different stacked destination shares unchanged camera endpoints; early horizontal-stroke probe stays near original row instead of jumping to global progress; pass. |
| [`223 seat`](/tests/harness/checks/223_focus_travel_seat.sh) | Actual different-target and same-target clicks cancel supplementary ring; pass. |
| [`223 grab`](/tests/harness/checks/223_focus_travel_grab.sh) | Real Mod+press takes over selected destination before it moves/detaches; pass. |
| [`224 layer`](/tests/harness/checks/224_focus_travel_layer.sh) | Keyboard-enter proves layer already owns seat before activation; ring travels underneath, survives dismissal replay; pass. |
| [`225 disable`](/tests/harness/checks/225_focus_travel_disable.sh) | Disable while ring visibly travels removes it while ordinary reveal completes; pass. |
| [`226 removal`](/tests/harness/checks/226_focus_travel_destination.sh) | Old destination disappears; automatic replacement retargets supplementary ring instead of ending travel immediately; fail. Settlement and replacement activation pass. |
| [`227 swap`](/tests/harness/checks/227_focus_travel_column_swap.sh) | Same-axis equal-size column swap cancels; pass. |
| [`228 workspace`](/tests/harness/checks/228_focus_travel_workspace.sh) | Leave and return while original timeline would still run; ring does not reappear; pass. |
| [`229 output`](/tests/harness/checks/229_focus_travel_output.sh), [`layer-held variant`](/tests/harness/checks/229_focus_travel_output_layer.sh) | Cross-output activation cancels source ring for both view-held and exclusive-layer-held seat; both pass. |

All settle barriers compare successive actual grim frames, not IPC geometry or animation-sized waits. Fixed mid-flight delays deliberately choose an observation point; setup dispatch primer is 0.3 seconds. Shared helpers live in [`tests/harness/focus_travel.sh`](/tests/harness/focus_travel.sh); the two-output lifecycle is [`focus_travel_output.sh`](/tests/harness/focus_travel_output.sh). No exit client cleanup loops remain. Layer dismissal and destination close are genuine tested lifecycle operations.

The first attempted runs found incomplete/incorrect draft probes: undefined `assert_signal`, a retarget quiet band containing a vertical stroke, a click coordinate hitting B rather than C, and an exclusive-layer assertion with no headless keyboard. Corrections are test-only. Horizontal scanline stroke counting distinguishes the supplementary ring from a newly visible real focused border after cancellation.

## Decision required: automatic focus replacement after removal

[`View::handleUnmap`](/src/view/view.cpp), lines 2914–2923, selects replacement C using `FocusReason::Directional` **before** old D is marked unmapped/detached. Requesting travel to C captures the existing visible ring, changes its destination to C, and rebinds it. Later [`Workspace::detachFromLayout`](/src/workspace/workspace.cpp), lines 554–559, only cancels when the removed view is the journey's current destination: by then that destination is C, so removing D does not cancel it.

This is a fresh supported retarget rather than evidence of a dangling pointer, but conflicts with the requested immediate destination-removal cancellation. The close policy explicitly documents Directional as also covering focus-adjacent-after-close in [`focus.h`](/src/server/focus.h); silently changing that general reason would exceed a routine test repair.

Parent options:

1. **End lifecycle travel on close**: distinguish automatic removal focus from intentional Directional navigation, preserving the existing reveal and seat behavior but not launching/retargeting a ring. A focused reason or explicitly scoped travel-suppression mechanism needs parent judgment; changing every Directional to Startup is not proposed.
2. **Accept automatic replacement retarget**: explicitly revise the removal contract and regression to permit a new journey to the survivor. This retains current behavior, but is not the existing review gate.

Failing artifact directory: `/tmp/umv.G0lL7Xsk` (`removed.png`, converged `end.png`, compositor log). The harness itself chooses this temporary location; recovery did not create broad external scratch material. The failing regression is intentionally preserved for the parent checkpoint.

## Remaining limitations

- No live human visual trial; duplicate focus accent near handoff remains the documented supplementary-ring limitation.
- No runtime synthetic gesture test; same-target gesture cancellation is source-reviewed, not independently exercised here.
- Mid-flight color refresh, long-distance L/R/U/D containment and oversized fallback rely on source/unit evidence, not new screenshot scenarios.
- No full 212-check harness run is claimed. The full **Meson** suite passed; focused harness was run repeatedly at four-way concurrency. Historical baseline `330_overview_close_fade` failure was not rerun or reclassified.
- Finish the removal policy decision before reporting this runtime slice complete. Then rerun the focused harness and full Meson suite after any source change.

## Cross-references

- [Runtime review](/design/window-navigation/review0.sol61.md): exact corrections and lifecycle gates this checkpoint reconciles.
- [First-slice design](/design/window-navigation/focus-travel0.sol61.md): shared-clock supplementary indicator, supported endpoint contract and deliberate unsupported cases.
- [Parent checkpoint](/design/window-navigation/checkpoint0.sol61.md): architectural responsibility and authorization boundaries.
- [Contributor harness rules](/CONTRIBUTING.md#development-commands): rendered convergence, harness-owned cleanup and isolated concurrent checks.
