#!/usr/bin/env bash
# Shared probes for the focus-travel checks. IPC is used only for identity/focus;
# presentation and settling are measured from the framebuffer.
spawn_titled() { foot --title="$1" sh -c 'sleep 120' > /dev/null 2>&1 & }
window_count() { "$UMBRIEL" windows --json | jq 'length'; }
wait_for_count() {
  for _ in $(seq 80); do
    [[ $(window_count) -eq $1 ]] && return 0
    sleep 0.25
  done
  echo "timed out waiting for $1 window(s), have $(window_count)"
  return 1
}
id_of() {
  "$UMBRIEL" windows --json | jq -r --arg t "$1" '[.[] | select(.title == $t) | .id] | if length == 1 then .[0] else "missing" end'
}
settled_shot() {
  local dest=$1 previous="$UMBRIEL_RUNTIME_DIR/.settle-prev.png" current="$UMBRIEL_RUNTIME_DIR/.settle-cur.png"
  grim "$previous"
  for _ in $(seq 24); do
    sleep 0.25
    grim "$current"
    if cmp -s "$previous" "$current"; then
      mv "$current" "$dest"
      return 0
    fi
    mv "$current" "$previous"
  done
  echo "framebuffer never settled"
  return 1
}
signal() { magick "$1" -crop "$2" -colorspace RGB -format '%[fx:mean.r - mean.g]' info:; }
assert_signal() {
  local value
  value=$(signal "$1" "$2")
  if (( $(awk -v v="$value" -v m="$3" 'BEGIN {print (v < m)}') )); then
    echo "$4: expected magenta signal >= $3, got $value"
    exit 1
  fi
}
assert_quiet() {
  local value
  value=$(signal "$1" "$2")
  if (( $(awk -v v="$value" -v m="$3" 'BEGIN {print (v > m)}') )); then
    echo "$4: expected magenta signal <= $3, got $value"
    exit 1
  fi
}
assert_signal_retry() {
  local crop=$1 min=$2 label=$3 shot="$UMBRIEL_RUNTIME_DIR/.retry.png" value=""
  for _ in $(seq 3); do
    grim "$shot"
    value=$(signal "$shot" "$crop")
    if (( $(awk -v v="$value" -v m="$min" 'BEGIN {print (v >= m)}') )); then
      cp "$shot" "$UMBRIEL_RUNTIME_DIR/focus-travel-retry-last.png"
      return 0
    fi
    sleep 0.12
  done
  echo "$label: expected magenta signal >= $min within the animation window, got $value"
  exit 1
}
assert_only_one_focused() {
  local read
  read=$("$UMBRIEL" windows --json | jq -r --arg t "$1" \
    '[.[] | select(.focused)] | if length == 1 and .[0].title == $t then "ok" else (map(.title) | tostring) end')
  [[ $read == ok ]] || { echo "$2: expected only $1 focused, saw: $read"; exit 1; }
  read=$("$UMBRIEL" windows --json | jq '[.[] | select(.active)] | length')
  [[ $read == 1 ]] || { echo "$2: expected one activated client, saw $read"; exit 1; }
}
warmup() {
  local probe="$UMBRIEL_RUNTIME_DIR/focus-travel-warmup.png"
  for _ in $(seq 10); do
    grim "$probe"
    if (( $(magick "$probe" -format '%[fx:mean]' info: | awk '{print ($1 > 0.001)}') )); then
      return 0
    fi
    sleep 0.3
  done
  echo "the headless output never rendered a frame"
  return 1
}
