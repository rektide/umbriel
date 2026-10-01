#!/usr/bin/env bash
# Grab takeover must cancel before the selected tile even moves/detaches.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
configure_focus_travel
setup_focus_columns
"$UMBRIEL" msg "window-focus:$(id_of C)" > /dev/null
sleep 0.3
settled_shot "$UMBRIEL_RUNTIME_DIR/grab-setup.png"
"$UMBRIEL" msg "window-focus:$(id_of B)" > /dev/null
sleep 0.5
assert_signal_retry "240x160+30+280" 0.004 "ring before destination grab"
"$UMBRIEL_POINTER_CLIENT" 1280 720 mod logo move 200 360 press 272 pause 500 release 272 mod none \
  > "$UMBRIEL_RUNTIME_DIR/grab.log" 2>&1 &
sleep 0.15
grim "$UMBRIEL_RUNTIME_DIR/grabbed.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/grabbed.png" "same-target grab cancels before motion"
assert_only_one_focused B "grab target"
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
echo "manual same-target compositor grab cancels travel"
