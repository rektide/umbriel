#!/usr/bin/env bash
# harness: outputs=2
# DPMS is compositor-owned output power, not output removal. A named action must
# remain in effect while another monitor is awake. A bare action must affect
# every configured monitor, ignore the input release that triggered it, then
# wake every monitor on genuinely new input. Two monitors are the whole point,
# which the header directive above asks the harness for.
set -euo pipefail

POINTER=${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}
readonly KEY_O=24

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[keybinds]
"Mod+O" = { action = "dpms-off", repeat = false }
EOF
"$UMBRIEL" msg config-reload > /dev/null

log_mark() { wc -l < "$UMBRIEL_LOG"; }

wait_for_log_since() {
  local mark=$1 pattern=$2
  for _ in $(seq 40); do
    if tail -n +"$((mark + 1))" "$UMBRIEL_LOG" | grep -q "$pattern"; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for log: $pattern"
  tail -12 "$UMBRIEL_LOG" | sed 's/^/  | /'
  return 1
}

failures=0

assert_no_log_since() {
  local mark=$1 pattern=$2 message=$3
  sleep 0.1
  if tail -n +"$((mark + 1))" "$UMBRIEL_LOG" | grep -q "$pattern"; then
    echo "$message"
    failures=$((failures + 1))
  fi
}

wait_for_power() {
  local name=$1 expected=$2
  for _ in $(seq 40); do
    if "$UMBRIEL" outputs --json | jq -e --arg name "$name" --argjson power "$expected" \
      'any(.[]; .name == $name and .enabled and .powered == $power)' > /dev/null \
      && "$UMBRIEL" color --json | jq -e --arg name "$name" --argjson power "$expected" \
        'any(.outputs[]; .name == $name and .enabled == $power)' > /dev/null; then
      return 0
    fi
    sleep 0.1
  done
  echo "expected $name logically enabled and physical power $expected: $("$UMBRIEL" outputs --json)"
  return 1
}

wait_for_workspace() {
  local expected=$1 actual=
  for _ in $(seq 40); do
    actual=$("$UMBRIEL" windows --json | jq -r '.[0].workspace // ""')
    [[ $actual == "$expected" ]] && return 0
    sleep 0.1
  done
  echo "expected workspace '$expected', got '$actual'"
  return 1
}

bounce() {
  "$UMBRIEL" output-destroy "$1" > /dev/null
  "$UMBRIEL" output-create "$1" > /dev/null
}

# A DPMS keybind runs on the key press. Its release and the trailing modifier
# change belong to that same input sequence, so neither may wake the outputs.
mark=$(log_mark)
"$POINTER" 2560 720 mod logo tap "$KEY_O" mod none
wait_for_log_since "$mark" "output 'HEADLESS-1': powered off"
wait_for_log_since "$mark" "output 'HEADLESS-2': powered off"
assert_no_log_since "$mark" "output 'HEADLESS-1': applied mode=" \
  "the key or modifier release that triggered DPMS immediately woke HEADLESS-1"
assert_no_log_since "$mark" "output 'HEADLESS-2': applied mode=" \
  "the key or modifier release that triggered DPMS immediately woke HEADLESS-2"

# Restore a known state even if the assertion above failed and both outputs are
# already awake.
"$UMBRIEL" msg dpms-on > /dev/null

# A named action is case-insensitive, changes only the requested monitor, and
# stays in effect during activity on the monitor that remains awake.
mark=$(log_mark)
"$UMBRIEL" msg dpms-off headless-2 > /dev/null
wait_for_log_since "$mark" "output 'HEADLESS-2': powered off"
if tail -n +"$((mark + 1))" "$UMBRIEL_LOG" | grep -q "output 'HEADLESS-1': powered off"; then
  echo "named DPMS action also powered off HEADLESS-1"
  exit 1
fi

mark=$(log_mark)
"$POINTER" 2560 720 move 10 10
assert_no_log_since "$mark" "output 'HEADLESS-2': applied mode=" \
  "input on the remaining awake monitor woke named DPMS output HEADLESS-2"

# Reapply the named off state so the explicit on transition below is observable
# even during the pre-fix sensitivity run, where pointer motion already woke it.
"$UMBRIEL" msg dpms-off headless-2 > /dev/null
mark=$(log_mark)
"$UMBRIEL" msg dpms-on headless-2 > /dev/null
wait_for_log_since "$mark" "output 'HEADLESS-2': applied mode="

# A bare action changes all configured monitors.
mark=$(log_mark)
"$UMBRIEL" msg dpms-off > /dev/null
wait_for_log_since "$mark" "output 'HEADLESS-1': powered off"
wait_for_log_since "$mark" "output 'HEADLESS-2': powered off"

# Pointer motion wakes both without an explicit dpms-on command.
mark=$(log_mark)
"$POINTER" 2560 720 move 20 20
wait_for_log_since "$mark" "output 'HEADLESS-1': applied mode="
wait_for_log_since "$mark" "output 'HEADLESS-2': applied mode="

# Same-connector recreation must start dark, not enable briefly and turn off
# again. Once evacuated, the window stays in the exact refuge workspace through
# every pulse; only a successful wake makes its home eligible again.
foot --title=dpms-bounce sh -c 'sleep 120' > /dev/null 2>&1 &
wait_for_workspace 'HEADLESS-2:1'
"$UMBRIEL" msg dpms-off HEADLESS-2 > /dev/null
mark=$(log_mark)
"$UMBRIEL" output-destroy HEADLESS-2 > /dev/null
for _ in $(seq 40); do
  refuge=$("$UMBRIEL" windows --json | jq -r '.[0].workspace // ""')
  [[ $refuge == HEADLESS-1:* ]] && break
  sleep 0.1
done
[[ $refuge == HEADLESS-1:* ]] || { echo "window was not evacuated: $refuge"; exit 1; }
"$UMBRIEL" output-create HEADLESS-2 > /dev/null
for pulse in 1 2 3; do
  wait_for_power HEADLESS-2 false
  wait_for_workspace "$refuge"
  "$POINTER" 2560 720 move "$((30 + pulse))" 30
  wait_for_power HEADLESS-2 false
  assert_no_log_since "$mark" "output 'HEADLESS-2': applied mode=" \
    "remembered-off reconnect transiently enabled HEADLESS-2"
  assert_no_log_since "$mark" 'restored .* displaced windows' \
    "remembered-off reconnect restored displaced windows"
  [[ $pulse == 3 ]] || bounce HEADLESS-2
done
"$UMBRIEL" msg dpms-on HEADLESS-2 > /dev/null
wait_for_power HEADLESS-2 true
wait_for_workspace 'HEADLESS-2:1'
bounce HEADLESS-2
wait_for_power HEADLESS-2 true
wait_for_workspace 'HEADLESS-2:1'

# Input during an HPD gap ends the global off request, including the absent
# monitor's memory. It must return lit even though the other monitor is awake.
"$UMBRIEL" msg dpms-off > /dev/null
"$UMBRIEL" output-destroy HEADLESS-2 > /dev/null
"$POINTER" 2560 720 move 40 40
wait_for_power HEADLESS-1 true
"$UMBRIEL" output-create HEADLESS-2 > /dev/null
wait_for_power HEADLESS-2 true
wait_for_workspace 'HEADLESS-2:1'

# With every backend object gone, reconnects remain dark and cannot restore
# windows. All-dark input still wakes both and recovers the original home.
"$UMBRIEL" msg dpms-off > /dev/null
"$UMBRIEL" output-destroy HEADLESS-1 > /dev/null
"$UMBRIEL" output-destroy HEADLESS-2 > /dev/null
wait_for_workspace ''
"$UMBRIEL" output-create HEADLESS-1 > /dev/null
"$UMBRIEL" output-create HEADLESS-2 > /dev/null
wait_for_power HEADLESS-1 false
wait_for_power HEADLESS-2 false
wait_for_workspace ''
"$POINTER" 2560 720 tap 30
wait_for_power HEADLESS-1 true
wait_for_power HEADLESS-2 true
wait_for_workspace 'HEADLESS-2:1'

# Explicit global on clears even absent choices and succeeds with no outputs.
# A named request to an absent output still reports an error.
"$UMBRIEL" msg dpms-off > /dev/null
"$UMBRIEL" output-destroy HEADLESS-1 > /dev/null
"$UMBRIEL" output-destroy HEADLESS-2 > /dev/null
if "$UMBRIEL" msg dpms-on HEADLESS-2 > /dev/null 2>&1; then
  echo "named power-on of an absent output unexpectedly succeeded"
  exit 1
fi
"$UMBRIEL" msg dpms-on > /dev/null
"$UMBRIEL" output-create HEADLESS-1 > /dev/null
"$UMBRIEL" output-create HEADLESS-2 > /dev/null
wait_for_power HEADLESS-1 true
wait_for_power HEADLESS-2 true
wait_for_workspace 'HEADLESS-2:1'

# Input with no backend objects also expires the dormant all-dark request.
"$UMBRIEL" msg dpms-off > /dev/null
"$UMBRIEL" output-destroy HEADLESS-1 > /dev/null
"$UMBRIEL" output-destroy HEADLESS-2 > /dev/null
"$POINTER" 2560 720 tap 31
"$UMBRIEL" output-create HEADLESS-1 > /dev/null
"$UMBRIEL" output-create HEADLESS-2 > /dev/null
wait_for_power HEADLESS-1 true
wait_for_power HEADLESS-2 true
wait_for_workspace 'HEADLESS-2:1'

if ((failures > 0)); then
  exit 1
fi

echo "DPMS survives repeated hotplug without restoration churn, preserves partial-off, and wakes absent heads too"
