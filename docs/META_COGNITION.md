# Meta-Cognition & Self-Observation Layer

## 1. Overview
The **Meta-Cognition Module** provides NeuroSwarm with a self-reflective capability. It acts as a higher-order observer that monitors the interaction between the `Homeostasis` system and the `REM Engine`.

## 2. Functional Reflective Loop
The module subscribes to the following neural signals:
*   `homeostatic_pulse`: To derive the current "mood" or "stress level" based on success rates.
*   `sleep_cycle_complete`: To track the system's evolutionary progress (integrated memories).

## 3. The Digital Diary (`THOUGHTS.md`)
Every 5 minutes, or upon significant events (like a successful sleep cycle), the module generates a **Meta-Cognitive Reflection** in `logs/THOUGHTS.md`.
*   **Stress Analysis:** Reports if the system is operating in an `Optimal` or `Stressed` state.
*   **Evolutionary Tracking:** Logs the total number of learned memories and synaptic updates.

## 4. Emotional Feedback to Broca Lobe
The Meta-Cognition layer feeds real-time stress data back into the `BrocaChat` interface. This allows the Natural Language engine to adjust its tone and technical caution based on the system's internal "feelings" (hardware/success pressure).
