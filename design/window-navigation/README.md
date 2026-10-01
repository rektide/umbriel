# Window navigation inquiry

Goal: selecting a window in Vicinae should smoothly travel to it with a configurable ramp; a later visual focus ring should lead the movement while remaining mostly onscreen. Passing actual focus through intermediate windows is a tolerated shortcut, not the preferred experience. First scope is one scrolling workspace; eventual travel across vertically arranged workspaces remains desired.

## Findings and direction

- [Umbriel feasibility](umbriel0.sol61.md): existing foreign-activation reveal path, curve configuration, IPC limits, user clarification, and upstream scope constraints. Source-based, not a visual demonstration.
- [Vicinae investigation](vicinae0.glm53.md): built-in picker uses wlr foreign-toplevel activation; no selection interception hook is needed for a compositor-owned visual ring. External replacement options include `dmenu` and a custom picker.
- [Leading-focus first slice](focus-travel0.sol61.md): opt-in compositor-owned ring, shared geometry progress, viewport-constrained leading projection, lifecycle requirements, and verification gates.

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
