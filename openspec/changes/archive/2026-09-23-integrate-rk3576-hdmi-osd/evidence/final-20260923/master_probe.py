"""仅查询生产板显示控制权，不提交图像，不停止桌面。"""
import ctypes
import os
from pathlib import Path

library = ctypes.CDLL("libdrm.so.2", use_errno=True)
descriptor = os.open("/dev/dri/card0", os.O_RDWR | os.O_CLOEXEC)
try:
    print("drmIsMaster", library.drmIsMaster(descriptor))
    print("drmSetMaster", library.drmSetMaster(descriptor), "errno", ctypes.get_errno())
    print(Path("/sys/kernel/debug/dri/0/clients").read_text())
finally:
    os.close(descriptor)
