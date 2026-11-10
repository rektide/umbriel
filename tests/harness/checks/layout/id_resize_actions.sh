#!/usr/bin/env bash
# Sizing qualifiers address an unfocused window (or its column), never focus it,
# and reject stale IDs instead of falling back to the focused window.
set -euo pipefail
source "$(dirname "$UMBRIEL_HARNESS_LIB")/title_bar.sh"

cat >> "$UMBRIEL_CONFIG" <<'EOF'
[animation]
enabled = false
[input.focus]
follows_mouse = false
[input.cursor]
follows_focus = true
[layout.scrolling]
default_extent_fraction = 0.6
center_underfull_strip = false
EOF
"$UMBRIEL" msg config-reload > /dev/null

window() { title_bar_window "$1"; }
id() { window "$1" | jq -r .id; }
box() { window "$1" | jq -c '[.x,.y,.w,.h]'; }
field() { window "$1" | jq -r --arg field "$2" '.[$field]'; }
action() { "$UMBRIEL" msg "$@" > /dev/null; "$UMBRIEL" settle; }
spawn() {
  "$UMBRIEL_FRACTIONAL_CLIENT" "$1" 480 300 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  title_bar_wait_field "$1" title "$1"
  "$UMBRIEL" settle
}
still_focused() {
  [[ $("$UMBRIEL" focused-window --json | jq -r .id) == "$anchor" ]] || {
    echo "sizing stole focus from the anchor"
    return 1
  }
}
wait_size_change() {
  local title=$1 axis=$2 before=$3
  for _ in $(seq 120); do
    [[ $(field "$title" "$axis") != "$before" ]] && return 0
    sleep 0.025
  done
  echo "$title: $axis stayed at $before"
  return 1
}

spawn resize-anchor
anchor=$(id resize-anchor)
spawn resize-upper
spawn resize-lower
action window-consume-left
upper=$(id resize-upper)
lower=$(id resize-lower)
[[ $(field resize-upper column) == "$(field resize-lower column)" ]]
[[ $(field resize-upper column) != "$(field resize-anchor column)" ]]

# Bare width still uses the focused column, not either of its neighbors.
before_anchor_width=$(field resize-anchor w)
before_width=$(field resize-upper w)
action window-modify-width-right -0.1
wait_size_change resize-upper w "$before_width"
[[ $(field resize-upper w) == "$(field resize-lower w)" ]]
[[ $(field resize-anchor w) == "$before_anchor_width" ]]

# Bare height still addresses the focused lower row.
before_height=$(field resize-lower h)
action window-modify-height-up -0.1
wait_size_change resize-lower h "$before_height"

action window-focus "$anchor"
before_anchor=$(box resize-anchor)
before_width=$(field resize-upper w)
action window-modify-width-right 0.1 --window "$lower"
wait_size_change resize-upper w "$before_width"
[[ $(field resize-upper w) == "$(field resize-lower w)" ]]
[[ $(box resize-anchor) == "$before_anchor" ]]
still_focused

# The selected upper row grows down, even though a different column owns focus.
before_height=$(field resize-upper h)
before_y=$(field resize-upper y)
action window-modify-height-down 0.1 --window "$upper"
wait_size_change resize-upper h "$before_height"
(( $(field resize-upper h) > before_height ))
[[ $(field resize-upper y) == "$before_y" ]]
[[ $(box resize-anchor) == "$before_anchor" ]]
still_focused
before_height=$(field resize-lower h)
action window-modify-height-up 0.1 --window "$lower"
wait_size_change resize-lower h "$before_height"
still_focused

# Fraction/delta extent actions have the same qualifier and target semantics.
for args in 'window-set-primary-extent 0.4' 'window-modify-primary-extent 0.1'; do
  read -r command value <<< "$args"
  before_width=$(field resize-upper w)
  action "$command" "$value" --window "$lower"
  wait_size_change resize-upper w "$before_width"
  [[ $(field resize-upper w) == "$(field resize-lower w)" ]]
  still_focused
done
for args in 'window-set-secondary-extent 0.3' 'window-modify-secondary-extent 0.1'; do
  read -r command value <<< "$args"
  before_height=$(field resize-upper h)
  action "$command" "$value" --window "$upper"
  wait_size_change resize-upper h "$before_height"
  still_focused
done
before_width=$(field resize-upper w)
action window-modify-width-left 0.1 --window "$upper"
wait_size_change resize-upper w "$before_width"
still_focused

# An explicit floating target follows the bare edge action: resize the float,
# not a remembered tiled column. Neither form changes the anchor's geometry.
spawn resize-float
float=$(id resize-float)
action window-toggle-floating "$float"
before_width=$(field resize-float w)
action window-modify-width-right 0.1
wait_size_change resize-float w "$before_width"
action window-focus "$anchor"
before_anchor=$(box resize-anchor)
for command in window-modify-width-left window-modify-width-right; do
  before_width=$(field resize-float w)
  action "$command" -0.1 --window "$float"
  wait_size_change resize-float w "$before_width"
  [[ $(box resize-anchor) == "$before_anchor" ]]
  still_focused
done

# A real ID becomes stale on unmap. All eight actions share the established
# unknown-window error; none may quietly resize the anchor as a fallback.
action window-kill "$float"
title_bar_wait_absent resize-float
for command in window-modify-width-left window-modify-width-right \
  window-modify-height-up window-modify-height-down \
  window-set-primary-extent window-set-secondary-extent \
  window-modify-primary-extent window-modify-secondary-extent; do
  if reply=$("$UMBRIEL" msg "$command" 0.2 --window "$float" 2>&1); then
    echo "$command accepted a stale window ID"
    exit 1
  fi
  [[ $reply == *"unknown window: $float"* ]]
  [[ $(box resize-anchor) == "$before_anchor" ]]
  still_focused
done
echo "bare and ID-qualified sizing target the right windows without stealing focus"
