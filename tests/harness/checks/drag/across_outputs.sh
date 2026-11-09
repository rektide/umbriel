#!/usr/bin/env bash
# harness: outputs=2
# Dragging a window across outputs must land it on the output the drop releases over, for tiled and floating windows
# and in both directions. The outputs are staggered like monitors of different sizes (the small one sits lower), so
# the drag also crosses the layout space beside the shorter monitor on its way.
set -euo pipefail

readonly BTN_LEFT=272
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[output.HEADLESS-2]
position = [0, 0]

[output.HEADLESS-1]
position = [1280, 360]

[animation]
enabled = false
EOF
"$UMBRIEL" msg config-reload > /dev/null

# HEADLESS-2 spans (0,0)-(1280,720); HEADLESS-1 spans (1280,360)-(2560,1080).
readonly LEFT_CENTRE=(640 360)
readonly RIGHT_CENTRE=(1920 720)

position_of() {
  "$UMBRIEL" outputs | awk -v name="$1" '$1 == name {found = 1; next} found && /Position:/ {split($2, p, ","); print p[1] "," p[2]; exit}'
}

wait_for_position() {
  local name=$1 expected=$2 actual=
  for _ in $(seq 40); do
    actual=$(position_of "$name")
    [[ $actual == "$expected" ]] && return 0
    sleep 0.25
  done
  echo "expected $name at $expected, got ${actual:-unknown}"
  exit 1
}

spawn_client() {
  foot --title="$1" sh -c 'sleep 120' > /dev/null 2>&1 &
}

wait_for_count() {
  local expected=$1 count=
  for _ in $(seq 60); do
    count=$("$UMBRIEL" windows --json | jq 'length')
    [[ $count == "$expected" ]] && return 0
    sleep 0.25
  done
  echo "timed out waiting for $expected window(s)"
  exit 1
}

workspace_of() {
  "$UMBRIEL" windows --json | jq -r --arg title "$1" '.[] | select(.title == $title) | .workspace | split(":")[0]'
}

wait_for_workspace() {
  local title=$1 expected=$2 actual=
  for _ in $(seq 40); do
    actual=$(workspace_of "$title")
    [[ $actual == "$expected" ]] && return 0
    sleep 0.25
  done
  echo "$title never reached $expected (at ${actual:-unknown})"
  exit 1
}

wait_for_position HEADLESS-2 "0,0"
wait_for_position HEADLESS-1 "1280,360"

drag() {
  local from_x=$1 from_y=$2 to_x=$3 to_y=$4
  "$POINTER" 2560 1080 \
    move "$from_x" "$from_y" mod logo press "$BTN_LEFT" \
    move "$to_x" "$to_y" release "$BTN_LEFT" mod none > /dev/null
}

# A tiled window dragged across, both ways. The single window fills the strip, so its centre is the output centre.
spawn_client crossy
wait_for_count 1
"$UMBRIEL" settle

drag "${LEFT_CENTRE[0]}" "${LEFT_CENTRE[1]}" "${RIGHT_CENTRE[0]}" "${RIGHT_CENTRE[1]}"
wait_for_workspace crossy HEADLESS-1

drag "${RIGHT_CENTRE[0]}" "${RIGHT_CENTRE[1]}" "${LEFT_CENTRE[0]}" "${LEFT_CENTRE[1]}"
wait_for_workspace crossy HEADLESS-2

# A floating window dragged across, both ways, through the staggered band beside the shorter output.
"$UMBRIEL" msg window-toggle-floating > /dev/null
"$UMBRIEL" settle
drag "${LEFT_CENTRE[0]}" "${LEFT_CENTRE[1]}" "${RIGHT_CENTRE[0]}" "${RIGHT_CENTRE[1]}"
wait_for_workspace crossy HEADLESS-1

drag "${RIGHT_CENTRE[0]}" "${RIGHT_CENTRE[1]}" "${LEFT_CENTRE[0]}" "${LEFT_CENTRE[1]}"
wait_for_workspace crossy HEADLESS-2

echo "tiled and floating drags crossed outputs in both directions"
