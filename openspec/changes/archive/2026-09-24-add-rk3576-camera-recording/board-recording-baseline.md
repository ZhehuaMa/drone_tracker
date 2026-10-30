# Production-board recording dependency baseline

Checked on 2026-09-23 against the production board selected by SSH alias `firefly-production`, hostname `hzhy`. This inspection queried devices, packages, and file fingerprints. It did not start capture, switch the production release, or replace system libraries. Raw results are in [board-baseline.txt](evidence/board-baseline.txt).

| Item | Observed result |
|---|---|
| Board | HZ-RK3576_WuKong Board |
| Kernel | 6.1.141-rt36, ARM64 |
| Camera | SC233HGS, entity `m00_b_sc233hgs 0-0030`; ISP output `/dev/video22`, media device `/dev/media2` |
| Image format | 1920×1200 NV12, one memory plane, 1920-byte row stride, 3456000 bytes total |
| Color metadata | Colorspace/Transfer/YCbCr: Default; Quantization: Full Range. Default metadata does not establish a confirmed color matrix |
| Frame rate | Production configuration and sensor topology specify 60 fps. The capture node does not support `VIDIOC_G_PARM`. Actual dequeue intervals were not remeasured during this inspection |
| MPP packages | `librockchip-mpp-dev` and `librockchip-mpp1`, both `1.5.0-1` |
| MPP pkg-config | `rockchip_mpp` reports `1.3.9`, different from the Debian package version. This difference alone does not establish API incompatibility |
| MPP headers | `/usr/include/rockchip/`; the raw results record SHA-256 for every header |
| MPP library | `/usr/lib/aarch64-linux-gnu/librockchip_mpp.so.1`; SHA-256 `f03882324fddb343ff714e77b1fe37587ab70a42d3e40057327e5fa9afa46261` |
| Encoder device | `/dev/mpp_service`, owner/group root:video, permissions 0660 |
| Container-format library | `libavformat-dev` package `7:4.4.2-0ubuntu0.22.04.1`, pkg-config version `58.76.100` |
| Persistent mount | `/var/lib` is on the ext4 root partition `/dev/mmcblk0p7`. About 47 GiB was available during inspection; recheck at runtime |
| RKNN | System Runtime 2.3.2 baseline; `/usr/lib/librknnrt.so` SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`, matching the accepted version |
| RKNPU | Driver version read during this inspection: 0.9.8 |

MPP is the hardware encoding interface. Headers and a shared library permit compilation checks; their presence does not prove API compatibility or sufficient encoding throughput. SHA-256 identifies file content so later acceptance can use the same dependencies.

The camera-warmup prerequisite is archived. Its [verification report](../2026-09-23-fix-sc233hgs-startup-timestamp-warmup/verification.md) records independent production-board camera acceptance. That evidence does not replace recording validation.

Not yet validated at this baseline: actual MPP encoder settings and readback, full-rate 1920×1200 throughput, input alignment, color-metadata propagation, encoding memory, recording synchronization and recovery, concurrent tracking/OSD performance, and image quality in the three specified scenarios. No power-loss or destructive test was performed; this inspection makes no maximum-loss guarantee for real power failure.
