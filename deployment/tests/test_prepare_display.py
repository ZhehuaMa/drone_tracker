"""用替代系统调用验证启动边界，不关闭真实桌面或启动相机。"""
import importlib.util
from pathlib import Path
import stat
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import call, patch

spec = importlib.util.spec_from_file_location(
    "prepare_display", Path(__file__).resolve().parents[1] / "prepare-display.py"
)
display = importlib.util.module_from_spec(spec)
spec.loader.exec_module(display)
HEADER = "command pid dev master a uid magic\n"


class DisplayPreparationTests(unittest.TestCase):
    def test_stop_is_synchronous_and_state_is_checked(self):
        with patch.object(display, "systemctl", side_effect=["loaded", "", "inactive", "not-found"]) as ctl:
            display.stop_desktop()
        self.assertEqual(ctl.call_args_list, [
            call("show", "display-manager.service", "--property=LoadState", "--value"),
            call("stop", "display-manager.service"),
            call("show", "display-manager.service", "--property=ActiveState", "--value"),
            call("show", "lightdm.service", "--property=LoadState", "--value"),
        ])

    def test_failed_stop_aborts(self):
        with patch.object(display, "systemctl", side_effect=["loaded", subprocess.CalledProcessError(1, "stop")]):
            with self.assertRaises(subprocess.CalledProcessError):
                display.stop_desktop()

    def test_still_active_aborts(self):
        with patch.object(display, "systemctl", side_effect=["loaded", "", "active"]):
            with self.assertRaisesRegex(RuntimeError, "桌面未停止"):
                display.stop_desktop()

    def check_clients(self, contents, times):
        with patch.object(display.Path, "stat", return_value=SimpleNamespace(st_mode=stat.S_IFCHR, st_rdev=display.os.makedev(226, 0))), \
                patch.object(display.Path, "read_text", side_effect=contents), \
                patch.object(display.time, "monotonic", side_effect=times), \
                patch.object(display.time, "sleep"):
            display.wait_for_display("/dev/dri/card0")

    def test_waits_for_release(self):
        self.check_clients([HEADER + "Xorg 1 0 y y 0 0\n", HEADER], [0, 1])

    def test_occupied_timeout_aborts(self):
        with self.assertRaisesRegex(RuntimeError, "仍被占用"):
            self.check_clients([HEADER + "Xorg 1 0 y y 0 0\n"], [0, 5])

    def test_unreadable_or_invalid_evidence_aborts(self):
        with self.assertRaises(PermissionError):
            self.check_clients([PermissionError("clients")], [0])
        with self.assertRaisesRegex(RuntimeError, "无法解析"):
            self.check_clients([""], [0])

    def test_prepare_orders_stop_before_release_check(self):
        events = []
        config = 'drone_tracker_node:\n  ros__parameters:\n    osd:\n      enabled: true\n'
        with patch.object(display.os, "geteuid", return_value=0), \
                patch.object(display.Path, "read_text", return_value=config), \
                patch.object(display, "stop_desktop", side_effect=lambda: events.append("stop")), \
                patch.object(display, "wait_for_display", side_effect=lambda device: events.append(device)):
            display.prepare("camera.yaml")
        self.assertEqual(events, ["stop", "/dev/dri/card0"])


if __name__ == "__main__":
    unittest.main()
