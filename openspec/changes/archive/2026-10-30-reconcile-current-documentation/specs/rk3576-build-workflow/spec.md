# Spec Delta

## MODIFIED Requirements

### Requirement: Python tests execute on the architecture they exercise
A single SDK Python CTest SHALL run portable controller and native-loader checks using native Python. It SHALL run the ARM middleware identity case using genuine ARM Python. Discovery SHALL preserve every unique test ID exactly once across the two groups. Either group failing or skipping a case SHALL fail the CTest. The original aggregate 30-second deadline SHALL remain unchanged. ARM Python children SHALL reenter the selected emulator explicitly.

#### Scenario: Native supervisor and actual ARM middleware
- **WHEN** the Python SDK CTest discovers the current test set
- **THEN** it runs every portable case natively and the actual ARM middleware case on ARM Python, including its ctypes child, without relying on host emulator registration

#### Scenario: Classification drops or duplicates a test
- **WHEN** test discovery or partitioning loses an ID or produces a duplicate
- **THEN** the coordinator fails before reporting the SDK Python group as passed
