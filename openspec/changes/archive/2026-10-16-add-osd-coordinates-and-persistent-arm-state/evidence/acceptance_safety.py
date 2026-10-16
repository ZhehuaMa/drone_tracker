"""Diagnostics for private board acceptance; never changes target thermal policy."""
import json
import os
import signal
import subprocess
import time
from pathlib import Path


# This test guard is deliberately below the board's 115 C protection shutdown.
# Reaching it fails acceptance; it never counts as a shortened passing run.
ABORT_MILLICELSIUS = 95000


class AcceptanceAborted(RuntimeError):
    pass


class Diagnostics:
    def __init__(self, path, thermal_root=Path('/sys/class/thermal')):
        self.path = Path(path)
        self.thermal_root = Path(thermal_root)
        self.last_sample = 0.0

    def record(self, event, **values):
        row = dict(event=event, mono=time.monotonic(), wall=time.time(), **values)
        # One independently parseable, flushed record survives an interrupted run.
        with self.path.open('a') as output:
            output.write(json.dumps(row) + '\n')
            output.flush()
            os.fsync(output.fileno())

    def check(self, sample=None, force=False):
        if not force and time.monotonic() - self.last_sample < 1.0:
            return
        self.last_sample = time.monotonic()
        temps = {}
        try:
            for path in self.thermal_root.glob('thermal_zone*/temp'):
                temps[str(path)] = int(path.read_text().strip())
            if not temps:
                raise ValueError('no readable thermal sensors')
        except (OSError, ValueError) as error:
            self.record('thermal_monitor_failed', error=str(error))
            raise AcceptanceAborted('thermal monitoring failed') from error
        row = dict(temps=temps, abort_millicelsius=ABORT_MILLICELSIUS)
        if sample is not None:
            try:
                row['resources'] = sample()
            except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
                row['resource_error'] = str(error)
        self.record('checkpoint', **row)
        if max(temps.values()) >= ABORT_MILLICELSIUS:
            self.record('thermal_abort', maximum_millicelsius=max(temps.values()),
                        acceptance_completed=False)
            raise AcceptanceAborted('95 C diagnostic guard reached; acceptance incomplete')

    def install_signal_handlers(self):
        def abort(signum, _frame):
            self.record('signal_abort', signal=signum, acceptance_completed=False)
            raise AcceptanceAborted(f'acceptance interrupted by signal {signum}')
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            signal.signal(signum, abort)


def stop_owned_process(process, diagnostics, timeout=30):
    """Stop only our Popen child, escalating after bounded graceful shutdown."""
    if process is None or process.poll() is not None:
        return
    diagnostics.record('node_stop', pid=process.pid)
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        diagnostics.record('node_stop_timeout', pid=process.pid)
        # Never use pkill/killall or a process group that may contain other jobs.
        process.kill()
        process.wait(timeout=10)
        raise AcceptanceAborted('node stop timeout; owned child killed')


def cleanup_and_restore(process, diagnostics, restore, verify):
    """Restoration is attempted even if child shutdown or invariant checks fail."""
    errors = []
    started = time.monotonic()
    try:
        stop_owned_process(process, diagnostics)
    except BaseException as error:
        errors.append(f'node cleanup: {error}')
    finally:
        try:
            restore()
        except BaseException as error:
            errors.append(f'service restoration: {error}')
        finally:
            try:
                evidence = verify()
            except BaseException as error:
                errors.append(f'restoration verification: {error}')
                evidence = {}
            diagnostics.record('restore', errors=errors, evidence=evidence,
                               acceptance_completed=False if errors else None,
                               process_stop_seconds=time.monotonic() - started,
                               node_exit_code=None if process is None else process.poll())
    if errors:
        raise AcceptanceAborted('; '.join(errors))
    return evidence


def service_states():
    result = {}
    for service in ('drone-tracker', 'apm-bridge', 'drone-interceptor', 'lightdm'):
        reply = subprocess.run(['systemctl', 'is-active', service + '.service'],
                               capture_output=True, text=True, timeout=15)
        result[service] = reply.stdout.strip()
    return result
