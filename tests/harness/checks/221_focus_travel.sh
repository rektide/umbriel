#!/usr/bin/env bash
# The opt-in leading-focus travel ring, observed on frames rather than layout targets. With windows_move on a
# deterministic linear 900ms curve and a magenta focused border, activating the fourth column from the first draws a
# compositor-owned ring that leaves the old focus, rides ahead of the panning strip, and lands exactly on the
# destination's border. IPC x/y are layout targets the instant a selection happens, so every assertion here samples
# screenshots, every journey settles on a real clock before the next one starts, and the only magenta on screen
# mid-flight is the ring: the destination is still offscreen, the origin's border swaps to gray instantly (border
# animation disabled), and intermediate columns never receive focus or activation. The check also proves the
# lifecycle: re-activating the same destination mid-flight keeps the journey (the asynchronous picker-dismissal
# replay), opening a window cancels it, an exclusive keyboard layer neither stops the ring nor makes the later
# refocus replay restart it, and disabling the setting removes the ring while everything else stays put.
set -euo pipefail

readonly RING_BAND="220x160+405+280"   # A→D journey: origin and final box both sit at x=328, so the lead bump
                                       # toward D's pre-pan presentation (x=2236) carries the ring to x 431..614
                                       # for camera progress 0.13..0.9 of the 1500ms pan; screenshot latency can
                                       # slide the sampled progress, so the band is wide and centered on it
readonly ORIGIN_BAND="160x160+100+280" # empty mid-flight: the ring has already left the origin behind
readonly LEFT_BAND="160x160+20+280"    # the leftward D→A journey leads to x 43..145 over the matching progress
readonly DEST_STROKE="20x60+316+330"   # the settled destination's focused border stroke
readonly SETTLE_PROBE="240x160+370+280"

declare -A CLIENT_PID=()

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

spawn_titled() {
  foot --title="$1" sh -c 'sleep 120' > /dev/null 2>&1 &
  CLIENT_PID[$1]=$!
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

# A journey takes the 1500ms windows_move pan plus the opener reveal; IPC geometry is final immediately, so the only
# honest settle is a real wait followed by a layout-target stability sample.
settle() {
  sleep 2.0
  local previous="" current=""
  for _ in $(seq 40); do
    current=$("$UMBRIEL" windows --json | jq -cS '[.[] | {id, x, w}]')
    if [[ -n $previous && $current == "$previous" ]]; then
      return 0
    fi
    previous=$current
    sleep 0.2
  done
  echo "geometry never stopped changing (last: $previous)"
  return 1
}

id_of() {
  "$UMBRIEL" windows --json | jq -r --arg t "$1" '[.[] | select(.title == $t) | .id] | if length == 1 then .[0] else "missing" end'
}

# Mean(red - green) over a crop, linearized: magenta is the only saturated red-and-blue color on screen, and every
# expected source of it is enumerated in the assertions. Saturated colors survive either encoding (see 430).
signal() {
  magick "$1" -crop "$2" -colorspace RGB -format '%[fx:mean.r - mean.g]' info:
}

assert_signal() {
  local file=$1 crop=$2 min=$3 label=$4
  local value
  value=$(signal "$file" "$crop")
  if (( $(awk -v v="$value" -v m="$min" 'BEGIN {print (v < m)}') )); then
    echo "$label: expected magenta signal >= $min, got $value"
    exit 1
  fi
}

assert_quiet() {
  local file=$1 crop=$2 max=$3 label=$4
  local value
  value=$(signal "$file" "$crop")
  if (( $(awk -v v="$value" -v m="$max" 'BEGIN {print (v > m)}') )); then
    echo "$label: expected magenta signal <= $max, got $value"
    exit 1
  fi
}

assert_only_one_focused() {
  local want_title=$1 label=$2
  local read
  read=$("$UMBRIEL" windows --json | jq -r --arg t "$want_title" \
    '[.[] | select(.focused)] | if length == 1 and .[0].title == $t then "ok" else (map(.title) | tostring) end')
  if [[ $read != "ok" ]]; then
    echo "$label: expected only $want_title focused, saw: $read"
    exit 1
  fi
  read=$("$UMBRIEL" windows --json | jq -r '[.[] | select(.active)] | if length == 1 then "ok" else "many" end')
  if [[ $read != "ok" ]]; then
    echo "$label: expected exactly one activated client, saw $read"
    exit 1
  fi
}

# Warm the render pipeline: the headless output commits its first frame lazily, and a black capture would fail the
# measured probes for the wrong reason.
warmup() {
  local probe
  for _ in $(seq 10); do
    probe="$UMBRIEL_RUNTIME_DIR/focus-travel-warmup.png"
    grim "$probe"
    if (( $(magick "$probe" -format '%[fx:mean]' info: | awk '{print ($1 > 0.001)}') )); then
      return 0
    fi
    sleep 0.3
  done
  echo "the headless output never rendered a frame"
  return 1
}

for title in A B C D; do
  spawn_titled "$title"
  wait_for_count "${#CLIENT_PID[@]}"
done
# The last spawn centered D; go back to A so the measured journey starts from a settled left end.
"$UMBRIEL" msg "window-focus:$(id_of A)" > /dev/null
settle
warmup

# Default-journey, observed mid-flight. A settled centered leaves the origin at x=328 and D presented at x=2236;
# centering D again puts the final box back at 328, so the ring's whole camera path is flat and the lead bump alone
# carries it to x≈590 at camera progress 0.4 — ahead of the origin while D is still offscreen right.
"$UMBRIEL" msg "window-focus:$(id_of D)" > /dev/null
sleep 0.5
mid="$UMBRIEL_RUNTIME_DIR/focus-travel-mid.png"
grim "$mid"
assert_signal "$mid" "$RING_BAND" 0.005 "mid-flight ring in the travel band"
assert_quiet "$mid" "$ORIGIN_BAND" 0.003 "ring has left the origin behind"
assert_only_one_focused D "mid-flight"

# Re-activating the same destination mid-flight must keep the journey, not restart it from the origin.
"$UMBRIEL" msg "window-focus:$(id_of D)" > /dev/null
sleep 0.2
mid2="$UMBRIEL_RUNTIME_DIR/focus-travel-replay.png"
grim "$mid2"
assert_signal "$mid2" "$RING_BAND" 0.005 "ring still traveling after same-destination replay"

# Handoff: the ring lands on the destination's border and is gone from the travel band.
settle
end="$UMBRIEL_RUNTIME_DIR/focus-travel-end.png"
grim "$end"
assert_signal "$end" "$DEST_STROKE" 0.03 "destination border carries the focused accent"
assert_quiet "$end" "$SETTLE_PROBE" 0.003 "travel band after settle"

# An unrelated focus change (the new window's startup focus) cancels the leftward journey: its ring vanishes within
# a frame while the newly focused column is still off to the right.
"$UMBRIEL" msg "window-focus:$(id_of A)" > /dev/null
sleep 0.45
left="$UMBRIEL_RUNTIME_DIR/focus-travel-leftward.png"
grim "$left"
assert_signal "$left" "$LEFT_BAND" 0.004 "leftward ring leads toward where A was presented"
spawn_titled E
wait_for_count 5
sleep 0.15
cancel="$UMBRIEL_RUNTIME_DIR/focus-travel-cancelled.png"
grim "$cancel"
assert_quiet "$cancel" "$LEFT_BAND" 0.003 "leftward ring cancelled by the new window's focus"
settle

# An exclusive keyboard layer (the Vicinae picker situation) neither stops the ring nor lets the later refocus
# replay restart it: the ring is still mid-flight after the layer grabs, and still mid-flight after it dies.
kill "${CLIENT_PID[E]}" 2>/dev/null || true
wait "${CLIENT_PID[E]}" 2>/dev/null || true
unset 'CLIENT_PID[E]'
wait_for_count 4
"$UMBRIEL" msg "window-focus:$(id_of A)" > /dev/null
settle
"$UMBRIEL" msg "window-focus:$(id_of D)" > /dev/null
sleep 0.1
"${UMBRIEL_LAYER_CLIENT:-./build-debug/tests/layer-client}" HEADLESS-1 0 bottom-layer keyboard=exclusive \
  > "$UMBRIEL_RUNTIME_DIR/focus-travel-layer.log" 2>&1 &
LAYER_PID=$!
sleep 0.25
layer="$UMBRIEL_RUNTIME_DIR/focus-travel-layer.png"
grim "$layer"
assert_signal "$layer" "$RING_BAND" 0.005 "ring travels while an exclusive layer holds the seat"
kill "$LAYER_PID" 2>/dev/null || true
wait "$LAYER_PID" 2>/dev/null || true
sleep 0.2
layer2="$UMBRIEL_RUNTIME_DIR/focus-travel-layer-replay.png"
grim "$layer2"
assert_signal "$layer2" "$RING_BAND" 0.005 "ring survives the layer-dismissal refocus replay"
settle
assert_only_one_focused D "after the layer replay settles"

# Disabling the setting removes the ring; the ordinary reveal and borders carry on.
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation.focus_travel]
enabled = false
EOF
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" msg "window-focus:$(id_of A)" > /dev/null
sleep 0.5
off="$UMBRIEL_RUNTIME_DIR/focus-travel-disabled.png"
grim "$off"
assert_quiet "$off" "$RING_BAND" 0.003 "no ring while focus_travel is disabled"
settle
assert_only_one_focused A "disabled journey still focuses its destination"

for pid in "${CLIENT_PID[@]}"; do
  kill "$pid" 2>/dev/null || true
done

echo "focus travel ring flies, leads, hands off, replays, cancels, and obeys its switch"
