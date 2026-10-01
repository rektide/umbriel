#!/usr/bin/env bash
# Common two-output lifecycle, with and without an already-held exclusive seat.
setup_focus_columns
spawn_titled E
wait_for_count 5
"$UMBRIEL" msg "window-focus:$(id_of A)" > /dev/null
sleep 0.3
settled_shot "$UMBRIEL_RUNTIME_DIR/output-setup.png"
if [[ ${with_layer:-false} == true ]]; then
  "$UMBRIEL_POINTER_CLIENT" 2560 720 mod none pause 60000 > "$UMBRIEL_RUNTIME_DIR/keyboard.log" 2>&1 &
  "$UMBRIEL_LAYER_CLIENT" HEADLESS-1 0 bottom-layer keyboard=exclusive > "$UMBRIEL_RUNTIME_DIR/layer.log" 2>&1 &
  for _ in $(seq 60); do
    grep -q '^keyboard-enter$' "$UMBRIEL_RUNTIME_DIR/layer.log" && break
    sleep 0.1
  done
  grep -q '^keyboard-enter$' "$UMBRIEL_RUNTIME_DIR/layer.log" || { echo "exclusive layer never held seat"; exit 1; }
fi
"$UMBRIEL" msg "window-focus:$(id_of D)" > /dev/null
sleep 0.5
grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/before.png"
assert_signal "$UMBRIEL_RUNTIME_DIR/before.png" "220x160+405+280" 0.005 "source ring before cross-output activation"
"$UMBRIEL" msg "window-focus:$(id_of E)" > /dev/null
sleep 0.15
grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/after.png"
# D remains offscreen: this isolated band cannot contain its ordinary border.
assert_quiet "$UMBRIEL_RUNTIME_DIR/after.png" "220x160+405+280" 0.003 "source output ring cancelled"
grim -o HEADLESS-2 "$UMBRIEL_RUNTIME_DIR/destination.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/destination.png" "cross-output destination has no travel"
echo "cross-output activation ends source travel (exclusive layer: ${with_layer:-false})"
