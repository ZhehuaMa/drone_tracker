"""从生产板固定真实回放生成可重复的目标消失测试输入。"""
from pathlib import Path

import cv2
import numpy as np


root = Path('/home/hzhy/hdmi-hud-acceptance-20260924')
source = cv2.VideoCapture(str(root / 'fixed-drone-1920x1200-650s-60fps.avi'))
if not source.isOpened():
    raise RuntimeError('cannot open fixed real video')
fps = source.get(cv2.CAP_PROP_FPS)
width = int(source.get(cv2.CAP_PROP_FRAME_WIDTH))
height = int(source.get(cv2.CAP_PROP_FRAME_HEIGHT))
if not (59 <= fps <= 61 and (width, height) == (1920, 1200)):
    raise RuntimeError(f'unexpected input: {fps} fps, {width}x{height}')
destination = root / 'visual-loss-1920x1200.avi'
writer = cv2.VideoWriter(str(destination), cv2.VideoWriter_fourcc(*'MJPG'), fps,
                         (width, height))
if not writer.isOpened():
    raise RuntimeError('cannot create controlled video')
for index in range(12 * 60):
    ok, frame = source.read()
    if not ok:
        raise RuntimeError(f'input ended at frame {index}')
    writer.write(frame)
blank = np.zeros((height, width, 3), dtype=np.uint8)
for _ in range(6 * 60):
    writer.write(blank)
writer.release()
source.release()
print(f'{destination}: 12 s real frames + 6 s blank, 60 fps, {width}x{height}')
