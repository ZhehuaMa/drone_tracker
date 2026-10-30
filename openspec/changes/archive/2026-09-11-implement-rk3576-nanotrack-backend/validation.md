# NanoTrack implementation validation record

Validation date: 2026-09-11. The independent-entry acceptance prescribed by this change was complete, 29/29 tasks. Archival had not yet been performed when this report was written; the document now resides in the archive.

## Final validation state of this change

The table records the final conclusion at that validation date. Later historical sections preserve pre-fix commands, model identities, and failure diagnostics rather than current blockers.

| Task/scope | State and evidence |
|---|---|
| 5.4 ARM64 build and contract checks | Complete; verification rebuilt and passed 16/16 CTests. See [verification report](verification.md) |
| 6.1 Four combinations, submission attributes, and normalization responsibility | Complete; query exited 0 for every combination. See [FP16 fix and rerun](fp16-fix.md) |
| 6.2 Fixed floating-input numerical comparison | Complete; maximum absolute difference was 0 for each group's 50 comparisons, with absolute/relative tolerances both 1e-5. See [numerical validation](numeric-validation.md) |
| 6.3 Real YOLO before/after extraction regression | Complete; 309-frame results, fixed-image raw outputs, and five failure categories matched. See section 6.3 |
| 6.4 Four-precision grayscale replay | Complete; each group had 309 frames and 308 searches. Quality and duration are reported separately in [four-combination replay](fp16-fix.md) |
| 6.5 NanoTrack and asynchronous YOLO coexistence | Limited hardware check complete; all INT8, 20 rounds, about 18 seconds, without observed sustained memory growth or stop failure. See section 6.5 |
| 6.6 Evidence summary | Complete; this report and linked reports record versions, models, inputs, tolerances, and unexecuted scope |
| Jetson comparison | Not run; matching local hardware and SDKs were unavailable |
| Complete RK3576 platform, camera, and ROS acceptance | Outside this independent-entry change; incomplete at this validation date |
| Long-term stability and deployment performance | Not run; short coexistence and one video cannot replace them |

## Platform and build evidence

The target was RK3576 ARM64, Linux 6.1.141, RKNN Runtime `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`, RKNPU driver `0.9.8`. Runtime is the host library used to call the NPU. Compatibility baseline was Runtime 2.3.2/driver 0.9.8. At the user's request, this run retained the existing 2.3.0 build SDK, meaning compiler headers and linked libraries, without upgrading or rebuilding the image. Its output completed the recorded four-combination fixed-input comparison on the target versions. This limited result does not establish compatibility for arbitrary SDK versions.

Early acceptance read the prior 16/16 result in `.docker-artifacts/nanotrack/ctest-final.log` without rerunning it then. Subsequent verification rebuilt and ran CTest: 16/16 passed, covering sessions, NanoTrack, preprocessing, YOLO, and platform selection. That run's log path was `.docker-artifacts/build/nanotrack-contracts/Testing/Temporary/LastTest.log`. Injected SDK returns validate program logic, not real-model execution. `.docker-artifacts/` is disposable; these historical paths do not guarantee continuing availability.

## History: pre-fix commands and inputs

The target was reached through `ssh firefly`. Two probes from `.docker-artifacts/build/nanotrack-contracts/` were copied into new board directory `/home/firefly/drone_tracker_test/nanotrack_acceptance_20260911/`, retaining the old YOLO baseline. Commands executed on the board were:

```sh
cd /home/firefly/drone_tracker_test/nanotrack_acceptance_20260911
m=/home/firefly/benchmarks/nanotrackv3_evaluation_runs/nanotrackv3-official-20260910/models/components/fp16
./rk3576_nanotrack_probe --template-model "$m/template_backbone.rknn" \
  --search-model "$m/search_backbone.rknn" --head-model "$m/head.rknn" \
  --mode query > query-fp16.log 2>&1
sha256sum rk3576_nanotrack_probe rk3576_yolo_probe "$m"/*.rknn
```

Models came from the existing official evaluation directory. The first FP16 attempt queried only attributes. All-INT8 queries, grayscale replay, and coexistence follow below. SHA-256 identifies file content for reproduction, not an acceptance whitelist.

| File | SHA-256 |
|---|---|
| rk3576_nanotrack_probe | `edee9260aee8e3241f90d544b33e0d1862f4e5dc0597419903ebdf14f32d4945` |
| rk3576_yolo_probe | `03a7fbd13e2ca7696c1461d8e6ceae526eccc09d5cf1b638c10a202347e3258f` |
| template_backbone.rknn | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| search_backbone.rknn | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| head.rknn | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

## History: incorrect FP16 rejection and original blocker

This section concerns the pre-fix executable. The issue was repaired and successfully rerun; see [FP16 fix](fp16-fix.md).

Query exited 1 with `Unsupported NanoTrack quantization [input#0] expected=unquantized Float32/FP16 or affine INT8`. The original board log was `query-fp16.log` in the directory above.

| Template tensor | Actual attributes |
|---|---|
| Input #0 | `[1,127,127,3]`, FP16, NHWC, AFFINE, zp=0, scale=1, 48387 elements, 96774 bytes, w_stride=128, size_with_stride=97536 |
| Output #0 | `[1,96,8,8]`, FP16, NCHW, AFFINE, zp=0, scale=1, 6144 elements, 12288 bytes, size_with_stride=12288 |

NHWC/NCHW specify tensor-axis order. AFFINE is the SDK's affine-quantization metadata flag. The original `rknn_nanotrack_contract.cpp` rejected all non-INT8 tensors carrying quantization metadata, so it stopped at template input validation. Later investigation removed that redundant check and added floating-metadata regressions without changing the build SDK or models.

## History: pre-fix task state and tolerances

This table preserves the early snapshot and evidence boundaries. The final state of this validation is the opening table.

| Task/scope | State and evidence limit |
|---|---|
| 6.1 Four combinations, submission attributes, and normalization responsibility | Partial; all-INT8 attributes and roles passed, while the other three combinations were blocked by FP16 validation |
| 6.2 Fixed floating-input comparison | Not run; repeated-execution error was unmeasured and numerical tolerances were not yet fixed |
| 6.3 Real YOLO before/after extraction regression | Complete; 309-frame results, fixed-image raw outputs, and five failure categories matched |
| 6.4 Four-precision grayscale replay | Partial; all INT8 ran on the same labeled video; the other three groups were blocked |
| 6.5 NanoTrack/asynchronous YOLO coexistence | Limited hardware check complete; all INT8, 20 rounds, about 18 seconds, without sustained memory growth or stop failure |
| Jetson comparison | Not run; matching local hardware and SDKs were unavailable |
| Complete RK3576 platform and ROS acceptance | Outside this independent-entry scope and incomplete then |

Existing color evaluation was not substituted for grayscale validation. Public thresholds were unchanged and model loading/building was not treated as proof of correct inference. Independent checks continued under the user's request to skip blocked items temporarily. Tasks 6.1, 6.2, and 6.4 stayed unchecked until complete evidence arrived; completion conditions were not reduced.

## History: pre-fix four-combination queries

Full diagnostics are in [evidence](evidence/). Model directories remain relative to the above `models/components`:

| Backbone / Head | Subdirectories | Result |
|---|---|---|
| FP16 / FP16 | Both `fp16` | Exit 1; template FP16 attributes rejected |
| INT8 / INT8 | Both `int8/rknn_int8` | Exit 0; all roles matched |
| FP16 / INT8 | `fp16`, `int8/rknn_fp16_backbone_int8_head` | Exit 1; template FP16 attributes rejected |
| INT8 / FP16 | `int8/rknn_int8`, `fp16` | Exit 1; Head FP16 attributes rejected |

Both all-INT8 image inputs were NHWC, zp=-128, scale=1. Template output was NCHW, zp=7, scale=0.103884; Head template input was NHWC, zp=6, scale=0.100217. Search output and Head search input both had zp=11, scale=0.125591. The differing quantization parameters make floating feature transfer necessary for that historical path. Submission used Float32 in native logical order with `pass_through=0`; output requested `want_float=1` so the SDK returned floats. Code added neither normalization nor dequantization. Task 6.2 had not yet run, so these attributes alone did not prove numerical correctness. Subsequent independent comparisons are in [numerical validation](numeric-validation.md).

All-INT8 model SHA-256 values:

| File | SHA-256 |
|---|---|
| template_backbone.rknn | `848ec40a659b5a3484c0d76ba6ded353250f524c7f384fec95a0f85cf2682bdf` |
| search_backbone.rknn | `ecabaefdeadba721b466d942f76195bb9a5184e8e0a7d2a8262cc2d86e38ccdd` |
| head.rknn | `f4fc6259eaeb83860515773df49391c860a3be31507f613b2c15fcde6430e736` |

## 6.3 YOLO before/after extraction regression

Both existing programs ran on Runtime 2.3.2/driver 0.9.8. The pre-extraction executable was board `yolo_acceptance_20260910/rk3576_yolo_probe`, SHA-256 `b794d3c11b4efb71a62103ab18e3f227cacfe8d90904859d3f828d7aea2daa55`. Model `yolov8n-model_zoo-int8_fp16-multi_cls.rknn` had SHA-256 `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3`.

Original video `V_DRONE_051.mp4` was 640×512, 309 frames, 30 FPS, SHA-256 `01c49506366d2b8f273df4951bc8546e2f0b5920203f815589950d90aa5b53d7`. Fixed image `video-frame-1.png` had SHA-256 `bd3783fca65e01383666dcbf44c466f3f4bd584364bac3aa83f8e5ebecdafa93`. See [prior board results](../2026-09-11-implement-rk3576-yolo-backend/board-validation.md) for input provenance.

Both ran `--model MODEL --mode detect --target 2 --video VIDEO` and exited 0. The local [yolo_regression.py](yolo_regression.py) then compared logs and image/failure inputs. The board had no conda, so comparison used `/usr/bin/python3` and its standard library.

- All 309 hit flags, classes, score strings, and boxes matched exactly: 159 hits and 150 misses on each side.
- Fixed-image input, six Float32 raw outputs, and merged prediction matched byte-for-byte; maximum numerical difference was 0.
- Missing model, missing image, out-of-range target class, invalid ROI, and accidentally supplying a two-input Head model all exited 1 without a successful frame. Diagnostics could add tensor detail; complete error text need not match.
- Exact byte/result-text equality was stricter than prior raw-output `atol=1e-5, rtol=1e-5` and display-box `atol=1e-3, rtol=1e-5`; no tolerance was relaxed.

See [yolo-regression.json](evidence/yolo-regression.json), `evidence/old-video.log`, and `evidence/new-video.log`. No driver was deliberately damaged to create a real device fault. Existing contract checks retained device-failure injection.

## 6.4 History: first all-INT8 grayscale replay

These are the initial all-INT8 results and measurement rules. Later same-condition four-combination comparison completed in [four-combination replay](fp16-fix.md).

The same original video used dataset first-frame label `labels/test/V_DRONE_051_frame_000001.txt`, containing `2 0.738281 0.556641 0.032813 0.039062`. Fields are class, normalized center, width, and height. At 640×512, the initial box is `461.99968,275.00032,21.00032,19.999744`. Later labels match original frame numbers; the dataset starts at 1 and the probe at 0. Every subsequent frame, 308 total, had one unique class-2 label. Detector results were not used as ground truth.

```sh
cd /home/firefly/drone_tracker_test/nanotrack_acceptance_20260911
m=/home/firefly/benchmarks/nanotrackv3_evaluation_runs/nanotrackv3-official-20260910/models/components/int8/rknn_int8
timeout 120 ./rk3576_nanotrack_probe --template-model "$m/template_backbone.rknn" \
  --search-model "$m/search_backbone.rknn" --head-model "$m/head.rknn" --mode replay \
  --video /home/firefly/drone_tracker_test/yolo_acceptance_20260910/V_DRONE_051.mp4 \
  --bbox 461.99968,275.00032,21.00032,19.999744 > replay-int8.csv 2> replay-int8.log
/usr/bin/python3 summarize_replay.py "$PWD"
```

Exit was 0. Color frames were explicitly converted to grayscale, with default public algorithm settings. Statistics exclude template initialization. There was no reset, redetection, or later correction from ground truth. Labels refer to original dataset frame names. JPEG and original-video decoding may differ through compression, but coordinates use the same 640×512 image space.

| Metric | All-INT8 result |
|---|---|
| Searches returning a valid box | 308/308 |
| Mean IoU, intersection area divided by union area of prediction and label | 0.315898 |
| Mean center error | 2.34935 pixels |
| Mean confidence | 0.997392 |
| Frames without any label overlap | 0/308 |
| Last-frame IoU / center error | 0.091477 / 1.34885 pixels |
| Mean / P95 duration | 10.9467 / 11.354 ms |

P95 is sorted sample `ceil(0.95*N)`, covering 95% of search frames. Duration includes grayscale conversion, preprocessing, and update, excluding video decoding. The YOLO regression and coexistence program were not running concurrently. High confidence alongside low overlap shows why confidence is not accuracy. Zero-overlap counts and last-frame error describe drift indicators, not an approved quality threshold. See [summary](evidence/replay-int8-summary.json) for raw CSV, per-frame errors, and all label hashes; the calculator is [summarize_replay.py](summarize_replay.py). The other three combinations had not been replayed at this stage.

## 6.5 Coexistence, resources and stopping

[board_coexistence.cpp](board_coexistence.cpp) is an independent acceptance program linked to existing ARM64 libraries. It changed neither production algorithms nor Runtime. Compilation used the established temporary container with `--rm` and the existing 2.3.0 SDK. Program SHA-256 was `9487a5db7990d2e1991b3d29de4e801616fa5fbe76ec0611f7faf32834211fff`. A local analysis database ran clang-tidy `clang-analyzer-*` on the new translation unit without diagnostics; clangd reported 0 errors. Historical logs were under `.docker-artifacts/nanotrack/coexistence/`.

Historical compilation from the `nanotrack-contracts` build directory used the command below. `repo` is the container repository path. `-iquote` prevents the platform header with the same name from hiding SDK `<rknn_api.h>`:

```sh
repo=/work/ros2_ws/src/drone_tracker
c++ -std=c++17 -O2 -I"$repo/src" -I"$repo/src/hybrid_tracker" \
  -iquote "$repo/src/platform/rk3576" -I"$repo/.docker-artifacts/rknn/include" \
  -I/usr/include/opencv4 "$repo/openspec/changes/implement-rk3576-nanotrack-backend/board_coexistence.cpp" \
  -o board_coexistence librk3576_yolo_sdk.a librk3576_yolo_runtime.a librk3576_yolo_host.a \
  common/libhybrid_tracker.a common/libmodel_common.a common/libpreprocess_geometry.a \
  "$repo/.docker-artifacts/rknn/lib/librknnrt.so" -lopencv_video -lopencv_dnn \
  -lopencv_videoio -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -pthread
```

The board run used the above three all-INT8 files, same YOLO model, and `video-frame-1.png`:

```sh
timeout 240 ./board_coexistence "$m/template_backbone.rknn" "$m/search_backbone.rknn" \
  "$m/head.rknn" /home/firefly/drone_tracker_test/models/yolov8n-model_zoo-int8_fp16-multi_cls.rknn \
  /home/firefly/drone_tracker_test/yolo_acceptance_20260910/video-frame-1.png > coexistence.log 2>&1
```

It exited 0 after 20 rounds in 18.1154 seconds. Each round reloaded four independent sessions, repeatedly reset twice, initialized templates, submitted asynchronous YOLO, searched NanoTrack synchronously, and read YOLO results. Totals were 200 template initializations, 200 searches, and 200 YOLO results. Each round submitted one additional detection, observed entry into real `rknn_run`, and called stop twice. All 20 waits joined the worker; `run` had returned when stop returned. NanoTrack calls ran synchronously only on the main thread. Objects were destroyed after calls ended, without concurrent calls into one session.

Initial VmRSS was 51524 kB; round 1 was 60976 kB; rounds 2–20 were 61160–61592 kB; final was 61508 kB. VmRSS is currently resident physical process memory. After each destruction, thread count was 1 and descriptors were 6 versus initial 5, without sustained growth. See [coexistence.log](evidence/coexistence.log). This bounded load does not establish indefinite leak freedom and did not measure NPU device allocations directly.
