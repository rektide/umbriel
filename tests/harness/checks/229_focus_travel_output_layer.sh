#!/usr/bin/env bash
# harness: outputs=2
# Cross-output activation must cancel even while an exclusive picker owns seat.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
configure_focus_travel
cat >> "$UMBRIEL_CONFIG" <<'EOF'
[[window_rule]]
match.title = "^[ABCD]$"
default_output = "HEADLESS-1"
[[window_rule]]
match.title = "^E$"
default_output = "HEADLESS-2"
EOF
with_layer=true
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel_output.sh"
