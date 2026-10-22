#!/usr/bin/env bash
# The native allocation contract applies one whole group without moving focus,
# rejects stale/invalid writes before mutation, and reports target vs committed geometry.
set -euo pipefail

python3 - <<'PY'
import copy, json, os, socket, subprocess, time

path = os.path.join(os.environ['XDG_RUNTIME_DIR'], 'umbriel-' + os.environ['WAYLAND_DISPLAY'] + '.sock')
def request(value):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(5)
        connection.connect(path)
        connection.sendall((json.dumps(value) + '\n').encode())
        return json.loads(connection.makefile('rb').readline())
def ok(value):
    reply = request(value)
    assert 'ok' in reply, reply
    return reply['ok']
def action(value):
    return ok({'cmd': 'msg', 'arg': value})
def settled():
    subprocess.run([os.environ['UMBRIEL'], 'settle'], check=True, stdout=subprocess.DEVNULL)
def state():
    return ok({'cmd': 'layout-state'})
def group():
    return next(c for c in state()['columns'] if len(c['basis']['members']) == 3)
def wait_windows(count):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        windows = ok({'cmd': 'windows'})
        if len(windows) == count:
            settled()
            return
        time.sleep(.05)
    raise AssertionError('windows did not map')

for i in range(3):
    subprocess.Popen(['foot', '--title=allocation-' + str(i), 'sh', '-c', 'sleep 120'],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait_windows(i + 1)
    if i:
        action('window-consume-left')
        settled()

baseline = group()
assert baseline['basis']['supported'], baseline
active = state()['active']
assert active in [m['id'] for m in baseline['basis']['members']]
subscription = socket.socket(socket.AF_UNIX)
subscription.settimeout(5)
subscription.connect(path)
subscription.sendall(b'{"cmd":"subscribe","events":["layout-state"]}\n')
events = subscription.makefile('rb')
initial = json.loads(events.readline())
assert initial['event'] == 'layout-state' and initial['data']['version'] == 1

budget = baseline['row_budget']
tile = [budget * 6 // 10, budget * 2 // 10]
tile.append(budget - sum(tile))
content = [extent - member['title_bar_height'] for extent, member in zip(tile, baseline['basis']['members'])]
write = {'cmd': 'layout-allocate', 'target': active, 'expected': baseline['basis'],
         'expected_active': active, 'weights': {'rows': tile, 'top': baseline['top_gap'], 'bottom': baseline['bottom_gap']},
         'target_content_extents': content}
accepted = ok(write)
assert accepted['previous_weights'] == baseline['basis']['weights']
assert [g['target_content']['height'] for g in accepted['accepted']['geometry']] == content
assert state()['active'] == active, 'allocation moved focus'
settled()
assert [g['committed_content']['height'] for g in group()['geometry']] == content
while True:
    event = json.loads(events.readline())
    if any(c['basis'] == accepted['accepted']['basis'] for c in event['data']['columns']):
        break

# A stale baseline cannot overwrite our own accepted write.
assert request(write)['err'] == 'stale layout basis'
current = group()['basis']
for replacement in ({'rows': [1, -1, 1], 'top': 0, 'bottom': 0},
                    {'rows': [1], 'top': 0, 'bottom': 0}):
    bad = dict(write, expected=current, weights=replacement)
    assert 'err' in request(bad)
    assert group()['basis'] == current
bad = dict(write, expected=current, target_content_extents=[1, 1, 1])
assert request(bad)['err'] == 'target extents not representable'
assert group()['basis'] == current

# Same-pixel weight edits remain visible in the basis, so geometry cannot regain authority.
scaled = copy.deepcopy(current['weights'])
scaled['rows'] = [w * 2 for w in scaled['rows']]
scaled['top'] *= 2
scaled['bottom'] *= 2
scaled_result = ok(dict(write, expected=current, weights=scaled))
assert [g['target_content'] for g in scaled_result['accepted']['geometry']] == [g['target_content'] for g in accepted['accepted']['geometry']]
assert request(dict(write, expected=current))['err'] == 'stale layout basis'

# An emphasis intended for a formerly active member is rejected without changing allocation.
other = next(m['id'] for m in current['members'] if m['id'] != active)
action('window-focus:' + other)
settled()
current = group()['basis']
assert request(dict(write, expected=current))['err'] == 'active window changed'
assert group()['basis'] == current

# Restore by original native weights, against the last accepted/current basis, with no focus predicate.
restore = {'cmd': 'layout-allocate', 'target': active, 'expected': current, 'weights': baseline['basis']['weights']}
restored = ok(restore)
assert restored['accepted']['basis']['weights'] == baseline['basis']['weights']
assert state()['active'] == other
settled()
assert [g['target_content'] for g in group()['geometry']] == [g['target_content'] for g in baseline['geometry']]

# Topology changes invalidate a retained basis.
retained = group()['basis']
action('window-consume-or-expel-right')
settled()
assert 'err' in request(dict(restore, expected=retained))
subscription.close()
print('conditional layout allocation: passed')
PY
