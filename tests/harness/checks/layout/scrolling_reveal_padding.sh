#!/usr/bin/env bash
# Reveal and snap padding change where the strip parks after focus moves and snap-backs. A column revealed from the
# right stops `reveal_padding` logical pixels short of the right viewport edge (one revealed from the left stops short
# of the left edge), so the strip's continuation stays legible beyond the focused column; a column snapped back after
# fullscreen keeps `snap_padding` pixels before its leading edge instead of landing flush at the start edge. The strip
# bounds still clamp, which is why the freshly spawned last column lands flush right first. Reloaded at zero padding
# the same focus jumps land flush, pinning the legacy behavior on the same live instance.
set -euo pipefail

# Output is 1280x720. With the shipped decoration defaults (gap 8, border 2): edgePad = 10, totalGap = 12,
# viewport = 1260, a 0.5-fraction column is 624 wide, and five columns sit at strip x 0, 636, 1272, 1908, 2544
# (total 3168, max scroll 1908). Viewport x maps to output x with the +10 edgePad.
readonly COLUMN_W=624
readonly PAD=100

spawn_titled() {
  foot --title="$1" sh -c 'sleep 120' > /dev/null 2>&1 &
}

window_count() { "$UMBRIEL" windows --json | jq 'length'; }

wait_for_count() {
  for _ in $(seq 80); do
    [[ $(window_count) -eq $1 ]] && return 0
    sleep 0.25
  done
  echo "timed out waiting for $1 window(s), have $(window_count)"
  return 1
}

field_of() {
  "$UMBRIEL" windows --json \
    | jq -r --arg t "$1" --arg f "$2" \
      '[.[] | select(.title == $t) | .[$f]] | if length == 1 then .[0] | tostring else "missing" end'
}

id_of() { field_of "$1" id; }

x_of() { field_of "$1" x; }

wait_for_x() {
  local title=$1 want=$2 message=$3
  for _ in $(seq 60); do
    [[ $(x_of "$title") == "$want" ]] && return 0
    sleep 0.1
  done
  echo "$message, got x=$(x_of "$title") (want $want)"
  return 1
}

focus_id() {
  "$UMBRIEL" msg window-focus "$1" > /dev/null
}

cat >> "$UMBRIEL_CONFIG" <<EOF

[animation]
enabled = false

[layout]
mode = "scrolling"

[layout.scrolling]
default_extent_fraction = 0.5
center_focused = "never"
reveal_padding = $PAD
snap_padding = $PAD
EOF
"$UMBRIEL" msg config-reload > /dev/null

# Spawned one at a time so the columns end up in a known left-to-right order:
# each new window is inserted after the focused one.
declare -i spawned=0
for title in A B C D E; do
  spawn_titled "$title"
  spawned+=1
  wait_for_count "$spawned"
done

B_ID=$(id_of B)
D_ID=$(id_of D)
if [[ $B_ID == "missing" || $D_ID == "missing" ]]; then
  echo "expected five distinctly titled windows, got: $("$UMBRIEL" windows --json | jq -c '[.[].title]')"
  exit 1
fi

# Focus rested on E, whose reveal clamped at the strip end: E lands flush right.
# E.x = 10 + 2544 - 1908 = 646.
wait_for_x E 646 "spawn-time reveal should clamp E flush right"

# Jump left to B: it enters from the left and stops 100 px short of the left
# viewport edge. B.x = 10 + 636 - 536 = 110, with A peeking 98 px wide at -526.
focus_id "$B_ID"
wait_for_x B 110 "left-entry reveal should keep padding before B"
wait_for_x A -526 "A should keep peeking after the left-entry reveal"

# Jump right to D: it enters from the right and stops 100 px short of the right
# viewport edge. D.x = 10 + 1908 - 1372 = 546, with E peeking 98 px wide.
focus_id "$D_ID"
wait_for_x D 546 "right-entry reveal should keep padding beyond D"
wait_for_x E 1182 "E should keep peeking after the right-entry reveal"

# Fullscreen round trip on D: the exit snap keeps 100 px before D's leading edge
# instead of flush left. D.x = 10 + 1908 - 1808 = 110.
"$UMBRIEL" msg window-toggle-fullscreen > /dev/null
"$UMBRIEL" settle
"$UMBRIEL" msg window-toggle-fullscreen > /dev/null
wait_for_x D 110 "fullscreen-exit snap should keep padding before D"

# Zero the padding on the same instance: the same focus jumps land flush again.
sed -i "s/^reveal_padding = .*/reveal_padding = 0/; s/^snap_padding = .*/snap_padding = 0/" "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
focus_id "$B_ID"
wait_for_x B 10 "zero padding should restore flush left-entry landings"

echo "reveal/snap padding landed columns $PAD px short of the viewport edges"
