#!/usr/bin/env bash
# Actual manual pointer takeover cancels even when it hits the selected destination.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
readonly POINTER="$UMBRIEL_POINTER_CLIENT"
cat >> "$UMBRIEL_CONFIG" <<'EOF'
[layout.scrolling]
default_extent_fraction = 0.5
center_focused = "always"
[animation.windows_move]
enabled = true
duration_ms = 1500
curve = "linear"
[animation.border]
enabled = false
[animation.focus_travel]
enabled = true
lead = 0.6
[colors.border]
focused = "#FF00FF"
EOF
"$UMBRIEL" msg config-reload > /dev/null
for title in A B C D; do spawn_titled "$title"; sleep 0.45; done
wait_for_count 4
"$UMBRIEL" msg "window-focus:$(id_of C)" > /dev/null
sleep 0.3
warmup
settled_shot "$UMBRIEL_RUNTIME_DIR/setup.png"
"$UMBRIEL" msg "window-focus:$(id_of B)" > /dev/null
sleep 0.75
assert_signal_retry "240x160+30+280" 0.004 "leftward ring before other-window click"
"$POINTER" 1280 720 move 1000 360 click 272 > /dev/null 2>&1
sleep 0.15
grim "$UMBRIEL_RUNTIME_DIR/other.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/other.png" "other-window click cancels"
assert_only_one_focused C "click target"
settled_shot "$UMBRIEL_RUNTIME_DIR/other-settled.png"
"$UMBRIEL" msg "window-focus:$(id_of B)" > /dev/null
sleep 0.5
assert_signal_retry "240x160+30+280" 0.004 "ring before destination click"
"$POINTER" 1280 720 move 200 360 click 272 > /dev/null 2>&1
sleep 0.15
grim "$UMBRIEL_RUNTIME_DIR/same.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/same.png" "destination click cancels"
assert_only_one_focused B "destination remains focused"
settled_shot "$UMBRIEL_RUNTIME_DIR/same-settled.png"
echo "manual pointer takeover cancels both different and same-target journeys"
