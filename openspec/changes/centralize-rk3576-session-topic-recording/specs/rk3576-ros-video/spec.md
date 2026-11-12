# Spec Delta

## MODIFIED Requirements

### Requirement: ROS bag 记录与关闭互斥

A ROS bag is a recording of ROS messages. With `need_save=true`, RK3576 SHALL record active-round messages according to `rk3576-session-topic-recording`, using its frozen application catalog and explicit sampling policies. Its recording inputs SHALL include `/fpv/gimbal`, `/fpv/odom`, and `/mavros/imu/data`. It SHALL record the `/fpv/target_bbox` and `/fpv/target_los` messages actually constructed for publication as a pair. Retained input messages SHALL preserve their resolved topic names, complete payloads, and valid source timestamps. Bbox and LOS SHALL preserve their shared timestamp. Published `/fpv/tracker_image` messages SHALL be eligible for sampled recording under that capability. When no image is admitted, a bag MUST NOT contain image topic metadata. `visualized_result` SHALL remain solely the switch for publishing tracking debug images. Recording MUST NOT enable debug-image publication. Video-file input SHALL NOT create camera-video recordings.

Disk writes SHALL be separate from ROS callbacks and tracking processing. Admission MUST NOT wait for disk writes. Retained recording work SHALL remain bounded by both 256 message slots and 64 MiB of accounted payload, including the writer's in-flight item. Each observation pair SHALL consume two slots and its combined payload allowance. Insufficient capacity SHALL reject the whole pair and increment each member's attempted and dropped counts. It MUST NOT accept only one member. Unexpected queue overflow SHALL mark the session incomplete. Intentional policy sampling SHALL be reported separately from unexpected loss. A mid-write disk failure can still leave only one pair member in the file; the recorder SHALL report failure or incompleteness rather than promise a file transaction.

Creation, admission, writing, closing, and destruction SHALL be synchronized. OFF, EOF, round-ending failures, and normal service exit SHALL close the current session. The authoritative OFF message and final invalid observation pair SHALL be offered before sealing. Callbacks after sealing MUST NOT access a destroyed writer or attach old-session work to a new session. Bag open, write, and close failures SHALL report the session's failure and original reason. Such failures MUST NOT stop otherwise healthy acquisition, tracking, HDMI, or independent camera-video recording. Existing recording directories MUST NOT be overwritten. Each session SHALL report accepted, written, intentionally omitted, unexpectedly dropped, and accepted-but-unwritten totals. Successful policy-complete close SHALL be distinguishable from incomplete, failed, and storage-limited results.

#### Scenario: EOF 与姿态回调并发
- **WHEN** image processing reaches EOF while an attitude callback is submitting recording work
- **THEN** the message is either accepted into the old session before sealing or rejected safely, without accessing a released writer or entering a later session

#### Scenario: 目录不可写或已存在
- **WHEN** ON needs a bag in an unwritable directory or would collide with existing evidence
- **THEN** the session reports an open failure, existing evidence is retained, and a healthy tracking round can still run

#### Scenario: bag 写入失败
- **WHEN** disk writing fails during a round with tracking and independent camera recording enabled
- **THEN** the bag reports the original failure, accepted, written, and unwritten counts; healthy tracking, HDMI, and camera recording continue

#### Scenario: 队列达到容量
- **WHEN** disk writing lags and retained recording work reaches a message or byte limit
- **THEN** submission does not wait for disk progress, unexpected drops are counted, and the session is marked incomplete while tracking and display continue

#### Scenario: 禁用调试图像
- **WHEN** `need_save=true` and `visualized_result=false` for an ON/OFF round that produces target observations
- **THEN** the readable closed bag includes retained observations and other received catalog topics, but contains no tracker_image messages or metadata

#### Scenario: bag 完整性回查
- **WHEN** a session closes after OFF or EOF without detected unexpected losses or write errors
- **THEN** a reader can reach the end of every split file, its message counts equal successful writes, retained timestamps and recognizable payloads match their inputs, and intentional sampling is explicitly reported

#### Scenario: 开启图像也不写入 bag
- **WHEN** `need_save=true`, `visualized_result=true`, and ON processing publishes images and receives body IMU
- **THEN** the bag can contain policy-selected original images and retained IMU, bbox, and LOS messages; recording changes neither image publication nor the shared observation reference time

#### Scenario: 仅余一个消息位置
- **WHEN** the available admission class has one remaining message slot and a bbox/LOS pair is offered
- **THEN** both members are rejected, both drop counts increase, and the queue remains within its limits

#### Scenario: 无记录请求
- **WHEN** `need_save=false`, body IMU arrives, and target observations are produced
- **THEN** observations retain their normal behavior, no bag is created, and `recording.enabled` independently controls original-camera video recording
