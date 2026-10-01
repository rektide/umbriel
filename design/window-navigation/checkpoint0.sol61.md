# Leading-focus groundwork: parent review checkpoint

## State

The implementation groundwork is committed: pure path geometry, opt-in configuration, and a renderer-backed ring presenter. It is not yet runtime-wired. The implementation agent reports 11 geometry cases, 120 config cases, the 62-test Meson suite, formatting, and the full compositor link passing. Parent review read the actual geometry, presenter, tests, config parsing, and implementation note; it did not rerun those checks.

Source stage: geometry `6c26292e`, config `89820248`, presenter `c60a7528`, implementation note `3439abcc`. Direction remains [the first-slice proposal](/design/window-navigation/focus-travel0.sol61.md). Configuration-only smooth scrolling has already been applied separately through Compfuzor.

## Accepted shape

- Pure path math and a dumb border presenter are a sound separation.
- Borrow the monotonic tiled-geometry progress, not a second clock.
- Keep seat focus and actual activation unchanged; supplementary indicator is acceptable for this first visual trial, with its duplicate-accent limitation explicit.
- Runtime integration belongs with workspace motion ownership and existing focus-reason policy. Cross-workspace travel remains out of this slice, not abandoned as a future direction.

## Corrections required before integration

1. **Arbitrary-distance containment:** independent edge clamping can collapse a fully offscreen ring to zero width/height. For a ring that fits the viewport, constrain by translating its origin into the border-inset viewport, preserving size. Add genuinely long journeys in all four directions. Oversized boxes need an explicit fallback, not an unsupported exact-handoff claim.
2. **Reusable presenter colors:** `ensureScene` initializes colors only once. Refresh colors for later journeys and theme reloads, just as geometry reads the current appearance.
3. **Destruction order:** explicitly destroy/reset the presenter before destroying the workspace parent scene tree. Hiding it is not sufficient: its destructor still touches its scene node.
4. **Real visible viewport:** negative layout struts can expand `tiledArea` beyond the output. Intersect the tiled region with actual output/usable visibility before claiming the ring stays onscreen.
5. **Pure-translation premise:** do not apply the strip formula to arbitrary swaps/reflow/resizes just because they use the same motion owner. Cancel or gate/rebase based on verified translation invariants.
6. **Focus replay and origin:** capture origin before workspace remembered focus changes. Same-destination replay after launcher dismissal must not cancel/restart travel. Moving to another workspace must cancel the source's transient indicator too.

Concrete counterexample for the original edge clamp:

```text
viewport = {0,0,1000,700}
O = {28,50,340,500}
D0 = {30000,50,340,500}
D1 = {330,50,340,500}
lead = 0.6, p = 0.5
```

The unconstrained x is far beyond the viewport. Clamping both edges independently yields zero width and `nullopt`, whereas the desired fitting-box projection retains a visible ring at the forward edge. This is why passing finite-distance tests did not establish the core invariant.

## Authorized next slice

Correct and commit the groundwork, then wire request/bind/tick/cancel into the existing workspace and focus paths. Verify the actual rendered mid-flight indicator in an isolated headless compositor, including retargeting, cancellation, destination lifetime, and activation-under-layer followed by dismissal. Do not install a patched compositor or enable the new setting on the live desktop yet.

## Cross-references

- [Implementation checkpoint](/design/window-navigation/implementation0.glm53.md): author's choices, proposed ownership, and reported checks; this review identifies where evidence did not yet meet its claims.
- [Geometry code](/src/layout/focus_travel.cpp) and [geometry tests](/tests/unit/focus_travel.cpp): the exact projection and its coverage.
- [Presenter](/src/scene/focus_travel_ring.cpp): existing `umbrielfx` border mechanism; no window pointer or independent animation state.
- [Window navigation index](/design/window-navigation/README.md): configuration checkpoint, Vicinae evidence, and temporary visual comparison.
