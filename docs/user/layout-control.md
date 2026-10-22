---
type: Reference
title: Conditional scrolling allocation IPC
description: Coherent layout observation and explicit-target whole-column allocation for external controllers.
resource: docs/user/layout-control.md
status: implemented
generated: { at: 2026-10-22, by: 'model:openai/gpt-6-astra#medium' }
sources:
  - resource: opencode://ses_e9a70297dffe6dQ5UJP0up8m7T
    title: Implement native allocation and Rust bindings, then carry narrowly onto the preserved composed base with committed-size stream verification
  - resource: ../../src/server/ipc_layout.cpp
    title: Native snapshot, complete-basis check, preview and atomic assignment
  - resource: ../../src/layout/scrolling.cpp
    title: Existing native layout math and validated whole-column weight assignment
  - resource: ../../tests/harness/checks/layout/conditional_allocation.sh
    title: Isolated compositor behavioral contract
---

# Conditional scrolling allocations

This composed deployment candidate is based on `1845885e`, which predates
server-side title bars. It reports `title_bar_height: 0`, with identical target
tile/content boxes. The layout-state wire contract is otherwise the same as the
upstream-based control implementation. Only this event family was added; newer
upstream output/layer event families were not imported. Committed-size changes
schedule snapshots after client redraw, independently of target arrangement.

`{"cmd":"layout-state"}` (CLI: `umbriel layout-state --json`) returns the
version-1 layout snapshot. Subscribe with
`{"cmd":"subscribe","events":["layout-state"]}` for an initial snapshot and
subsequent complete replacements. Unknown command/event rejection on an older
compositor means this extension is unavailable. Other event families remain
independent; this family itself contains one coherent native allocation view.

The successful query/event data has `version`, process-scoped random `instance`,
seat-global `active` window ID or null, `columns`, and `unsupported_workspaces`.
Only scrolling groups are described; an unsupported layout workspace is listed
explicitly. Empty scrolling workspaces have no columns.

Each column has:

- `basis`: the complete expected-state object for a later write;
- `geometry`: member-order objects with ID, `target_tile`, `target_content` and
  `committed_content` boxes (`x`, `y`, `width`, `height`, logical pixels);
- `cross_span`: secondary allocation span after inter-row gaps, including edge
  gaps; `row_budget`: tile span left after the rounded `top_gap`/`bottom_gap`.

The basis contains native `instance`, workspace/output IDs, positional `column`,
secondary `axis` (`vertical` or `horizontal`), ordered `members`, native `weights`,
tiled `area`, `edge_pad`, inter-row `gap`, `width_fraction`, `primary_extent`,
`tabbed` and `supported`. Members report ID, `title_bar_height`, tile `constraints`
(`min_width`, `min_height`, `max_width`, `max_height`, zero maximum is unlimited),
and fullscreen/maximized/maximized-to-edges/floating flags. Tile height constraints
already include title-bar height. Committed geometry and scroll positions are
deliberately outside the conditional basis.

Only ordinary untabbed scrolling groups are writable. Pinned, fullscreen or
maximized members make the column unsupported. Clients must inspect `supported`,
not infer write support merely from having geometry. Window IDs are live native
identities, not durable identities across compositor restarts.

## Whole-column write

Send a structured socket request (no `msg` action or focus round trip):

```json
{
  "cmd": "layout-allocate",
  "target": "a-member-window-id",
  "expected": { "...": "the complete basis object previously returned" },
  "weights": { "rows": [420, 140, 140], "top": 0, "bottom": 0 },
  "expected_active": "a-member-window-id",
  "target_content_extents": [420, 140, 140]
}
```

The example's `expected` placeholder must be replaced by the actual full object.
`rows` follow member order; each is finite and positive. Edge weights are
finite and nonnegative, and the sum must remain finite. Weights are relative native
allocation state, not independent percentages. Their exact values are preserved.
The native arrangement applies an effective row-weight floor of `0.05`; positive
raw weights below it remain valid restoration state (tab dissolution can produce
them), but do not bypass that effective floor.

`expected_active` is optional. When present it must equal the target and that
window must still be seat-active at execution. Use it for emphasis; omit it to
restore an inactive group. `target_content_extents` is also optional: when present,
the native layout previews the entire vector and rejects unless every requested
secondary content extent exactly equals the native constrained target. This is
useful when an external solver has computed integer pixels.

The server resolves the target and compares the entire expected basis with current
native state on its single event loop, validates all values and previews the
allocation before changing any native weight. A rejection changes nothing.
Success returns:

```text
{"ok":{"version":1,"previous_weights":{...},"accepted":{...LayoutColumn...}}}
```

`accepted.basis` is the basis for the next write. `accepted.geometry` has the new
native targets but may still have old committed client geometry. The operation
does not change focus, activate a workspace, reveal a column, or clear maximize.
Native arrangement/client commits remain asynchronous. The ordinary error envelope
is `{"err":"reason"}`; connection loss after transmission remains uncertain.

## Pixel allocation and restore

For feasible untabbed rows, assign tile extents as row weights and rounded edge
gap extents as edge weights, with their sum equal to `cross_span`. The native
rounding algorithm assigns the final row the remainder. For vertical secondary
sizing, add each member's title-bar height to desired content height; for horizontal
secondary sizing, title-bar height does not consume width. Use the optional content
vector to verify the round trip rather than assume constraints cannot intervene.

Save the original `basis.weights` as the native restoration payload. Restore by
assigning those weights against the **last accepted basis**, not against the old
pre-emphasis basis or a new geometry-only guess. A same-pixel weight edit still
changes the basis and rejects stale restoration. Changed membership, constraints,
workarea or output similarly reject. A timeout is not authority to replay.

This is compare-current-state, **not a monotonic revision**: an intervening change
that returns every basis field to exactly its old value (ABA) is not detected.
The stream is coalesced and deduplicated, not a log of every intermediate mutation.
Restoring native weights does not promise historical pixels under changed context;
that context is part of the predicate and must match first.
