"""检查两份相机模板和视频模板的 OSD 渲染路径。"""
import sys
from pathlib import Path
import yaml

root = Path(sys.argv[1])
paths = [root / 'parameters' / name for name in (
    'rk3576_ar0234_camera_params.yaml',
    'rk3576_sc233hgs_production_params.yaml',
    'rk3576_video_params.yaml')]
values = [yaml.safe_load(path.read_text())['drone_tracker_node']['ros__parameters']['osd']['render_path']
          for path in paths]
assert values == ['cached-overlay'] * len(paths), values
print('RK3576 camera/video render defaults agree:', values[0])
