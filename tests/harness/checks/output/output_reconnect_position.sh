#!/usr/bin/env bash
# harness: outputs=2
# A monitor that drops off and returns must come back to its established layout position. Auto placement re-packs the
# remaining outputs in connection order when one leaves, and appends the returning one, which mirrors the arrangement
# against the physical desk whenever the leaving monitor was not the rightmost. The established arrangement is
# remembered and restored instead: both outputs end exactly where they started, in both directions.
set -euo pipefail

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

output_exists() {
  "$UMBRIEL" outputs | awk -v name="$1" '$1 == name {found = 1} END {exit found ? 0 : 1}'
}

wait_for_gone() {
  for _ in $(seq 40); do
    output_exists "$1" || return 0
    sleep 0.25
  done
  echo "$1 never went away"
  exit 1
}

# The headless pair boots in either connection order; the check reads the arrangement it finds and asserts it survives.
pos_a=$(position_of HEADLESS-1)
pos_b=$(position_of HEADLESS-2)
if (( ${pos_a%%,*} <= ${pos_b%%,*} )); then
  left=HEADLESS-1 left_pos=$pos_a
  right=HEADLESS-2 right_pos=$pos_b
else
  left=HEADLESS-2 left_pos=$pos_b
  right=HEADLESS-1 right_pos=$pos_a
fi
echo "arrangement: $left at $left_pos, $right at $right_pos"

# The left monitor drops (a DP link loss) and comes back: both must return to the same arrangement.
"$UMBRIEL" output-destroy "$left" > /dev/null
wait_for_gone "$left"
"$UMBRIEL" output-create "$left" > /dev/null
wait_for_position "$left" "$left_pos"
wait_for_position "$right" "$right_pos"
echo "left monitor ($left) returned to $left_pos with $right still at $right_pos"

# The right monitor drops and comes back: same contract.
"$UMBRIEL" output-destroy "$right" > /dev/null
wait_for_gone "$right"
"$UMBRIEL" output-create "$right" > /dev/null
wait_for_position "$left" "$left_pos"
wait_for_position "$right" "$right_pos"
echo "right monitor ($right) returned to $right_pos with $left still at $left_pos"

echo "output reconnect restored the established arrangement both ways"
