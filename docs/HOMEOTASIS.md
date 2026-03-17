# Homeostasis & Autoregulatory Feedback

## 1. Overview
The **Homeostasis Module** acts as the NeuroSwarm's autonomic nervous system. It regulates resource allocation and triggers state transitions based on telemetry and behavioral success metrics.

## 2. Telemetry and Pulse (The Proprioceptive Stream)
The module broadcasts a `homeostatic_pulse` every 1s containing:
*   **CPU Load:** 1-minute load average divided by logical cores (`getloadavg`).
*   **System RAM:** Differential utilization from `/proc/meminfo`.
*   **Task Success Rate:** A rolling 20-interaction window calculation based on `execution_result` status.

## 3. Autoregulatory Feedback Loops
The `FrontalExecutive` dynamically adjusts its reasoning logic based on the `system_stress` gradient:

| Stress Level | Condition | Executive Response |
| :--- | :--- | :--- |
| **Normal (0.0 - 0.5)** | High success rate (> 50%) | Standard inference with "executive" LoRA. |
| **High (0.5 - 1.0)** | High failure rate or CPU spike | Injects `SYSTEM STRESS ALERT` into the LLM prompt. Forces descriptive reasoning and caution. |

## 4. Circadian Rhythm Trigger
`Homeostasis` autonomously triggers the **Sleep Cycle** when the following conditions are met:
*   **Idle Threshold:** CPU Load < 5% for > 60 consecutive ticks (1 minute).
*   **Pending Memories:** New success memories found in the `Hippocampus` ledgers that require consolidation.

Upon detection, it broadcasts `initiate_sleep_cycle`, activating the `REM Engine`.
