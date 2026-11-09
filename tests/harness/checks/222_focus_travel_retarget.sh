#!/usr/bin/env bash
# Distinct destinations sharing unchanged camera targets must borrow remaining
# progress, not jump to the running motion's global fraction.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
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
[[window_rule]]
match.title = "^stack-a$"
default_scrolling_column = "travel-stack"
default_scrolling_column_order = 10
[[window_rule]]
match.title = "^stack-b$"
default_scrolling_column = "travel-stack"
default_scrolling_column_order = 20
EOF
"$UMBRIEL" msg config-reload > /dev/null
for title in A stack-a stack-b; do spawn_titled "$title"; sleep 0.45; done
wait_for_count 3
"$UMBRIEL" msg window-focus "$(id_of A)" > /dev/null
sleep 0.3 # real time: the initial focus-reveal pan must settle before the baseline probe
warmup
settled_shot "$UMBRIEL_RUNTIME_DIR/setup.png"
"$UMBRIEL" msg window-focus "$(id_of stack-a)" > /dev/null
sleep 0.5 # real time: sample the ring mid-journey, with the 1500 ms pan still advancing
assert_signal_retry "220x160+200+280" 0.005 "ring before retarget"
"$UMBRIEL" msg window-focus "$(id_of stack-b)" > /dev/null
sleep 0.08
grim "$UMBRIEL_RUNTIME_DIR/retarget.png"
# Sample horizontal strokes, away from the ring's vertical sides and the real
# destination border (whose top is y=364). The old global fraction would put
# the top stroke in the lower band immediately.
assert_signal "$UMBRIEL_RUNTIME_DIR/retarget.png" "200x130+700+25" 0.003 "ring continues near first row"
assert_quiet "$UMBRIEL_RUNTIME_DIR/retarget.png" "200x100+700+190" 0.003 "no global-progress leap"
assert_only_one_focused stack-b "retarget"
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
assert_only_one_focused stack-b "settled"
echo "distinct same-camera retarget borrows remaining progress"
