# Leading focus: implementation slices 0 and 1

---
type: Design
title: Leading focus travel implementation
description: Pure geometry, configuration, presenter, workspace integration, and harness evidence for the opt-in traveling focus ring.
status: draft
generated: { by: agent:glm-5.3-flash, at: 2026-10-01 }
verified: { by: agent:glm-5.3-flash, at: 2026-10-01 }
sources:
  - id: focus-travel0
    resource: /design/window-navigation/focus-travel0.sol61.md
    title: Leading focus first compositor slice
  - id: umbriel0
    resource: /design/window-navigation/umbriel0.sol61.md
    title: Umbriel feasibility
---

## Situation

Implementation of the opt-in traveling focus ring defined in
[focus-travel0](/design/window-navigation/focus-travel0.sol61.md). Slice 0 (pure geometry, configuration,
presenter) landed first; the parent review corrected the constraint policy and presenter color refresh; slice 1
wired the workspace integration and produced harness evidence. (This file's first revision had its frontmatter
after the heading, where it is not frontmatter; fixed here.)

## Geometry

- `focusTravelFraction(p, L) = clamp(p + L*p*(1-p))` — bounded, monotone, exact at both endpoints, per the contract.
- Strip-axis position follows the contract exactly: `mix(O, D1, p) + (q-p) * (D0 - O)`. Cross-axis position and both
  sizes morph on `q`.
- Edge rounding mirrors `interpolateBox` (round edges, width = right - left) and is the last step after
  floating-point geometry.
- Constraint (revised after parent review): the ring is **translated, never resized**, so ring plus complete border
  stroke (widths + 1 render margin) stays inside the viewport whatever distance the journey spans. A ring that
  cannot fit the viewport at all **skips travel explicitly** (`nullopt`) rather than clipping or pinning a box that
  would claim an endpoint fidelity the geometry does not have. The earlier edge-clamping version dropped the ring
  entirely when a far-offscreen raw box clamped both edges onto one boundary; the translation form preserves the
  visible-ring invariant for arbitrary-distance jumps, including far negative presented coordinates.
- Journey direction is the O→D1 displacement; the presented box D0 can lie far beyond D1 from either side, in either
  sign, on either strip axis. Tests cover right, left, down, and upward journeys with far presented boxes in both
  signs, plus zero-lead far journeys.

## Integration

The workspace owns the journey; the presenter stays dumb (`src/scene/focus_travel_ring.cpp` draws a
`wlr_scene_border` through `makeBorderRing`/`applyBorderGeometry`, holds no timeline and no destination pointer,
and refreshes its colors from configuration on every present so theme changes cannot resurrect a stale ring).

```text
FocusManager::focusView (before the workspace focus change, so the visible origin is still the previous focus)
  → Workspace::requestFocusTravel(view, reason)
      config gate: master + focus_travel enabled, else cancel
      same-destination replay (any reason, e.g. the async layer-dismissal refocus): no-op, travel keeps running
      different destination on any reason (pointer press/hover, grab, gesture, drag-drop, startup): cancel
      initiate reasons only: Directional | XdgActivation | ForeignActivation
      eligibility: mapped tile of this active scrolling workspace, not fullscreen/pinned, no switch transition,
      not locked, no overview
      origin: the previous focused view's presented box, or the ring's current visible box on retarget
  → Workspace::applyTiledMotion (before the same-motion early return)
      pending + animateMove + destination among the moving members → bind D0 = entry.from, D1 = entry.to
      bind refuses journeys whose endpoints cannot fit the viewport (the constraint would skip every frame)
      running ring + motion restarting: rebase only a pure strip translation (unchanged size, displacement along
      the one scroll axis); swaps-with-resize, resizes, and arbitrary reflow cancel instead
      same-motion continuation returns before touching anything: replays keep the running journey
      overview arrange cancels travel outright
  → Workspace::tickLayoutMotion
      the ring rides the same monotonic geometry progress already computed for the views — no second clock
      per-tick revalidation cancels on config-disable, session lock, overview, or a destination that stopped being
      a mapped tile of this workspace
      natural completion cancels: at p=1 the ring sits exactly on D1, so hiding it is the handoff
```

Cancellation and lifetime: `detachFromLayout` drops the destination before any dangling pointer can survive a
removal (unmap, float, workspace move all reach it); `setActive(false)`, `endLayoutMotion`, overview entry, and the
workspace destructor cancel; the ring's scene nodes are destroyed before the workspace tree in the destructor. The
constraint viewport is the usable area intersected with the strutted tiled area, so negative struts cannot promise
visibility off the output. FocusManager cancels the old workspace's ring when focus crosses to another workspace
while the seat is view-held; a layer-held seat reads as no view there, so picker-driven selection keeps its ring.

## Presenter decision

Supplementary ring in `colors.border.focused` with the configured border widths and corner radius. At `p=1` the
ring occupies exactly the destination's layout border, so hiding it coincides with the destination's own focused
border. Honest limitation: because the destination activates immediately, two focus-colored rings can coexist
briefly when the destination's border slides into view just before handoff; during most of the flight the
destination is offscreen, so the visible accent is the ring alone. Accent suppression during travel was rejected
for this slice: it couples into border color animation, close snapshots, and shader paths.

## Harness evidence (tests/harness/checks/221_focus_travel.sh)

One headless compositor, `windows_move` on a linear 1500ms curve, magenta `colors.border.focused`, border
animation off so the origin's accent swaps instantly and only the ring is magenta mid-flight. All assertions read
grim screenshots — IPC x/y are layout targets, final the instant a selection happens, so they cannot observe
mid-flight presentation. Journeys settle on a real clock (fixed 2s waits) because layout-target stability returns
immediately, and a warmup probe guards the headless output's lazy first commit.

Proven: the ring flies ahead of the pan (x≈590 at progress 0.4 where the camera path is flat), has left the origin
behind, exactly one focused/activated client mid-flight (no intermediate activation as the ring crosses columns),
same-destination replay keeps the journey, the ring lands on the destination's border and vanishes from the travel
band after settle, a new window's startup focus cancels a leftward journey within a frame, an exclusive keyboard
layer neither stops the ring nor lets the dismissal refocus replay restart it, and disabling the setting removes
the ring while focus and reveal carry on. Five consecutive passes including under four-way parallel load.

## Known gaps and tensions

- Unit-level geometry tests cannot judge feel; a live human visual trial is still required.
- Two focus-colored rings briefly coexist near handoff (see presenter decision); distinct-color or suppression are
  the follow-up options.
- Travel refuses when `windows_move` or master animation is off — there is no timeline to borrow. Deliberate.
- Selections that scroll nothing and journeys whose endpoints cannot fit the viewport fall back to ordinary
  behavior rather than a focus-only animation.
- Retarget rebases from the ring's current box with a fresh full-duration curve — no velocity continuity, matching
  how layout motion itself retargets.
- Overview open mid-flight lets the ring ride out hidden and complete, or cancels at the next arrange; a dedicated
  overview-open cancel hook was not added.
- Workspace-axis travel, cross-workspace and cross-output journeys, floating/fullscreen/pinned/scratchpad
  destinations remain unsupported by design (ordinary behavior applies).
- `330_overview_close_fade` fails identically at the pre-change baseline (dim=208) — pre-existing, not caused by
  this work; verified by building and running the baseline commit in a jj workspace.

## Verification state

Unit: 14 focus-travel geometry cases, 120 config-load cases including the focus_travel section, full suite 62/62.
Harness: `221_focus_travel` green repeatedly, plus activation/focus/overview/scratchpad/layer/keyboard neighbor
checks green except the pre-existing 330. clang-format clean; clang-tidy is not installed in this environment, so
the lint gate did not run.

## Cross-references

- [focus-travel0](/design/window-navigation/focus-travel0.sol61.md): the architecture and scope this implements.
- [umbriel0](/design/window-navigation/umbriel0.sol61.md): source evidence for the activation → reveal → shared
  motion path the ring borrows.
- `src/workspace/workspace.cpp` `applyTiledMotion`/`tickLayoutMotion`: the timeline being borrowed; the same-motion
  early return is why travel binds before it.
- `src/scene/hint_rect.cpp`: precedent for a compositor-owned transient scene target (the ring drops its
  independent AnimatedValues in favor of the borrowed timeline).
- `tests/harness/checks/430_drag_opacity.sh`: the screenshot color-sampling conventions this check follows.
