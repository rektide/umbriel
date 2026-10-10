#!/usr/bin/env bash
# harness: outputs=2
# wlr-output-power-management-v1 is how idle daemons (swayidle, hypridle)
# drive and observe DPMS. The client holds one power object per output, like
# swayidle does: each must report the current mode on creation, set_mode must
# reach the real DPMS path, a per-output off must survive input while another
# output is awake, and once every output is off, input must wake all of them
# and deliver the transitions to the client — the resume hook idle daemons run.
set -euo pipefail

readonly POWER_CLIENT="${UMBRIEL_OUTPUT_POWER_CLIENT:-./build-debug/tests/output-power-client}"
readonly POINTER=${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}
readonly POWER_LOG="$UMBRIEL_RUNTIME_DIR/output-power-client.log"
readonly POWER_FIFO="$UMBRIEL_RUNTIME_DIR/output-power-control"

mark_of() { wc -l < "$1"; }

wait_since() {
  local file=$1 mark=$2 pattern=$3
  for _ in $(seq 60); do
    if tail -n +"$((mark + 1))" "$file" | grep -q "$pattern"; then
      return 0
    fi
    sleep 0.05
  done
  echo "timed out waiting for '$pattern' in $file"
  tail -12 "$file" | sed 's/^/  | /'
  return 1
}

assert_none_since() {
  local file=$1 mark=$2 pattern=$3 message=$4
  sleep 0.2 # real time: prove nothing reacted to the input inside the window
  if tail -n +"$((mark + 1))" "$file" | grep -q "$pattern"; then
    echo "$message"
    return 1
  fi
}

mkfifo "$POWER_FIFO"
exec {power_fd}<> "$POWER_FIFO"
: > "$POWER_LOG"
# The marker is taken before the client starts: the initial-state lines below
# arrive within milliseconds of the spawn, and a mark sampled after them would
# skip them and time out.
mark=$(mark_of "$POWER_LOG")
"$POWER_CLIENT" <&"$power_fd" > "$POWER_LOG" 2>&1 &
wait_since "$POWER_LOG" "$mark" '^power HEADLESS-1$'
wait_since "$POWER_LOG" "$mark" '^power HEADLESS-2$'
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-1 on$'
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'

# set_mode(off) is the real DPMS path: the output powers down and the client
# is told about the transition it caused.
mark=$(mark_of "$POWER_LOG")
compositor_mark=$(mark_of "$UMBRIEL_LOG")
echo "off HEADLESS-2" >&"$power_fd"
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 off$'
wait_since "$UMBRIEL_LOG" "$compositor_mark" "output 'HEADLESS-2': powered off"

# A per-output protocol off behaves like a named DPMS action: input on the
# remaining awake output leaves it off.
mark=$(mark_of "$POWER_LOG")
compositor_mark=$(mark_of "$UMBRIEL_LOG")
"$POINTER" 2560 720 move 10 10
assert_none_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$' \
  "input woke protocol-off HEADLESS-2 while HEADLESS-1 was powered"
assert_none_since "$UMBRIEL_LOG" "$compositor_mark" "output 'HEADLESS-2': applied mode=" \
  "input woke protocol-off HEADLESS-2 while HEADLESS-1 was powered"

# set_mode(on) powers the output back up.
mark=$(mark_of "$POWER_LOG")
compositor_mark=$(mark_of "$UMBRIEL_LOG")
echo "on HEADLESS-2" >&"$power_fd"
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'
wait_since "$UMBRIEL_LOG" "$compositor_mark" "output 'HEADLESS-2': applied mode="

# With every output protocol-off, input wakes all of them and the client sees
# the transitions it did not cause — the swayidle resume path.
mark=$(mark_of "$POWER_LOG")
echo "off HEADLESS-1" >&"$power_fd"
echo "off HEADLESS-2" >&"$power_fd"
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-1 off$'
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 off$'
mark=$(mark_of "$POWER_LOG")
"$POINTER" 2560 720 move 20 20
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-1 on$'
wait_since "$POWER_LOG" "$mark" '^mode HEADLESS-2 on$'

echo "output power management reported initial state, drove DPMS, and delivered input-wake transitions"
