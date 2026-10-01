# Leading focus: first compositor slice

## Destination and boundary

After the initial feasibility investigation, the user asked to get existing smooth scrolling configured through Compfuzor, then chase the leading-focus effect. The preferred effect is a traveling visual focus ring. Passing actual focus through intermediate windows is tolerated as a shortcut, not desired as the eventual design. Initial work can ignore multiple vertically arranged workspaces, but eventual cross-workspace support is explicitly wanted.

The configuration slice is independent and lives in Compfuzor. This proposal develops the first **opt-in compositor slice**, without installing a patched compositor or changing upstream defaults. Supporting evidence and configuration tradeoffs are in [the Umbriel investigation](/design/window-navigation/umbriel0.sol61.md).

## Selected architecture

- Selection continues to use existing activation and `window-focus` entry points. No picker-specific protocol or window-title matching.
- The normal focus manager remains authoritative for seat focus, activation, MRU, urgency, and workspace selection. The traveling ring is presentation, not focus stepping.
- A scrolling-workspace navigation request captures the visible origin before focus changes. A running ring can supply that origin when another selection interrupts it.
- The ring borrows the **monotonic geometry progress** of the workspace's shared layout motion. It must not use the raw spring/shader progress or restart a timer on every frame.
- A small pure geometry module determines the traveling box. A scene presenter draws it using the existing `umbrielfx` border primitive and border geometry helpers.
- The workspace motion owner coordinates the request, start, tick, completion, and cancellation. Do not put navigation policy in the renderer or give the scene presenter a raw pointer to a destination window that can disappear.

Separate projection from ownership: a presenter that accepts a box and can hide is reusable for later workspace-axis travel. An implementation tied to a particular layout-motion clock is intentionally only the first coordination policy, not a universal multi-workspace engine.

## Geometry model to test

For a same-workspace strip translation, let:

- `O`: the visible origin box at the start;
- `D0`: the destination's presented box before the pan (possibly offscreen);
- `D1`: its final layout box;
- `p`: shared monotonic geometry progress, in `[0, 1]`;
- `L`: optional lead strength, in `[0, 1]`;
- `q = p + L * p * (1 - p)`: a bounded, monotonic path fraction ahead of camera progress.

The ring's strip-axis position before viewport constraint is:

```text
mix(O.axis, D1.axis, p) + (q - p) * (D0.axis - O.axis)
```

Equivalently, move from `O` toward `D0` at `q` while adding the camera translation `(D1.axis - D0.axis) * p`. This equivalence assumes a pure translation. On long journeys the lead projects toward the destination-facing viewport edge; constraint keeps the ring visible until the actual destination comes into view. On short journeys it glides directly between borders.

Morph size and cross-axis position with a bounded fraction, and include the complete border stroke when constraining to the usable viewport. At `p=1`, the indicator must meet the destination's visible border, not remain stranded at a lead offset. `L=0` supplies a useful baseline. Preserve floating-point geometry until final pixel rounding.

The important invariants are bounded progress, visible projection, correct direction, exact supported endpoints, and deterministic behavior on both strip axes. This formula is a candidate to verify, not an assertion that it will feel right without a visual trial. If a box cannot fit the viewport, skip travel or define an explicit mostly-visible policy rather than claiming full containment.

## First supported behavior

Navigation within the **same active scrolling workspace and output**, between eligible tiled views, for foreign activation/explicit window selection and directional keyboard navigation. Both horizontal and vertical strip axes should use the same geometry model.

Do not initiate travel for hover, pointer presses, startup, grabs, or gesture settling. Do not initiate it during overview or a workspace slide. Fullscreen, floating, pinned, scratchpad, and cross-output destinations retain existing behavior. Ordinary same-workspace selection that causes no scrolling may fall back initially rather than creating a second focus-only timeline.

The first patch must be explicit about whether it draws a supplementary indicator or suppresses the destination's normal focused accent during travel. A clean handoff is desirable, but suppressing presentation must never mutate real activation or intermediate-app focus. Preserve ordinary border rendering when disabled and restore it on every cancellation path.

## Configuration and lifecycle

Use a small opt-in setting, provisionally `[animation.focus_travel] enabled = false` and `lead = 0.6`. Reuse `windows_move` curve/duration rather than introducing an independent clock. Naming can be refined against current configuration conventions; do not imply this changes keyboard-focus semantics.

Required lifecycle behavior:

- another eligible selection retargets from the currently visible ring rather than the previous logical destination;
- unrelated layout reflow either safely rebases or cancels the ring; do not apply pure-translation math to arbitrary reflow;
- manual gestures/grabs, unsupported focus changes, loss/movement/unmap of destination, workspace/output removal, overview, session lock, and config disabling cancel safely;
- completion restores normal destination presentation and removes the transient scene target;
- cancellation leaves current focus alone and schedules any frame needed to erase the indicator;
- no raw view lifetime dependency should survive a view's removal.

## Verification gates

Pure geometry tests should cover endpoints, lead zero, lead extremes, long left/right/up/down jumps, nonzero output origin, border margins, differing supported sizes, invalid/degenerate viewport, and progress bounds. Configuration tests should cover parsing, defaults, validation, and reload effects.

Runtime verification must observe frames, not only the IPC layout-target positions. Use an isolated headless compositor and distinctive ring colors/widths to verify mid-flight pixels, containment, endpoint cleanup, retargeting, and cancellation. Also verify that intermediate apps do not receive activation merely because the indicator crosses them. A live human visual trial remains necessary to judge the feel.

## Checkpoint before widening

Review the emerging code for ownership, motion-progress coupling, and lifetime cleanup before expanding beyond this slice. Record what became possible, what remains unsupported, and whether the geometry actually expresses leading travel. Do not silently add cross-workspace machinery or replace navigation semantics with actual focus stepping.

## Cross-references

- [Scope](/SCOPE.md): clients/scripts are preferred where sufficient, embedded scripting is excluded, and new visuals must use `umbrielfx`. This patch direction is a potential refinement, not a claim of upstream acceptance.
- [Contributor principles](/CONTRIBUTING.md#design-principles): separate policy from mechanism and retain the single-threaded event loop.
- [Shared geometry motion](/src/workspace/workspace.cpp): `applyTiledMotion` and `tickLayoutMotion` own the actual tiled presentation timeline.
- [Animation math](/src/core/animation.h): `MonotonicEasing` differs from raw spring progress; use the geometry projection for alignment.
- [Border helpers](/src/scene/border_rect.h) and [view decorations](/src/view/decoration.cpp): renderer-supported ring geometry and existing color/opacity behavior.
- [Overview ownership](/docs/design/overview-rendering.md#animation-ownership): precedent for borrowing presented geometry rather than inventing a second competing window-motion model.
