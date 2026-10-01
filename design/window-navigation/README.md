# Window navigation inquiry

Goal: selecting a window in Vicinae should smoothly travel to it with a configurable ramp; a later visual focus ring should lead the movement while remaining mostly onscreen. Passing actual focus through intermediate windows is a tolerated shortcut, not the preferred experience. First scope is one scrolling workspace; eventual travel across vertically arranged workspaces remains desired.

## Findings and direction

- [Umbriel feasibility](umbriel0.sol61.md): existing foreign-activation reveal path, curve configuration, IPC limits, user clarification, and upstream scope constraints. Source-based, not a visual demonstration.
- [Vicinae investigation](vicinae0.glm53.md): built-in picker uses wlr foreign-toplevel activation; no selection interception hook is needed for a compositor-owned visual ring. External replacement options include `dmenu` and a custom picker.
- [Leading-focus first slice](focus-travel0.sol61.md): opt-in compositor-owned ring, shared geometry progress, viewport-constrained leading projection, lifecycle requirements, and verification gates.
- [Implementation groundwork](implementation0.glm53.md): geometry, opt-in configuration, and border presenter, with the runtime coordination design.
- [Parent review checkpoint](checkpoint0.sol61.md): approved ownership shape, long-distance visibility correction, lifetime and coordinate constraints, and the next integration slice.
- [Runtime review](review0.sol61.md): independent Standards and Spec findings plus parent analysis; records the correctness and rendered-verification gaps being corrected before a live trial.
- [Recovery evidence](implementation1.sol61.md): the runtime review fixes and split rendered checks; records a remaining destination-removal policy regression and distinguishes tested behavior from source-only claims.
- [Passing removal-policy checkpoint](implementation2.sol61.md): resolves that regression without changing normal replacement focus/reveal; all 11 focused travel checks, four neighboring close/layout checks, and the 62-test Meson suite pass.
- [Recovery verification](implementation1.sol61.md): landed corrections, split frame-based checks, 62/62 Meson result, and the unresolved automatic replacement-retarget behavior after destination removal (10/11 focused harness checks pass).

Vicinae's generic provider does not expose workspace metadata, but that does **not** prevent selecting a window on another workspace: activation identifies the window and Umbriel resolves its workspace. A separate workspace picker or richer labels would need additional integration. Keep exact compositor IDs for external orchestration; titles and app IDs are not unique window identities.

## Work boundaries

- **Configuration:** Compfuzor's `umbriel.user.pb` owns the user's include chain; smooth movement should be configured there without losing existing default or Vicinae bindings.
- **Selection integration:** independent investigation of Vicinae's backend and activation path; no new picker-specific protocol assumed necessary.
- **Leading presentation:** a scoped experimental patch, disabled by default, developed separately from configuration. A patched compositor is not installed automatically.

These documents are inquiry, not accepted upstream design. Accepted explanations should be synthesized into maintained documentation deliberately.

## Configuration checkpoint

Compfuzor change `nslkkpzz` adds `files/umbriel/animation.toml` to the existing user include chain and `umbriel.user.pb` deployment. It enables the master animation switch and `windows_move`, with `duration_ms = 650` and `curve = "0.42,0,0.58,1"`. Destination centering and keybind ordering are unchanged. The scoped user playbook has applied the fragment and root chain to `~/.config/umbriel/`; Umbriel's config watcher should reload it without a restart. Source and live rendered chain were reviewed by the parent; implementation agent reports all four integration tests, playbook syntax check, and rendered/live Umbriel validation passing. A human visual trial is still needed.

Known tradeoff: ordinary window moves/resizes/reflows use that same ramp. No leading-focus setting has been enabled in the live configuration, and no patched compositor has been installed.

## Throwaway visual sketch

`.test-agent/window-navigation/focus-travel.prototype.html` compares viewport-leading travel, a direct screen-space glide, and intermediate highlight hops. It is a dependency-free browser simulation, not compositor evidence. Long jumps in the leading model can park the ring at the forward edge before the destination arrives; the human question is whether that reads as useful anticipation or excessive edge-dwelling. The scratch README explains controls and cleanup. Preview could not be opened automatically because this session has no connected desktop browser; JavaScript syntax was checked, but the page has not been visually verified.

## Implementation recovery checkpoint

At this historical checkpoint, the initial runtime patch and screenshot evidence were committed, but [runtime review](review0.sol61.md) required further correctness and coverage fixes before a live trial. The implementation agent reached its provider usage limit during those revisions, after committing through `80f023a2` and drafting three replacement checks (`221_focus_travel_journey.sh`, `222_focus_travel_retarget.sh`, `223_focus_travel_seat.sh`). A replacement agent preserved the drafts before completing them and reconciling the review requirements against the actual committed code. Those drafts and the earlier passing check alone did not prove readiness. Smooth-scrolling configuration remained applied; the patched compositor remained uninstalled.

## Current trial readiness

The recovery and review fixes are now committed. Source `767cb305` distinguishes automatic focused-view removal from intentional navigation: replacement keyboard focus and scrolling remain normal, but the visual ring cancels instead of redirecting to an unrequested survivor. Parent reviewed the final source delta and the corrected ownership, common-pan/endpoint acceptance, focus-replay and bound-progress paths. [The final verification checkpoint](implementation2.sol61.md) reports all 15 targeted rendered checks (11 travel plus four neighbors) and all 62 Meson tests passing. Historical red evidence above is preserved deliberately and is no longer the current result.

The first ring slice is ready for an **opt-in human visual trial**, not declared accepted upstream. It remains disabled by default and no patched compositor is installed. In a patched build, enable:

```toml
[animation.focus_travel]
enabled = true
lead = 0.6
```

It borrows `windows_move` curve/duration and only starts for supported same-workspace scrolling pans. It is a supplementary ring, so the destination's normal focus accent may briefly coexist near handoff. Cross-workspace/output travel, oversized or edge-incompatible endpoints, and focus-only selections without a pan retain ordinary behavior. Gesture cancellation has source review rather than a synthetic runtime gesture check. Human judgment of pacing, edge-dwelling, and the duplicate-accent tradeoff is still outstanding; a nested compositor trial is the safe next step before replacing a live session.
