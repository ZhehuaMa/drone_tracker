# Ten paired diagnostic acceptance groups

Task 7.3 compares ten `off`/`basic` pairs of the same business binary on the same
identified production board. Odd groups run off first; even groups run basic first.
Each measurement window lasts 45 seconds after four seconds of warmup. Retain all
window samples; dividing a window does not create another independent group.

Each passing group must meet all limits: submission P99 <=20 us, complete-call P99
increase <=0.2 ms, consumed-frame-rate decrease <=1%, and no diagnostic health gaps.
P99 is sorted sample `ceil(0.99*N)`. At least eight groups must pass. Timeout,
insufficient samples or unconfirmed cleanup count as failures. Keep failed attempts
and complete batches; never assemble a passing batch from selected groups of older runs.

## Preparation

1. Build matching target installation artifacts using the [image guide](../../../dockerfiles/README.amd64.md).
   Save SHA-256 fingerprints of node, worker, inspector, video, configuration, models and
   scripts. A diagnostic hot-path change requires a fresh binary and complete batch.
2. Create a new isolated board directory containing the `drone_tracker/` installation,
   `fixed-balloon.avi`, empty `evidence/`, and `board_acceptance.py`,
   `analyze_board_acceptance.py`, `ten_round_window.py`, `ten_round_restore_guard.py`.
   Read-only links to verified artifacts are allowed. Preserve formal `current`,
   configuration, models and the system Runtime.
3. Set `DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT` to that absolute directory and
   `DRONE_DIAGNOSTIC_UNIT_PREFIX` to a unique batch prefix. Never reuse armed markers
   or output directories.
4. Match the services' environment during preflight. Source `/opt/ros/humble/setup.bash`
   and the isolated `share/drone_tracker/local_setup.bash`. Business uses
   `DRONE_TRACKER_CPU_CONFIG=/opt/drone_tracker/config/cpu-affinity.yaml`,
   `ROS_DOMAIN_ID=198`, `ROS_LOCALHOST_ONLY=1`. Controller and bag use CPUs 0-3;
   business retains its CPU 4-7 role policy. The installed diagnostic entry requires
   CPU 6 and exact `{6}` readback. The experimental bare-worker harness has its own
   explicit range; record it separately from installed-entry validation.
5. With formal services still running, perform a short isolated-video preflight with
   HDMI disabled. Check package discovery, actual model load, ON/OFF, policy parsing
   and role readback. A protocol-version query does not exercise that path. Preserve
   environment, effective YAML and output; verify formal service/device state,
   protected fingerprints and isolated cleanup afterward.

## Deadlines and restoration

Each group runs in an independent systemd cgroup, which contains new-session descendants
as well. `ten_round_window.py` sets `RuntimeMaxSec=140s`, `TimeoutStopSec=5s` and
`KillMode=control-group`. The complete group has a 150-second external budget covering
startup, warmup, both windows, switching, cleanup and read-only export.
`*-window.json` records actual timing, service state, cgroup and surviving processes.
Controller exit alone does not prove resource recovery.

1. Start `ten_round_restore_guard.py` as an independent service with the batch root
   and prefix. Confirm active/running and `evidence/guard-ready.json`.
2. Start `ten_round_window.py` in another independent service. Each privileged shell
   sources ROS first. The controller records formal identity/state and writes the
   armed monotonic origin before stopping authorized services.
3. The guard cleans batch units and restores `drone-tracker.service` on
   `restore-requested` or at second 1720. Restoration remains in `finally`, including
   cleanup failure. The 1800-second outer deadline includes restoration. SSH loss
   does not stop the guard, and retries cannot reset the origin.
4. After completion, failure or timeout, read back the original enabled/active state,
   HDMI ownership, `current`, configuration, models and Runtime fingerprints. Confirm
   no batch units or processes remain. Leave unrelated processes alone.

## Independent recomputation and delivery

After formal service restoration, run on the board:

```bash
source /opt/ros/humble/setup.bash
python3 analyze_board_acceptance.py "$DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT/evidence"
```

Each case's `measurement_start_ns` and `measurement_end_ns` define a half-open window.
Use every sample whose start lies within it. Call P99 increase is basic minus off.
Frame-rate decrease is `100*(1-basic_fps/off_fps)`; keep negative values. Consumption
counts come from actual consumed-call flags, rather than messages or event counts.

Deliver all ten groups' raw CSV, binary diagnostic logs, exports, bags, effective YAML,
slow samples, deadline results, artifact/input identities, restoration records and
independent analysis. Preserve failed startup/configuration attempts and missing or
unfinished groups explicitly. Mark task 7.3 complete only after at least eight groups
pass, then check progress with:

```bash
openspec instructions apply --change add-rk3576-tracking-diagnostic-process --json
```

`.docker-artifacts` and `/tmp` are disposable and can be cleared at any time. Copy any
required acceptance batch and its manifests into an explicitly retained external
location before cleanup. Historical paths do not guarantee surviving evidence.
The maintenance restore guard protects service recovery; it does not independently
prove that every deadline or entry-point check passed.
