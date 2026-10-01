#!/usr/bin/env bash
# Removing the actual destination ends the supplementary scene object safely.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
configure_focus_travel
setup_focus_columns
start_focus_journey
"$UMBRIEL" msg "window-close:$(id_of D)" > /dev/null
wait_for_count 3
sleep 0.15
grim "$UMBRIEL_RUNTIME_DIR/removed.png"
rc=0
if [[ $(accent_strokes "$UMBRIEL_RUNTIME_DIR/removed.png") -gt 2 ]]; then
  echo "destination removal: supplementary ring remains after automatic replacement focus"
  rc=1
fi
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/end.png" "removal settled"
assert_only_one_focused C "removal selects surviving neighbor"
echo "replacement settles with one activated survivor and no supplementary ring"
exit "$rc"
