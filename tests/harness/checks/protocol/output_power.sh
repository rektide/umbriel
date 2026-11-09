#!/usr/bin/env bash
# harness: outputs=2
# Protocol requests share DPMS/session memory. Observe actual commit events with
# a client that remains connected through output destruction and recreation.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

readonly POWER_CLIENT="$UMBRIEL_OUTPUT_POWER_CLIENT"
readonly POWER_LOG="$UMBRIEL_RUNTIME_DIR/output-power.log"
readonly POINTER="$UMBRIEL_POINTER_CLIENT"
readonly MANAGEMENT="$UMBRIEL_OUTPUT_MANAGEMENT_CLIENT"

mark_of() { wc -l < "$1"; }
wait_since() {
  local file=$1 mark=$2 pattern=$3
  for _ in $(seq 80); do
    if tail -n +"$((mark + 1))" "$file" | grep -q "$pattern"; then
      return 0
    fi
    sleep 0.025
  done
  echo "missing '$pattern' in $file"
  tail -15 "$file"
  return 1
}
assert_none_since() {
  local file=$1 mark=$2 pattern=$3
  if tail -n +"$((mark + 1))" "$file" | grep -q "$pattern"; then
    echo "unexpected '$pattern' in $file since line $mark"
    return 1
  fi
}
assert_power() {
  local name=$1 expected=$2
  "$UMBRIEL" outputs --json | jq -e --arg name "$name" --argjson expected "$expected" \
    'any(.[]; .name == $name and .powered == $expected)' > /dev/null
  "$UMBRIEL" color --json | jq -e --arg name "$name" --argjson expected "$expected" \
    'any(.outputs[]; .name == $name and .enabled == $expected)' > /dev/null
}
barrier=0
sync_client() {
  barrier=$((barrier + 1))
  echo "sync $barrier" >&"$power_fd"
  await_lines "$POWER_LOG" "synced $barrier$" 1
}
request() {
  echo "$*" >&"$power_fd"
  sync_client
}

# Initial state comes from the physical backend, including already-dark heads.
"$UMBRIEL" msg dpms-off HEADLESS-2 > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/power-control"
exec {power_fd}<> "$UMBRIEL_RUNTIME_DIR/power-control"
"$POWER_CLIENT" <&"$power_fd" > "$POWER_LOG" 2>&1 &
power_pid=$!
await_lines "$POWER_LOG" 'mode HEADLESS-1 on$' 1
await_lines "$POWER_LOG" 'mode HEADLESS-2 off$' 1

# wlroots grants one exclusive control per output, not one per client.
mkfifo "$UMBRIEL_RUNTIME_DIR/second-power-control"
exec {second_fd}<> "$UMBRIEL_RUNTIME_DIR/second-power-control"
"$POWER_CLIENT" <&"$second_fd" > "$UMBRIEL_RUNTIME_DIR/second-power.log" 2>&1 &
second_pid=$!
await_lines "$UMBRIEL_RUNTIME_DIR/second-power.log" 'failed HEADLESS-1$' 1
await_lines "$UMBRIEL_RUNTIME_DIR/second-power.log" 'failed HEADLESS-2$' 1
echo quit >&"$second_fd"
wait "$second_pid"

mark=$(mark_of "$POWER_LOG")
request on HEADLESS-2
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'
assert_power HEADLESS-2 true
mark=$(mark_of "$POWER_LOG")
request off HEADLESS-2
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 off$'
assert_power HEADLESS-2 false

# Idempotent off needs no commit or acknowledgement. Partial-off survives input.
mark=$(mark_of "$POWER_LOG")
request off HEADLESS-2
assert_none_since "$POWER_LOG" "$mark" '^mode\|^failed'
"$POINTER" 2560 720 move 10 10
sync_client
assert_none_since "$POWER_LOG" "$mark" '^mode\|^failed'
assert_power HEADLESS-2 false

request off HEADLESS-1
mark=$(mark_of "$POWER_LOG")
"$POINTER" 2560 720 move 20 20
sync_client
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-1 on$'
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'

# IPC and logical-management commits reach the same held controls; test-only
# transactions and protocol requests against logical disable do not claim on.
mark=$(mark_of "$POWER_LOG")
"$UMBRIEL" msg dpms-off HEADLESS-2 > /dev/null
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 off$'
mark=$(mark_of "$POWER_LOG")
"$UMBRIEL" msg dpms-on HEADLESS-2 > /dev/null
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'
mark=$(mark_of "$POWER_LOG")
"$MANAGEMENT" test disable HEADLESS-2 > /dev/null
sync_client
assert_none_since "$POWER_LOG" "$mark" '^mode\|^failed'
# Logical disable withdraws wl_output but does not invalidate an already-held
# power control. Retain that resource specifically to exercise the rejection.
request retain HEADLESS-2
"$MANAGEMENT" apply disable HEADLESS-2 > /dev/null
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 off$'
mark=$(mark_of "$POWER_LOG")
request on HEADLESS-2
assert_none_since "$POWER_LOG" "$mark" '^mode\|^failed'
assert_power HEADLESS-2 false
request release HEADLESS-2
"$MANAGEMENT" apply enable HEADLESS-2 > /dev/null
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'

# Every replacement control must start off. Addressing the new same-name output
# must work even after several retired entries and failed/destroyed controls.
request off HEADLESS-2
for pulse in 1 2 3; do
  mark=$(mark_of "$POWER_LOG")
  compositor_mark=$(mark_of "$UMBRIEL_LOG")
  "$UMBRIEL" output-destroy HEADLESS-2 > /dev/null
  wait_since "$POWER_LOG" "$mark" '^removed HEADLESS-2$'
  "$UMBRIEL" output-create HEADLESS-2 > /dev/null
  wait_since "$POWER_LOG" "$mark" '^power HEADLESS-2$'
  wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 off$'
  sync_client
  assert_none_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'
  assert_none_since "$UMBRIEL_LOG" "$compositor_mark" "output 'HEADLESS-2': applied mode="
  assert_power HEADLESS-2 false
done
mark=$(mark_of "$POWER_LOG")
request on HEADLESS-2
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'
assert_power HEADLESS-2 true
mark=$(mark_of "$POWER_LOG")
request off HEADLESS-2
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 off$'
assert_power HEADLESS-2 false

# A client can start with no outputs and discover a later first output. Leave
# that control live for the harness's clean compositor-shutdown assertion.
"$UMBRIEL" output-destroy HEADLESS-1 > /dev/null
"$UMBRIEL" output-destroy HEADLESS-2 > /dev/null
sync_client
echo quit >&"$power_fd"
wait "$power_pid"
exec {power_fd}>&-
mkfifo "$UMBRIEL_RUNTIME_DIR/late-power-control"
exec {power_fd}<> "$UMBRIEL_RUNTIME_DIR/late-power-control"
"$POWER_CLIENT" <&"$power_fd" > "$UMBRIEL_RUNTIME_DIR/late-power.log" 2>&1 &
await_lines "$UMBRIEL_RUNTIME_DIR/late-power.log" 'ready$' 1
"$UMBRIEL" msg dpms-on > /dev/null
"$UMBRIEL" output-create HEADLESS-2 > /dev/null
await_lines "$UMBRIEL_RUNTIME_DIR/late-power.log" 'mode HEADLESS-2 on$' 1
echo 'off HEADLESS-2' >&"$power_fd"
await_lines "$UMBRIEL_RUNTIME_DIR/late-power.log" 'mode HEADLESS-2 off$' 1
assert_power HEADLESS-2 false

echo "power protocol shares DPMS memory, reports commits, and follows repeated hotplug and late output discovery"
