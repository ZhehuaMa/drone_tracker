"""检查两个 RK3576 入口选择同一合法 OSD 渲染路径。"""
import sys
from pathlib import Path
import yaml

root = Path(sys.argv[1])
paths = [root / 'parameters' / name for name in ('rk3576_camera_params.yaml', 'rk3576_video_params.yaml')]
values = [yaml.safe_load(path.read_text())['drone_tracker_node']['ros__parameters']['osd']['render_path']
          for path in paths]
assert values[0] == values[1], values
assert values[0] == 'cached-overlay', values
print('RK3576 camera/video render defaults agree:', values[0])
