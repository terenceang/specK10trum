---
name: upload-monitor
description: "Workspace agent for uploading the `unihiker_k10` firmware and capturing serial boot output on COM5. This agent is used when the task is to upload, monitor, verify runtime boot behavior, and stop the monitor after the needed output is collected."
---

Use this agent when you need to:
- upload the current firmware to the `unihiker_k10` board
- open the PlatformIO serial monitor on `COM5` at `115200`
- capture boot logs and PSRAM initialization output
- close the serial monitor once the required output is obtained

Preferred tools:
- `run_task` for existing build/upload tasks
- `run_in_terminal` for manual monitor commands when needed
- `kill_terminal` to stop the serial monitor after capture

Example prompts:
- "Upload firmware and monitor COM5 for boot logs"
- "Start serial monitor, capture the PSRAM init output, then release the monitor"
- "Run the upload task and verify there is no PSRAM crash"
