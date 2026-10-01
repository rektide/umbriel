#!/usr/bin/env bash
# Leading ring, same-destination replay and exact handoff, observed on rendered frames.
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
EOF
"$UMBRIEL" msg config-reload > /dev/null
for title in A B C D; do spawn_titled "$title"; sleep 0.45; done
wait_for_count 4
"$UMBRIEL" msg "window-focus:$(id_of A)" > /dev/null
sleep 0.3
warmup
settled_shot "$UMBRIEL_RUNTIME_DIR/setup.png"
# Origin and final box both sit at x=328. The lead bump carries the ring right
# while D is still offscreen. No intermediate client acquires focus/activation.
"$UMBRIEL" msg "window-focus:$(id_of D)" > /dev/null
sleep 0.5
grim "$UMBRIEL_RUNTIME_DIR/mid.png"
assert_signal "$UMBRIEL_RUNTIME_DIR/mid.png" "220x160+405+280" 0.005 "ring ahead of camera"
assert_quiet "$UMBRIEL_RUNTIME_DIR/mid.png" "160x160+100+280" 0.003 "ring left origin"
assert_only_one_focused D "mid-flight"
"$UMBRIEL" msg "window-focus:$(id_of D)" > /dev/null
sleep 0.2
grim "$UMBRIEL_RUNTIME_DIR/replay.png"
assert_signal "$UMBRIEL_RUNTIME_DIR/replay.png" "220x160+405+280" 0.005 "same-target replay"
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
assert_signal "$UMBRIEL_RUNTIME_DIR/end.png" "20x60+316+330" 0.03 "destination border handoff"
assert_quiet "$UMBRIEL_RUNTIME_DIR/end.png" "240x160+370+280" 0.003 "travel band settled"
assert_only_one_focused D "settled"
echo "ring leads, replays and hands off without intermediate activation"
