# Leading focus: implementation slice 0

---
type: Design
title: Leading focus travel implementation slice 0
description: Pure geometry, configuration, and presenter committed; workspace integration designed and pending.
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

First implementation slice for the opt-in traveling focus ring defined in
[focus-travel0](/design/window-navigation/focus-travel0.sol61.md). Three commits landed:

1. `src/layout/focus_travel.{h,cpp}` + `tests/unit/focus_travel.cpp` — pure geometry, 11 cases green.
2. `[animation.focus_travel]` config (`enabled=false`, `lead=0.6`), docs, example, config-load cases — 120 cases green.
3. `src/scene/focus_travel_ring.{h,cpp}` — presenter over the existing umbrielfx border primitive.

Full unit suite (62/62) and the packaged-example validation pass. No runtime behavior changed yet: nothing
constructs the presenter and the config default is off.

## Geometry decisions

- `focusTravelFraction(p, L) = clamp(p + L*p*(1-p))` — bounded, monotone, exact at both endpoints, per the contract.
- Strip-axis position follows the contract exactly: `mix(O, D1, p) + (q-p) * (D0 - O)`. Cross-axis position and both
  sizes morph on `q` (the doc's "morph size and cross-axis position with a bounded fraction"). Tension noted below.
- Edge rounding mirrors `interpolateBox` (round edges, width = right - left) and is the last step; everything
  upstream is double.
- Constraint squeezes ring + full stroke (border widths + 1 render margin, via `focusTravelStrokeExtent`) into the
  usable/tiled area; a box larger than the viewport pins to the leading edge — explicit mostly-visible policy,
  documented as such rather than claimed containment.
- Degenerate viewport or degenerate origin/final boxes skip travel (`std::nullopt`); a degenerate presented D0 is
  survivable since the constraint works from O and D1.

## Ownership and API design (integration slice, not yet written)

The workspace is the motion owner; the presenter stays dumb.

```text
FocusManager::focusView (reveal switch, before activateFocusedColumn + markArrange)
  → Workspace::requestFocusTravel(view, reason)
      initiate reasons: Directional | XdgActivation | ForeignActivation, config-gated
      same-destination replay (any reason): no-op — travel keeps running
      any other focus change to a different view: cancel (pointer press/hover, grab, gesture, drag-drop, startup)
  → Workspace::applyTiledMotion, before the same-motion early return:
      pending + animateMove + destination among moving members → bind D0 = entry.from, D1 = entry.to
      destination stable or absent, or motion not animated → cancel (ordinary fallback, per doc)
      active travel + motion restarting: rebase (origin := ring's current visible box, D0/D1 from the
      destination's new entry) or cancel when the destination left the moving set
  → Workspace::tickLayoutMotion:
      same progress already computed for view interpolation (geometryCurve.value(progress.progress()));
      present(constrainedFocusTravelBox(path, p, tiledArea(), appearance widths))
      motion completion path cancels — at p=1 the ring sits exactly on D1, so hide is a seamless handoff
```

State lives in `Workspace`: `{View* destination, FocusTravelPath path, bool pending, bool active}` plus a
`std::unique_ptr<FocusTravelRing>` parented under the workspace tree.

Lifetime and cancellation:

- `Workspace::detachFromLayout(view)` clears the destination when the view leaves the layout (unmap, float,
  workspace move) — the chokepoint that guarantees no raw view reference survives removal. `removeView` reaches it;
  so does `~View` → `setWorkspace(nullptr)` → `removeView`.
- Each tick re-validates before use: config still enabled (master + focusTravel), not session-locked, destination
  mapped, tiled, still in a column of this workspace. Covers scratchpad hiding, float toggles, config-disable
  mid-flight, and lock without extra coupling.
- `Workspace::setActive(false)` and the `Workspace` destructor cancel; `applyTiledMotion` under overview cancels on
  the next arrange; travel never initiates while the overview is active.
- No new frame clock: travel is active only while the borrowed motion runs, so `layoutMotionActive()` already keeps
  frames scheduled; the presenter registers no Animatable.

Layer-shell async (Vicinae): the ring starts from the arrange, which runs even while an exclusive keyboard layer
holds the seat; the later dismissal refocus replays the same destination (DragDrop/Startup reason) and hits the
same-destination no-op instead of restarting or cancelling. The presenter never touches seat focus, so a pending
indicator cannot mutate focus.

## Presenter decisions

Supplementary ring in `colors.border.focused` with the configured border widths and corner radius. At `p=1` the ring
occupies exactly the destination's layout border, so hiding it coincides with the destination's own focused border —
a clean-looking handoff without touching `ViewDecoration`. Accent suppression during travel was rejected for this
slice: suppressing the destination's accent couples into border color animation, close snapshots, and shader paths.

## Tensions

- Two focus-colored rings can coexist briefly when the destination's own border slides into view just before
  handoff. Self-resolving at `p=1`; distinct-color ring or accent suppression are the follow-up options.
- Ring squeezes against viewport edges rather than clipping: reads as pushing into the edge, but is a visible
  choice; scene-clip containment is the alternative if it feels wrong in the visual trial.
- Strip size morphs on `q` (doc reading); an alternative pins strip size to `p`. Subtle; the visual trial decides.
- Retarget rebases from the ring's current box with a fresh full-duration curve — no velocity continuity, matching
  how layout motion itself retargets.
- Selections that scroll nothing cancel rather than animating a focus-only hop (the doc allows this fallback).
- `windows_move` disabled (or master animation off) means no timeline to borrow: travel refuses and ordinary
  behavior applies. Deliberate — a second timeline is out of scope.
- Single active workspace, both strip axes; workspace-axis travel and cross-output remain future per the doc.

## Verification state and next steps

Done: geometry cases (endpoints both axes, lead 0/extremes/clamps, long L/R/U/D jumps, nonzero viewport origin,
stroke margins, degenerate inputs, oversized determinism), config cases (defaults, opt-in, clamp diagnostics,
unknown timeline keys), full suite, format.

Next slice: workspace + focus integration exactly as designed above, then a headless harness check that observes
frames — grim pixel sampling mid-flight with a distinctive `colors.border.focused`, containment and endpoint
handoff assertions, retarget and cancellation paths, activation while an exclusive layer exists followed by layer
unmap. IPC x/y are layout targets, not presentation, so assertions read screenshots only.

## Cross-references

- [focus-travel0](/design/window-navigation/focus-travel0.sol61.md): the architecture and scope this implements.
- [umbriel0](/design/window-navigation/umbriel0.sol61.md): source evidence for the activation → reveal → shared
  motion path the ring borrows.
- `src/workspace/workspace.cpp` `applyTiledMotion`/`tickLayoutMotion`: the timeline being borrowed; the same-motion
  early return is why travel binds before it.
- `src/scene/hint_rect.cpp`: precedent for a compositor-owned transient scene target (the ring drops its
  independent AnimatedValues in favor of the borrowed timeline).
