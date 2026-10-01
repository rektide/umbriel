#!/usr/bin/env bash
# Leaving a workspace cancels its ring, not just its visibility during switch.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
configure_focus_travel
setup_focus_columns
start_focus_journey
"$UMBRIEL" msg workspace-next > /dev/null
sleep 0.15
"$UMBRIEL" msg workspace-previous > /dev/null
sleep 0.15
grim "$UMBRIEL_RUNTIME_DIR/returned.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/returned.png" "return must not resurrect source journey"
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
assert_only_one_focused D "return preserves selection"
echo "workspace departure cancels rather than merely hiding travel"
