#!/usr/bin/env bash
# A same-axis column swap is not a common strip pan, even with identical sizes.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
configure_focus_travel
setup_focus_columns
start_focus_journey
"$UMBRIEL" msg column-move-left > /dev/null
sleep 0.15
grim "$UMBRIEL_RUNTIME_DIR/swapped.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/swapped.png" "column swap cancels ring"
assert_only_one_focused D "swap keeps selection"
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
echo "arbitrary same-axis reflow is not admitted as travel"
