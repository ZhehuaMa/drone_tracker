"""复用同目录 boundaries.py 的 ROS 测试辅助函数，延长恢复播放观察窗口。"""
from pathlib import Path

helpers = Path(__file__).with_name('boundaries.py').read_text().split('\ntry:\n', 1)[0]
exec(compile(helpers, 'boundaries.py', 'exec'))
video = R / 'observed-recovery-input.avi'
source = Path('/home/hzhy/drone-tracker-osd-20260923/synthetic-640x480-20fps.avi')
assert source.is_file()
assert not video.exists() and not video.is_symlink()
try:
    video.symlink_to(source)
    with log.open('w') as output:
        p = subprocess.Popen([
            str(R / 'drone_tracker_node'), '--ros-args', '--params-file',
            '/home/hzhy/drone-tracker-osd-20260923/synthetic.yaml',
            '-p', 'video_source:=' + str(video)
        ], stdout=output, stderr=subprocess.STDOUT)
    wait(lambda: trigger.get_subscription_count() > 0)
    spin(1)
    events.clear()
    command('ON')
    wait(lambda: bool(boxes))
    print('INITIAL LIVE: 10 SECONDS', flush=True)
    spin(10)
    command('OFF')
    wait(lambda: any(text == 'Drone Tracker Stopped' for _, text in events))
    video.unlink()
    events.clear()
    command('ON')
    wait(lambda: 'Round failed:' in log.read_text())
    wait(lambda: any(text == 'Drone Tracker Stopped' for _, text in events))
    print('INPUT FAILURE: FROZEN ERROR FOR 30 SECONDS', flush=True)
    result['events'].append({'case': 'missing input', 'resource': resource()})
    spin(30)
    video.symlink_to(source)
    for phase in ('RECOVERY AFTER INPUT FAILURE', 'NEW ROUND AFTER OFF'):
        events.clear()
        boxes.clear()
        command('ON')
        wait(lambda: bool(boxes))
        print(phase + ': LIVE FOR 30 SECONDS', flush=True)
        spin(30)
        command('OFF')
        wait(lambda: any(text == 'Drone Tracker Stopped' for _, text in events))
        result['events'].append({'case': phase, 'frames': len(boxes), 'resource': resource()})
        print('OFF: FROZEN FOR 30 SECONDS', flush=True)
        spin(30)
    p.send_signal(signal.SIGINT)
    p.wait(timeout=20)
    result['exit_code'] = p.returncode
    assert p.returncode == 0
except Exception as error:
    result['error'] = repr(error)
    raise
finally:
    if p is not None and p.poll() is None:
        p.send_signal(signal.SIGINT)
        p.wait(timeout=20)
    if video.is_symlink():
        video.unlink()
    (R / (mode + '-result.json')).write_text(json.dumps(result, indent=2))
    n.destroy_node()
    rclpy.shutdown()
