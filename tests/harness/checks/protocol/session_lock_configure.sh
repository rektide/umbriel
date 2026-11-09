#!/usr/bin/env bash
# Static lock surfaces must stay idle while real output resizes still configure them.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

readonly LOCK_LOG="$UMBRIEL_RUNTIME_DIR/lock-client.log"
readonly PROTOCOL_LOG="$UMBRIEL_RUNTIME_DIR/lock-protocol.log"
readonly CONFIGURE_PATTERN='.*ext_session_lock_surface_v1.*\.configure('
mkfifo "$UMBRIEL_RUNTIME_DIR/lock-control"
exec {lock_fd}<> "$UMBRIEL_RUNTIME_DIR/lock-control"
WAYLAND_DEBUG=client "$UMBRIEL_LOCK_CLIENT" <&"$lock_fd" > "$LOCK_LOG" 2> "$PROTOCOL_LOG" &

assert_idle() {
  local before after
  before=$(events "$PROTOCOL_LOG" "$CONFIGURE_PATTERN")
  sleep 0.3 # real time: detect unsolicited configures while the locked output is static
  after=$(events "$PROTOCOL_LOG" "$CONFIGURE_PATTERN")
  if [[ "$before" != "$after" ]]; then
    echo "static lock surface received $((after-before)) redundant configures"
    return 1
  fi
}

await_lines "$LOCK_LOG" 'locked$' 1
"$UMBRIEL" settle > /dev/null
assert_idle

cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "960x540"
scale = 1.0
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
await_lines "$PROTOCOL_LOG" "${CONFIGURE_PATTERN}.*960, 540)" 1
"$UMBRIEL" settle > /dev/null
assert_idle

# A remembered-off recreated output must not hold lock confirmation hostage.
# The existing lock client has no replacement surface after hotplug, so waking
# it must expose only the compositor's opaque secure blank.
"$UMBRIEL" msg dpms-off HEADLESS-1 > /dev/null
"$UMBRIEL" output-destroy HEADLESS-1 > /dev/null
"$UMBRIEL" output-create HEADLESS-1 > /dev/null
"$UMBRIEL" msg dpms-on HEADLESS-1 > /dev/null
timeout 5s grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/lock-wake.png"
assert_region_rgb "$UMBRIEL_RUNTIME_DIR/lock-wake.png" '100x100+20+20' 0 0 0 'hotplug wake secure blank'

echo unlock >&"$lock_fd"
await_lines "$LOCK_LOG" 'unlocked$' 1

# Start a new lock with the recreated output still dark. No physical output
# needs to present, so confirmation must arrive without waiting for wake.
"$UMBRIEL" msg dpms-off HEADLESS-1 > /dev/null
"$UMBRIEL" output-destroy HEADLESS-1 > /dev/null
"$UMBRIEL" output-create HEADLESS-1 > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/dark-lock-control"
exec {dark_fd}<> "$UMBRIEL_RUNTIME_DIR/dark-lock-control"
"$UMBRIEL_LOCK_CLIENT" <&"$dark_fd" > "$UMBRIEL_RUNTIME_DIR/dark-lock.log" 2>&1 &
await_lines "$UMBRIEL_RUNTIME_DIR/dark-lock.log" 'locked$' 1
"$UMBRIEL" msg dpms-on HEADLESS-1 > /dev/null
timeout 5s grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/dark-lock-wake.png"
assert_region_rgb "$UMBRIEL_RUNTIME_DIR/dark-lock-wake.png" '100x100+20+20' 16 32 48 'dark lock wake'
echo unlock >&"$dark_fd"
await_lines "$UMBRIEL_RUNTIME_DIR/dark-lock.log" 'unlocked$' 1
echo "lock surface stayed idle, resized on output change, and unlocked"
