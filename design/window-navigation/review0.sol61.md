# Leading-focus runtime review

## Scope and evidence

Review baseline: `51e6528ecf3d787531d9f8e536df24196edea17d` (the clean task-start checkout). Reviewed tip: `cc0b07e3`, including runtime integration `3579d0ea` and the first screenshot harness. This is a review checkpoint, not acceptance. Independent Standards and Spec reviewers read the committed diff; neither changed code or reran passing checks. Parent also read the key runtime, geometry, and harness paths.

The opt-in, shared-clock, renderer-backed architecture fits the user's preferred visual-ring direction. The ring remains uninstalled and disabled on the live desktop. The configuration-only scrolling ramp is already applied independently.

## Standards

### Hard violations

- **Animation-sized settling wait.** [`221_focus_travel.sh`](/tests/harness/checks/221_focus_travel.sh), originally lines 64–78, sleeps two seconds based on the 1500 ms animation, then polls IPC layout targets—which become final immediately. This does not observe presentation settling. [Contributor instructions](/CONTRIBUTING.md#development-commands), lines 100–103, require consecutive matching frame captures instead of animation-sized waits. Mid-flight sampling is a separate concern.
- **Unnecessary exit cleanup.** The final client-kill loop, originally lines 233–235, contradicts harness ownership of cleanup in `CONTRIBUTING.md` lines 89–93. Earlier kills that exercise lifecycle behavior are legitimate; end-of-check cleanup belongs to the harness.
- **Several long scenarios in one check.** Journey/handoff, cancellation, exclusive-layer replay, and disabling are sequential in one approximately 21-second check. `CONTRIBUTING.md` lines 112–114 says to split checks growing past a few seconds so isolated instances can run concurrently. Separate coherent lifecycle scenarios rather than sharing accumulated setup.

### Judgment call

- **Duplicated Code:** bind and rebase repeat path destination initialization and the viewport/endpoint eligibility predicate in [`workspace.cpp`](/src/workspace/workspace.cpp), originally lines 1220–1238 and 1272–1288. A small shared endpoint-fit helper can prevent policy drift while preserving their different origin and translation rules.

No other meaningful non-tooling standards findings were reported. Geometry, presentation, and lifecycle responsibilities are sensibly separated; the presenter uses existing `umbrielfx` border machinery.

## Spec

Changes required; no material scope creep found. Actual keyboard focus stepping and cross-workspace travel were not added.

1. **Cross-output cancellation reads new seat focus instead of old focus.** [`focus.cpp`](/src/server/focus.cpp), originally lines 99–110, applies seat focus before reading the focused surface. Consequently, the workspace comparison usually sees the destination workspace, leaving the previous output's ring running. An exclusive layer yields no window and also skips cancellation. This violates [checkpoint requirement 6](/design/window-navigation/checkpoint0.sol61.md#corrections-required-before-integration) and [unsupported-focus cancellation](/design/window-navigation/focus-travel0.sol61.md#configuration-and-lifecycle).
2. **Same-destination exemption also exempts manual interactions.** [`workspace.cpp`](/src/workspace/workspace.cpp), originally lines 1133–1135, returns for every focus reason when the target matches. A compositor move grab reaches this branch through [`cursor.cpp`](/src/input/cursor.cpp); tiled movement can remain pending before detachment, letting the ring continue during the grab. Launcher-dismissal replay must be distinguished from [interactions required to cancel](/design/window-navigation/focus-travel0.sol61.md#configuration-and-lifecycle).
3. **Exact handoff is not guaranteed for admitted endpoints.** Bind only checks whether constrained endpoints exist. [`focus_travel.cpp`](/src/layout/focus_travel.cpp), originally lines 84–86, translates any fitting but edge-overlapping endpoint. Such a path ends offset from the real destination border, contrary to [exact supported endpoints](/design/window-navigation/focus-travel0.sol61.md#geometry-model-to-test). Reject endpoints requiring translation or define an honest fallback.
4. **Verification gate is partial.** The original harness does not retarget to a different destination, remove the traveling destination, or switch outputs/workspaces. It disables the setting after settling, then starts another journey, rather than verifying mid-flight disable cleanup. These are explicit [runtime verification gates](/design/window-navigation/checkpoint0.sol61.md#authorized-next-slice), not established by same-target replay or startup-focus cancellation.

## Additional parent findings

- **Destination-only translation check is insufficient.** Same-axis column swaps can preserve destination dimensions and cross-axis position. Bind has no translation gate; rebase checks only the destination. Verify a common strip displacement across participating established tiles, and cancel arbitrary reflow instead of applying camera math to it.
- **A rebind onto an unchanged running camera can jump.** Bind captures current ring/destination boxes, but a same-motion early return preserves nonzero global progress. Applying that old progress to newly rebased boxes can immediately advance the ring. Normalize borrowed progress from the binding fraction, or deliberately decline unchanged-camera/no-new-scroll journeys within the existing fallback scope. Observe distinct-target retargeting before claiming continuity.
- **The launcher case must begin with the layer holding the seat.** The original test activates the target and only then creates an exclusive layer. Add activation while an already-mapped exclusive layer owns the seat, then dismiss it; validate the layer is actually present rather than inferring it from a sleep.

## Revision direction

The committed predecessor is preserved. The implementation agent has been authorized to fix these gaps in separate coherent commits, split the runtime checks, use rendered-frame convergence, and update evidence claims. No patched-compositor installation or live setting change is authorized by this review.

**Summary:** Standards: 4 findings (3 hard violations, 1 judgment call), worst is the non-rendered settling barrier. Spec: 4 findings, worst is cancellation reading the already-replaced seat focus. Parent findings remain separately listed; neither axis masks the other.
