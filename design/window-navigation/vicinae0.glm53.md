---
type: Research
title: "Vicinae window enumeration & activation — facts and Umbriel integration constraints"
description: How Vicinae's find-window picker lists windows, how it activates a selection, which protocols/IPC are involved, and which surfaces can deliver the selected destination before activation.
resource: https://github.com/vicinaehq/vicinae
tags: [vicinae, umbriel, wayland, wlr-foreign-toplevel, window-navigation]
status: draft
generated: { by: agent:glm53, at: 2026-10-01 }
verified: { by: unverified, at: never }
stale_after: 2027-01-01
sources:
  - id: vicinae-local
    resource: /home/rektide/archive/vicinaehq/vicinae
    title: Vicinae checkout at commit 0827a2f8c (post v0.28.2)
    author: vicinaehq
    last_modified: 2026-09-30
---

# Vicinae window enumeration & activation (input to Umbriel window-navigation design)

## Intent & situation

The operator uses Vicinae (a QtQuick launcher, C++ business logic + QML UI + TypeScript
extension API) as their find-window picker on Umbriel (wlroots-based scrolling compositor).
They want picking a window in Vicinae to cause Umbriel to **smoothly scroll to the selected
window, with configurable ramp algorithms**. A later phase wants the **moving focus/highlight
to stay mostly onscreen, leading the movement** to the destination.

Refined direction for this round:

- A **traveling visual focus ring** is the preferred end state (visual, not necessarily real focus).
- **Actual focus stepping through intermediate windows is anti-desired but acceptable** as a
  direct shortcut (i.e. focus may walk origin→destination if that is the cheap path).
- **Vertically arranged multi-workspace travel** can be deferred initially, but is explicitly
  wanted eventually.
- Key question this report must answer: **does Vicinae expose an outside/extension mechanism
  that receives the exact selected destination BEFORE the default activation fires**, so an
  orchestrator could drive intermediate focus (or suppress default activation entirely)?

Parent session has independently verified the Umbriel side: wlr-foreign-toplevel activation
is handled (`FocusReason::ForeignActivation`) and configurable animated scrolling already
exists on that path. This report is bounded to Vicinae facts and integration constraints;
the sibling wave file in this directory was not read.

Evidence basis: local checkout at `/home/rektide/archive/vicinaehq/vicinae`, commit
`0827a2f8c` ("fix(xdg): stop reading a locale suffix at end of file (#1951)", just after
`v0.28.2`). GitHub URLs below point at `main` and should match that commit. Claims are
marked **[V]** (verified in source), **[H]** (hypothesis/inference), or **[U]**
(unverified — checked but inconclusive).

## 1. Architecture: one WM service, compiled providers, first-match detection

**[V]** Vicinae's server has a `WindowManager` service that instantiates exactly one
`AbstractWindowManager` provider at startup:

- Provider candidate order on Linux (first `isActivatable()` wins):
  Hyprland → GNOME → KDE → X11 → Niri → generic Wayland; fallback is a `DummyWindowManager`
  ([window-manager.cpp L25-60](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/window-manager.cpp#L25-L60)).
- The provider list is **hardcoded C++** — there is no plugin/D-Bus/config mechanism to
  register a new window-manager provider **[V]** (the comment at L26 even says "For all new
  window managers, it is needed to add it to this vector").

**[V]** `AbstractWindowManager` is the polymorphic surface every consumer calls
([abstract-window-manager.hpp](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/abstract-window-manager.hpp)):
`listWindowsSync()` (L136), `getFocusedWindowSync()` (L164), `focusWindowSync(window)` (L193),
`closeWindow` (L240), capabilities flags (L33-39), `isActivatable()` (L285), `start()` (L291).
Windows expose `id()`, `title()`, `wmClass()`, optional `workspace()`/`bounds()`/`pid()`.

### Detection per provider **[V]**

| Provider | `isActivatable()` | Enumeration | Activation |
|---|---|---|---|
| Hyprland | env `HYPRLAND_INSTANCE_SIGNATURE` ([hyprland.cpp L112-114](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/hyprland/hyprland.cpp#L112-L114)) | `hyprctl -j/clients` oneshot over `$XDG_RUNTIME_DIR/hypr/<his>/.socket.sock` ([hyprland.cpp L36-48](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/hyprland/hyprland.cpp#L36-L48), [hyprctl.cpp L13-17](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/hyprland/hyprctl.cpp#L13-L17)); events via `.socket2.sock` | `dispatch hl.dsp.focus({ window = "address:<id>" })` (L75-77) |
| GNOME | GNOME env + D-Bus `org.gnome.Shell` @ `/org/gnome/Shell/Extensions/Windows` answering `List` — i.e. **requires the companion Vicinae GNOME Shell extension** ([gnome-window-manager.hpp L23-25](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/gnome/gnome-window-manager.hpp#L23-L25), [gnome-window-manager.cpp L258-274](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/gnome/gnome-window-manager.cpp#L258-L274)) | D-Bus `List` (JSON) | D-Bus `Activate(windowId)` (L229-256) |
| KDE | `Environment::isWaylandPlasmaDesktop()` ([kde-window-manager.hpp L79](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/kde/kde-window-manager.hpp#L79)) | KWin scripting tracker snapshot | one-shot KWin JS script `vicinae-focus-<uuid>` via D-Bus `org.kde.kwin.Scripting` ([kde-window-manager.cpp L258-265](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/kde/kde-window-manager.cpp#L258-L265)) |
| X11 | `Environment::isX11()` | xcb client list | EWMH `_NET_ACTIVE_WINDOW` |
| Niri | Wayland session **and** `NIRI_SOCKET` set and socket file exists ([niri.cpp L364-369](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/niri/niri.cpp#L364-L369)) | niri IPC JSON: `"Windows"` / persistent `"EventStream"` subscription (L378-410) | request `{"FocusWindow":{"id":<handle>}}` over the socket (L340-350) |
| **Wayland (generic)** | Wayland session and **not** COSMIC ([wayland.cpp L89-92](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/wayland/wayland.cpp#L89-L92)) | **wlr-foreign-toplevel-management v3** (below) | `zwlr_foreign_toplevel_handle_v1.activate(seat)` |

The Niri provider is the in-tree precedent for "compositor-specific IPC provider": env-var
socket discovery, JSON event stream, MRU ordering maintained via `WindowFocusTimestampChanged`
events and `sortWindowsByFocusTimestamp()` ([niri.cpp L618-632](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/niri/niri.cpp#L618-L632)).
An `umbriel` provider would follow this shape if ever contributed upstream.

## 2. The generic Wayland provider — exactly what Umbriel must satisfy today

**[V]** This is the provider Umbriel hits (Umbriel is not Hyprland/GNOME/KDE/X11/Niri, and is
not COSMIC). All facts from
[wayland.cpp](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/wayland/wayland.cpp)
and [wayland.hpp](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/wayland/wayland.hpp):

- Binds `zwlr_foreign_toplevel_manager_v1` **version 3** via
  `QWaylandClientExtensionTemplate` ([wayland.cpp L11-16](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/wayland/wayland.cpp#L11-L16));
  the protocol XML is vendored in-tree at
  [src/wayland-protocols/wlr-foreign-toplevel-management-unstable-v1.xml](https://github.com/vicinaehq/vicinae/blob/main/src/wayland-protocols/wlr-foreign-toplevel-management-unstable-v1.xml).
- **Enumeration** is event-driven: each `toplevel` event constructs a `WaylandWindow`
  (L18-21); `title`/`app_id` update fields, with `app_id` also used to mint the window `id()`
  — literally the pointer address as a string (L36-41); `state` arrays are scanned for
  `state_activated` to track focus, emitting `focusChanged` on rising edge (L43-50); `done`
  emits `windowsChanged` (L52); `closed` erases the window and re-emits (L54-59). `start()`
  grabs `wl_seat` from Qt's native interface, instantiates the manager, and does one
  `wl_display_roundtrip` to pull the initial window set (L98-113). `ping()` = manager bound
  and not finished (L94-96).
- **Activation**: `focusWindowSync` finds the window among live toplevels and calls the
  generated `ww->activate(m_seat)` → `zwlr_foreign_toplevel_handle_v1_activate(handle, seat)`
  ([wayland.cpp L73-78](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/wayland/wayland.cpp#L73-L78)).
  Protocol semantics: "There is no guarantee the toplevel will be actually activated"
  (XML L139-145). Fire-and-forget: no roundtrip, no completion callback, no parameters beyond
  the seat — **there is no place in this protocol to express a ramp/easing/duration** **[V]**.
- `closeWindow` maps to the handle's `close` request (L80-87). The provider reports
  `supportsFocusTracking() == true` and `supportsFocusHandoffDetection() == true`
  ([wayland.hpp L66-67](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/wayland/wayland.hpp#L66-L67)).
- **Workspaces: none.** `WaylandWindow::workspace()` returns `nullopt` and the provider does
  not implement `hasWorkspaces()`/`listWorkspaces()` — under wlr-foreign-toplevel, Vicinae
  cannot see or select Umbriel workspaces at all **[V]**. Consequences: the built-in
  "Switch Workspaces" command is not registered on Umbriel
  ([wm-extension.cpp L120](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/builtins/wm/wm-extension.cpp#L120)),
  and window rows show no workspace accessory
  ([switch-windows-model.cpp L17-21](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/builtins/wm/switch-windows-model.cpp#L17-L21)).
  Selecting a **window that lives on another workspace already works** through the same
  `activate` request — Umbriel resolves the target workspace itself, so cross-workspace
  *window* selection needs nothing new **[V]**. What the protocol gap costs is workspace
  *metadata*: workspace rows/chips in the picker, a workspace picker, workspace-scoped
  filtering — those would need either a compositor-specific provider (Niri-style) or
  Umbriel-side orchestration (see §7 Q2) **[V/H]**.
- **`ext-foreign-toplevel-list` is NOT consumed anywhere in Vicinae** **[V]** (repo-wide
  grep finds only the wlr protocol). Umbriel's ext-foreign handles are invisible to Vicinae;
  only the wlr protocol matters. (The ext protocol is enumerate-only with no activate request,
  so this is not a loss for this use case.)
- **List ordering**: `listWindowsSync()` returns `m_toplevels` in **compositor arrival order**
  (creation order), and neither the provider nor the built-in picker re-sorts
  ([wayland.cpp L64](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/window-manager/wayland/wayland.cpp#L64);
  `SwitchWindowsSection` does not override `sortFiltered()`). So under Umbriel the picker shows
  windows in map order, **not MRU** — unlike Niri (focus-timestamp sort) **[V]**. Worth noting
  as a picker-UX regression risk on Umbriel; fixing it needs either an Umbriel provider or a
  non-built-in picker (§5) **[H]**.
- Window identity: `id()` is a pointer-to-QObject string, stable only for the provider's
  lifetime of that handle, and the exact handle lives inside Vicinae. External parties should
  **not** correlate windows by `app_id`/title — unsafe and non-unique. An Umbriel-owned picker
  (§5a) doesn't need Vicinae's identity at all: it generates the candidate list from
  Umbriel's own window registry (native/ext IDs via its IPC) and maps the selection back
  through that snapshot **[V]**.

## 3. The built-in find-window picker ("Switch Windows")

**[V]** Flow when the user opens and confirms:

1. Command id `switch-windows`, registered by the `wm` builtin extension
   ([wm-extension.cpp L86-94, L118](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/builtins/wm/wm-extension.cpp#L86-L94)).
   Fuzzy-searchable: title weight 1.0, app display name 0.5, wmClass 0.3
   ([switch-windows-model.hpp L13-23](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/builtins/wm/switch-windows-model.hpp#L13-L23)).
2. View host refreshes once on open (`provider()->refresh()`, a no-op for event-driven
   providers) and on every `windowsChanged` ([switch-windows-view-host.cpp L14-17, L23-48](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/builtins/wm/switch-windows-view-host.cpp#L14-L48));
   rows join against the app database for icons/subtitles.
3. Selecting a row (Enter) executes the **primary action** of the row's action panel — the
   first action unless another is explicitly primary
   ([action-panel-state.hpp L54-57, L89-109](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ui/action-panel/action-panel-state.hpp#L54-L109);
   [navigation-controller.cpp L369-381](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/navigation-controller.cpp#L369-L381)).
   The panel is built in `SwitchWindowsSection::buildActionPanel` with
   **`FocusWindowAction` added first** (hence primary), then optional Pin / Bring-to-workspace
   (capability-gated, absent on Umbriel), then Close (Ctrl+Q)
   ([switch-windows-model.cpp L23-36](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/builtins/wm/switch-windows-model.cpp#L23-L36)).
4. `FocusWindowAction::execute` calls `provider()->focusWindowSync(*m_window)`; the action is
   flagged autoClose at **construction** (`setAutoClose()` sits in the constructor, not
   `execute`), so confirming the row makes the launcher dismiss itself via the dispatcher
   after the action executes ([window-actions.hpp L11-22](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/actions/window-actions.hpp#L11-L22);
   [action-panel-model.cpp L178-200](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ui/action-panel/action-panel-model.cpp#L178-L200)).

**[V] There is no interception hook anywhere in this path.** No signal like
"windowAboutToBeFocused", no way for an extension or external client to observe or veto the
built-in picker's selection. The action object is constructed inline in C++ with the window
pointer captured. The only sanctioned influences are the provider implementation itself.

**[V]** Users *can* disable built-in commands (root search "disable" action persists via
`RootItemManager::disableItem` → config, and disabled items are filtered from search)
([root-item-manager.cpp L547-549, L176](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/services/root-item-manager/root-item-manager.cpp#L547-L549)).
So a replacement picker can take over the niche without patching Vicinae — though the built-in
would then be gone rather than redirected **[V/H]**.

## 4. External control surfaces (no patching required)

**[V]** The server listens on a local socket `$XDG_RUNTIME_DIR/vicinae.sock`
([common.cpp L122, L140](https://github.com/vicinaehq/vicinae/blob/main/src/lib/common/src/common.cpp#L122-L140)),
speaking a figura-generated length-prefixed RPC
([ipc-command-server.hpp](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ipc/ipc-command-server.hpp)).
Surface relevant here:

- `launchCommand(entrypoint, args…)` and `deeplink(url)` — can open any command, including
  built-ins: `vicinae://launch/<provider>:<entrypoint>` with JSON `arguments`
  ([ipc-command-handler.cpp L117-183](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ipc/ipc-command-handler.cpp#L117-L183));
  entrypoint IDs serialize as `provider:entrypoint`
  ([entrypoint.hpp L14-21](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/common/entrypoint.hpp#L14-L21)),
  so the picker is openable externally as a deeplink to `switch-windows` **[V]** (exact builtin
  provider string not verified — `core/...` appears in shipped deeplinks, e.g. `vicinae://launch/core/store`).
- `launchApp(appId)` — notably **focuses an existing window of that app instead of spawning**
  when found ([ipc-command-server.cpp L196-210](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ipc/ipc-command-server.cpp#L196-L210)):
  an app-level (not window-level) activation entry point.
- **`dmenu`** — the interesting one, see §5.
- CLI wrappers for all of the above: `vicinae toggle|open|close|command <entrypoint>|deeplink|dmenu`
  ([cli.cpp L292-421](https://github.com/vicinaehq/vicinae/blob/main/src/cli/src/cli.cpp#L292-L421)).

**[V]** The TypeScript extension API (`@vicinae/api`) exposes window management to
extensions as *consumers*: `WindowManagement.getWindows(workspaceId?)`, `getActiveWindow()`,
`focusWindow(window)` / `window.focus()`, `getScreens()`, `getWorkspaces()`
([window-management.ts L147-221](https://github.com/vicinaehq/vicinae/blob/main/src/typescript/api/src/api/window-management.ts#L147-L221)),
served by `ExtWindowManagementService` which resolves ids against the live provider cache and
calls `provider()->focusWindowSync` ([wm-service.hpp L16-22, L58-77](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/extension/api/wm-service.hpp#L16-L77)).
Extensions render arbitrary React views (`List` with `onSelectionChange`, items, actions)
([components/list.tsx](https://github.com/vicinaehq/vicinae/blob/main/src/typescript/api/src/api/components/list.tsx)).
The in-tree TS API exposes **no exec/process surface** (repo grep; only `runInTerminal` and
`open`, both user-visible launches). Whether the extension runtime actually *blocks* Node
built-ins such as `child_process`, or outbound network (which could reach a compositor-side
listener), was **not verified** — the runtime is a downloaded Node archive and no sandbox
allowlist was found in-tree, so external orchestration from an extension is
**unsupported/unverified, not established impossible**. The only *documented* focus primitive
is `focusWindow` (= wlr activate) **[V/U]**.

## 5. Answer to the key question: destination-before-activation mechanisms

Three verified mechanisms exist; none of them hook the built-in picker.

### 5a. `dmenu` IPC / CLI — selection returned to caller, zero side effects **[V]**

`vicinae dmenu` renders Vicinae's fuzzy picker from stdin lines and prints the selection to
stdout; nothing is ever activated
([cli.cpp L344-394](https://github.com/vicinaehq/vicinae/blob/main/src/cli/src/cli.cpp#L344-L394)):
`--format index` prints the **original stdin index** (stable across fuzzy reordering — the
model keeps `(entry, idx)` pairs, [dmenu-model.cpp L14-34, L94-108](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ui/views/dmenu-model.cpp#L14-L34)),
`--format data` prints the line; Esc/cancel yields empty output. Server side, the request is
`DMenuRequest{rawContent, format, placeholder, query, width, height, …}`
([figura/ipc.fig L58-76, L122](https://github.com/vicinaehq/vicinae/blob/main/figura/ipc.fig#L58-L76))
and the reply is delivered through a promise when `DMenuViewHost::selected` fires
([ipc-command-server.cpp L269-309](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ipc/ipc-command-server.cpp#L269-L309)).

For Umbriel this means: an Umbriel-owned component could feed its own window list
(`app_id — title — workspace`, one per line), get back the chosen index **before any focus
change**, and then fully orchestrate compositor-side — traveling ring, optional intermediate
hops, ramp — using internal state, with no Wayland round trip needed for identity. Constraints:
plain-text rows only (icons only when an entry parses as an existing file path,
[dmenu-model.cpp L72-79](https://github.com/vicinaehq/vicinae/blob/main/src/server/src/ui/views/dmenu-model.cpp#L72-L79)),
no live window-add/remove events during the picker's lifetime, synchronous request/response
(one dmenu at a time), and index→window mapping is entirely the caller's responsibility.
The mapping itself is stable: the snapshot backing one dmenu request is immutable for that
request's lifetime, so the returned index is a valid lookup key even if a window closes
mid-picker — look up the compositor-side ID from that snapshot, then **revalidate the stored
compositor ID at selection time** (the window may be gone or moved); do not rebuild/reorder
the candidate list before performing the lookup. Embedding a token in the row text is
optional, not required **[V/H]**.

### 5b. Custom Vicinae extension command — rich picker, orchestration via sequential activates **[V]**

A TS extension can register its own window-switcher command: `getWindows()` to populate a
`List`, `onSelectionChange`/item actions to run arbitrary code on selection, then choose what
happens — e.g. a single `focusWindow(destination)`, or a timed sequence of
`focusWindow(intermediate_i)` calls (each maps to one `activate(seat)` **[V]**), or nothing
and just report. Constraints: the in-tree API has no exec surface and the runtime's actual
restrictions are unverified (§4), so "orchestrate" means *via Vicinae's documented focus
primitives* — every step is a real focus change seen by Umbriel
(exactly the "acceptable as direct shortcut" intermediate-focus path), and timing/ramps still
must be implemented Umbriel-side **[V/H]**. Upside over dmenu: app icons, subtitles, fuzzy
weights, workspace chips (once Umbriel exposes workspaces through some provider), and the
built-in can be disabled so this takes over.

### 5c. Nothing hooks the built-in picker **[V]**

The built-in `switch-windows` offers no pre-activation event, no configurable default action,
no veto. Its Enter path is hardcoded C++ `focusWindowSync` + autoClose (§3). If the built-in
picker stays the entry point, the earliest Umbriel can learn the destination is the
`activate(seat)` request itself.

## 6. Integration options (facts + constraints only)

- **Option A — keep the built-in picker; Umbriel-side animation only.** Zero Vicinae changes.
  Umbriel receives one `activate`; it knows origin (focused view) and destination, which is
  sufficient for a traveling ring that leads the scroll without any intermediate focus
  (parent has verified the ForeignActivation → animated-scroll path exists). Known picker
  limitations under this option: creation-order list (no MRU), no workspace rows, no
  workspace commands, no pin/bring-to-workspace actions (capability set is empty for the
  generic provider) **[V]**.
- **Option B — Umbriel-driven picker via `vicinae dmenu --format index`.** Destination known
  pre-activation (§5a); Umbriel free to implement ring, hops, ramps, and (later) cross-
  workspace travel entirely internally; window list and ordering (e.g. MRU) controlled by
  Umbriel. Costs: text-only rows, revalidate the mapped compositor ID at selection (§5a),
  one-shot request lifecycle.
  This is the only mechanism where *default activation never happens* unless Umbriel wills it
  **[V]**.
- **Option C — replacement extension picker.** Best UX inside Vicinae (icons, fuzzy over
  title/app/class); orchestration limited to sequential real focuses (§5b); no documented
  channel to Umbriel IPC (extension runtime restrictions unverified, §4); still needs a way
  to learn workspace metadata if the vertical phase lands (none exists via
  wlr-foreign-toplevel) **[V/U/H]**.
- **Protocol ceiling:** `zwlr_foreign_toplevel_handle_v1.activate` carries only a seat; no
  ramp/duration/intent parameters exist in any Vicinae-relevant protocol, so **ramp
  algorithms and ring pacing must be configured and executed inside Umbriel** in every option
  **[V]**. Intermediate focus, if used, arrives as N separate activate requests whose pacing
  is controlled by the sender (extension loop or Umbriel itself) — Umbriel must decide
  per-request whether to animate fully, snap, or chain **[H]**.
- **Sequencing note [V/H]:** after Enter, Vicinae sends the `activate` request and dismisses
  itself (autoClose via the dispatcher). The activate and the layer-shell hide are **separate
  Wayland requests**, so their arrival/ordering at the compositor is not guaranteed — Umbriel
  must not assume the launcher surface is already gone when its scroll/ring animation starts.
  Either way the ring must be an Umbriel overlay/focus-chrome effect, not something rendered
  by Vicinae. Also `state_activated` events streaming back from Umbriel drive Vicinae's
  `focusChanged`; rapid intermediate focuses would ripple into Vicinae's focus memory
  (harmless today — only paste/snippet services consume it — but worth remembering).

## 7. Open questions

1. Should Umbriel grow a first-class Vicinae provider (Niri-pattern: socket env var + JSON
   event stream + `FocusWindow`-style request)? That would unlock workspaces, MRU ordering,
   fullscreen/floating toggles in the built-in UI — but is an upstream-C++ (or fork/patch)
   path, not configuration. Upstream appetite unknown **[H]**.
2. For the deferred vertical-workspace phase: does selection need to target *workspaces*
   (Vicinae has a `switch-workspaces` builtin, provider-gated) or only windows that happen to
   live on other workspaces? The latter works today via activate; the former needs Option B/C
   or a provider **[V/H]**.
3. Is MRU ordering in the picker a requirement? If yes, Option B (Umbriel controls order) or a
   provider is needed; the built-in under generic Wayland is creation-ordered **[V]**.
4. dmenu identity handling: the per-request snapshot makes the returned index a stable lookup
   key; the follow-up obligation is revalidating the mapped compositor-side window ID at
   selection time (window may have closed or moved meanwhile), not embedding extra tokens
   **[V/H]**.

## Cross-references

- Sibling wave doc `umbriel0.sol61.md` in this directory (not read; independent wave).
- Umbriel-side verified facts referenced above live in
  [`/src/view/view_foreign.cpp`](/src/view/view_foreign.cpp) (wlr + ext foreign toplevel
  handling, `handleForeignActivate` → `focusView(ForeignActivation)`),
  [`/src/server/focus.cpp`](/src/server/focus.cpp) (focus → `group->activate(workspace)`),
  [`/src/server/ipc_commands.h`](/src/server/ipc_commands.h) (`windows` list, `msg` action
  dispatch), [`/src/server/ipc.cpp`](/src/server/ipc.cpp) (socket path).

## Source index (canonical)

All Vicinae references: [`vicinaehq/vicinae` @ main](https://github.com/vicinaehq/vicinae),
verified against local commit `0827a2f8c`:

- Provider chain & fallback — `src/server/src/services/window-manager/window-manager.cpp` L25-60
- WM abstraction — `src/server/src/services/window-manager/abstract-window-manager.hpp`
- Generic Wayland provider — `src/server/src/services/window-manager/wayland/wayland.cpp`, `.hpp`
- wlr protocol XML — `src/wayland-protocols/wlr-foreign-toplevel-management-unstable-v1.xml` (activate L139-145, state enum L153-156)
- Niri provider (IPC precedent) — `src/server/src/services/window-manager/niri/niri.cpp`
- Hyprland/GNOME/KDE/X11 providers — same tree, `hyprland/`, `gnome/`, `kde/`, `x11/`
- Built-in picker — `src/server/src/builtins/wm/{wm-extension.cpp, switch-windows-view-host.cpp, switch-windows-model.cpp/.hpp}`
- Focus action — `src/server/src/actions/window-actions.hpp` L8-23
- Primary-action machinery — `src/server/src/ui/action-panel/action-panel-state.hpp` L54-109; `src/server/src/navigation-controller.cpp` L369-381
- External IPC — `src/server/src/ipc/ipc-command-server.{hpp,cpp}`, `ipc-command-handler.cpp`, `figura/ipc.fig`; socket path `src/lib/common/src/common.cpp` L122-140
- CLI — `src/cli/src/cli.cpp` L292-421 (toggle/open/command/dmenu/deeplink)
- TS window API — `src/typescript/api/src/api/window-management.ts`; server side `src/server/src/extension/api/wm-service.hpp`
- Builtin disabling — `src/server/src/services/root-item-manager/root-item-manager.cpp` L547-549, L176
