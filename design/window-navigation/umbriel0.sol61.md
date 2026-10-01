# Animated window navigation: Umbriel feasibility

## Situation and scope

The user selects windows through Vicinae's window picker and wants smooth travel to the selected window, with configurable ramp algorithms. A later version should animate the visual focus so it remains onscreen or mostly onscreen and leads the movement toward the destination. External integration is attractive, compositor patches are acceptable, and upstreaming is a possible eventual destination. This is investigation, not authorization to implement.

This report examines the local Umbriel checkout at baseline `51e6528e`. It is source-based evidence, not a live demonstration. Vicinae's implementation is investigated independently in the companion report; no Vicinae compatibility is assumed here.

## Main finding: the first version is largely already present

Umbriel handles foreign-toplevel activation by focusing the selected view with `FocusReason::ForeignActivation`. For a tiled scrolling window, that focus chooses the destination scroll offset and schedules animated layout movement. The existing shared geometry animation then interpolates all participating windows with one timeline. You do not need to synthesize mouse-wheel events to get there.

Verified path:

1. Umbriel advertises both wlr foreign-toplevel management and ext foreign-toplevel listing ([`server.cpp`, lines 497–499](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/server/server.cpp#L497-L499)).
2. A wlr foreign-toplevel activate request reaches `View::handleForeignActivate`, then `focusView(this, ForeignActivation)` ([`view_foreign.cpp`, lines 131–146](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/view/view_foreign.cpp#L131-L146)).
3. Focus activates an inactive destination workspace if necessary, applies seat focus when available, remembers the selected view, then calls `activateFocusedColumn()` and `markArrange(true)` ([`focus.cpp`, lines 64–143](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/server/focus.cpp#L64-L143)).
4. Scrolling selects the reveal/centering destination; its logical `m_scroll` changes immediately ([`scrolling.cpp`, lines 643–674 and 706–720](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/layout/scrolling.cpp#L643-L720)).
5. Arrange computes target geometry and calls `applyPositions(animate, resized)` ([`workspace.cpp`, lines 634–697](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/workspace/workspace.cpp#L634-L697)).
6. Layout motion uses `animation.windowsMove`, captures current and target boxes, and advances their shared interpolation on compositor animation ticks ([`workspace.cpp`, lines 898–1040](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/workspace/workspace.cpp#L898-L1040)).

Important distinction: the logical scroll jumps to its destination; the presented geometry travels smoothly. This is not an externally controllable camera-position animation. For an ordinary strip translation, shared box interpolation nevertheless produces the desired visible scrolling.

## Configuration-only experiment

Merge these sections into the user's existing configuration, rather than appending duplicate TOML tables:

```toml
[layout.scrolling]
center_focused = "always"

[animation.windows_move]
enabled = true
duration_ms = 650
curve = "0.42,0,0.58,1"
```

The master `[animation] enabled` must also be true. The Bézier gives a deliberate acceleration/deceleration ramp; 650 ms is an illustrative starting value, not a recommendation proven by testing. `center_focused = "always"` centers the destination; omit it or use `"never"` for minimum movement sufficient to reveal it.

Alternative: `curve = "spring:1,500"` uses a critically damped spring; its stiffness determines its length, so `duration_ms` does not control that case. Named custom Béziers and springs also exist. See [animation configuration](/docs/user/animation.md#curves) and [scrolling settings](/docs/user/layout.md#settings).

Tradeoffs already visible in the source:

- These settings also affect moves, resizes, and established-window reflow. There is no separate ordinary navigation event in the documented configuration.
- Fixed-duration curves do not impose a fixed maximum travel speed. A distant destination traverses more pixels in the same duration.
- Overshooting/reversing curves are projected onto monotonic cumulative travel for tiled geometry. Choosing a bouncy spring does not imply the scrolling tiles literally overshoot ([animation geometry contract](/docs/user/animation.md#event-tables), [`MonotonicEasing`](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/core/animation.h#L92-L109)).
- The normal focus path generally grants focus before the visual journey finishes. The focused target can initially be offscreen. An exclusive layer-shell keyboard surface can temporarily withhold actual seat focus.

## Scriptability and external integration

Umbriel has line-delimited JSON IPC over a UNIX socket, CLI actions, and snapshot subscriptions. Scripts can enumerate windows and select an exact window:

```sh
umbriel windows --json
umbriel msg window-focus:<id>
umbriel subscribe windows,workspaces
```

Use an actual returned ID in place of `<id>`. IPC IDs are the ext foreign-toplevel identifiers, not titles or app IDs. `window-focus` deliberately uses the same `ForeignActivation` focus reason, and also has explicit hidden-scratchpad summoning behavior ([`ipc_commands.cpp`, lines 341–381](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/server/ipc_commands.cpp#L341-L381), [`actions.cpp`, lines 1064–1095](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/server/actions.cpp#L1064-L1095)).

Existing directional `layout-scroll-*` actions are not a documented API for setting an absolute scroll offset, injecting a curve per selection, or subscribing to navigation progress. Neither the documented IPC nor these action surfaces provide an embedded script/plugin engine. External programs can invoke existing compositor behavior; they cannot arbitrarily drive its internal animation state.

An external overlay has another important limitation: window query `x/y` values are tiled **layout targets**, not the current animated presentation. Window subscriptions are state snapshots, not an animation-frame feed. Reconstructing a perfectly aligned leading ring from these events would be speculative and brittle ([IPC geometry contract](/docs/user/ipc.md), [`ipc_commands.cpp`, lines 365–380](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/server/ipc_commands.cpp#L365-L380)).

## What “leading focus” could mean

The user's wording leaves a meaningful design choice open. Do not silently choose among these:

| Interpretation | What moves | Consequence |
| --- | --- | --- |
| Traveling visual focus ring | A compositor-owned indicator glides toward the destination, constrained to the visible viewport | Keeps keyboard-focus semantics independent of the effect; likely the cleanest first exploration |
| Focus passing through intermediate windows | Actual activation/keyboard focus changes as windows pass through a lead region | Causes client focus notifications, MRU changes, possible accidental input; requires an explicit policy |
| Destination window kept visible during travel | A temporary proxy/thumbnail or detached presentation of the selected window leads the strip | The real destination starts offscreen; keeping it visible requires an additional representation or altered geometry, not just easing |

A target several viewports away cannot both retain its ordinary strip position and stay visible throughout a continuous pan. A moving ring, intermediate selection, and a target proxy solve different problems.

Existing `animation.border` animates each window's border **color**, not a ring traveling between windows. Custom GLSL shaders operate on bounded rendering targets and do not change layout, input coordinates, or focus ([border event](/docs/user/animation.md#event-tables), [shader composition limits](/docs/user/animation.md#targets-and-composition)). They are not a general navigation scripting escape hatch.

## Suggested upstream-friendly boundary, not an implementation commitment

Keep selection outside and animation policy inside the compositor:

```text
picker selects an exact window
  → existing activation / window-focus request
  → compositor resolves workspace and strip destination
  → compositor owns travel timeline and presentation
  → optional compositor-owned leading indicator
```

No new Wayland protocol is necessary merely to make an existing activation look better. The first patch worth considering, if configuration is insufficient, is a navigation-specific motion policy that reuses the current curve machinery without slowing unrelated resizes. A later indicator should consume the actual shared presented-geometry progress, not try to infer it over IPC.

Prefer a policy at the reveal/layout-motion boundary rather than intercepting only the Vicinae-specific entry point. Keyboard navigation and other pickers should be able to benefit too, while hover and interactive gestures may need different behavior. The existing `FocusReason` distinction is useful but is not by itself a complete motion context once an arrange is coalesced with other changes.

Preserve interruption from current presentation; decide whether velocity continuity is needed. Existing layout retargets restart their scalar timeline from current boxes, while `AnimatedValue::settleSpring` supports velocity-carrying spring motion ([`workspace.cpp`, lines 966–1013](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/workspace/workspace.cpp#L966-L1013), [`animation.h`, lines 158–180](https://github.com/noctalia-dev/umbriel/blob/51e6528e/src/core/animation.h#L158-L180)). These are different behaviors, not interchangeable claims of smoothness.

Cross-workspace navigation adds a separate workspace slide; cross-output navigation has no single physical strip connecting displays. Floating, fullscreen, pinned, and scratchpad windows need explicit behavior rather than forced strip travel. Cancellation must cover another selection, manual scrolling, destination close/move, output loss, and session lock.

## Next evidence to gather

1. Confirm Vicinae's selected backend actually takes the wlr activation path on Umbriel.
2. Try the configuration-only experiment with distant windows on one scrolling workspace, and observe picker dismissal/focus timing and cursor behavior.
3. Choose which leading-focus interpretation is intended before drafting a patch.
4. If distinct navigation settings or distance-sensitive speed are needed, design that coherent compositor slice before adding an indicator.

Future checks should measure mid-flight presentation, spacing, indicator visibility, input-focus policy, and retargeting—not only final target geometry. The existing [layout-animation harness](/tests/harness/checks/170_layout_animation.sh) explicitly says it cannot watch an animation mid-flight, so passing it would not establish the desired experience.

## Cross-references

- [User IPC guide](/docs/user/ipc.md): exact external control and geometry-reporting contracts; explains why an outside frame-driven controller is not currently supported.
- [User animation guide](/docs/user/animation.md): configurable ramps already exist, including the monotonic projection caveat and shader limits.
- [Overview rendering](/docs/design/overview-rendering.md#animation-ownership): prior art for separating selection and keyboard delivery while coordinating a reveal with another visual timeline. It does not establish that ordinary navigation already has a leading indicator.
- [Border rendering](/docs/design/border-rendering.md#scene-lifecycle): existing decoration primitive and focus-color lifecycle; useful implementation material for a future standalone indicator, not proof one already exists.
- [Scrolling layout](/docs/user/layout.md#scrolling-layout): destination framing and the relation between strip axis and workspace axis.

No compositor code or user configuration has been changed, and no visual/runtime verification has been performed in this investigation.

# Addendum: user direction after initial investigation

The user prefers a **traveling visual focus ring**. Actual focus passing through intermediate windows is somewhat anti-desired, but acceptable as a direct implementation shortcut: move focus along the path from origin to destination. Do not reinterpret that tolerance as a requirement to activate intermediate apps, or reject it outright if a cheap experiment would clarify the visual effect.

Multiple vertically arranged workspaces can be left out of the first exploration, but eventual support is explicitly desired. Initial scope can be a single scrolling workspace; preserve the distinction between strip travel and workspace-axis travel so the latter can be added deliberately. No implementation has been requested by this clarification.
