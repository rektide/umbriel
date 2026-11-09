#!/usr/bin/env bash
# Picker order matters: activation arrives AFTER an exclusive layer owns keyboard.
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
"$UMBRIEL" msg window-focus "$(id_of A)" > /dev/null
sleep 0.3 # real time: the initial focus-reveal pan must settle before the baseline probe
warmup
settled_shot "$UMBRIEL_RUNTIME_DIR/setup.png"
log="$UMBRIEL_RUNTIME_DIR/layer.log"
# Headless seats have no keyboard until a virtual input client provides one.
"$UMBRIEL_POINTER_CLIENT" 1280 720 mod none pause 60000 > "$UMBRIEL_RUNTIME_DIR/keyboard.log" 2>&1 &
"$UMBRIEL_LAYER_CLIENT" HEADLESS-1 0 bottom-layer keyboard=exclusive > "$log" 2>&1 &
pid=$!
for _ in $(seq 60); do
  grep -q '^keyboard-enter$' "$log" && break
  sleep 0.1
done
grep -q '^keyboard-enter$' "$log" || { echo "exclusive layer never held keyboard"; exit 1; }
"$UMBRIEL" msg window-focus "$(id_of D)" > /dev/null
sleep 0.5 # real time: sample the ring mid-journey beneath the exclusive layer
assert_signal_retry "220x160+405+280" 0.005 "ring beneath existing exclusive layer"
# This kill is the tested dismissal, not exit cleanup.
kill "$pid"
wait "$pid" 2>/dev/null || true
sleep 0.2 # real time: give the layer-dismiss focus replay a beat before the replay probe
grim "$UMBRIEL_RUNTIME_DIR/replay.png"
assert_signal "$UMBRIEL_RUNTIME_DIR/replay.png" "220x160+405+280" 0.005 "dismissal replay preserves ring"
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
assert_only_one_focused D "dismissal settled"
echo "layer-held activation travels and dismissal preserves it"
