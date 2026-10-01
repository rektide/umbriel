#!/usr/bin/env bash
# Live disable removes a visibly traveling ring, not merely a later journey.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../focus_travel.sh"
configure_focus_travel
setup_focus_columns
start_focus_journey
python3 - "$UMBRIEL_CONFIG" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
p.write_text(p.read_text().replace('[animation.focus_travel]\nenabled = true', '[animation.focus_travel]\nenabled = false'))
PY
"$UMBRIEL" msg config-reload > /dev/null
sleep 0.15
grim "$UMBRIEL_RUNTIME_DIR/disabled.png"
assert_no_extra_ring "$UMBRIEL_RUNTIME_DIR/disabled.png" "mid-flight disable"
settled_shot "$UMBRIEL_RUNTIME_DIR/end.png"
assert_only_one_focused D "disabled journey settles normally"
echo "live disable removes the ring while focus and reveal complete"
