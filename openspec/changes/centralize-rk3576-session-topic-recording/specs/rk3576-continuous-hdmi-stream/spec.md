# Spec Delta

## MODIFIED Requirements

### Requirement: 记录与 ROS 调试图像保持业务边界

Original-camera video recording SHALL remain controlled only by `recording.enabled` and ON/OFF session control. ROS bag recording SHALL retain the `need_save` round boundaries and follow `rk3576-session-topic-recording` for its fixed application catalog, timestamp rules, and sampling. Published `/fpv/tracker_image` messages SHALL be eligible for sampled bag recording only during an active session. If no image is admitted, the bag MUST NOT contain tracker_image messages or topic metadata. Automatic service preview MUST NOT open either recording session. `/fpv/tracker_image` SHALL remain published only when `visualized_result=true` and the current tracking round actually consumes an image. Preview MUST NOT generate that topic's messages or bag data. The recorder MUST NOT enable debug-image publication or restart acquisition to obtain recording images.

#### Scenario: 开启调试图像但未触发
- **WHEN** continuous preview starts with `visualized_result=true`, `need_save=true`, and `recording.enabled=true`, but no ON has established a round
- **THEN** HDMI updates while no tracking image messages, bag session, or original-camera video session are created

#### Scenario: 结束一轮后继续预览
- **WHEN** ON processing has generated recording data and debug images, and OFF is received
- **THEN** the current sessions close, new tracking image publication stops, and HDMI preview continues

#### Scenario: 调试图像不等于消息录像
- **WHEN** an active ON round has `visualized_result=true`, `need_save=true`, and `recording.enabled=true`
- **THEN** debug images can be published live, policy-selected original debug messages can enter the bag, and original-camera video remains independent; OFF closes each session at its defined boundary while HDMI preview continues
