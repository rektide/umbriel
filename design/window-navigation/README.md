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
