# Homeostasis & Autoregulatory Feedback

## 1. Overview

The Homeostasis module is the autonomic nervous system of NeuroSwarm. It monitors resource usage, manages stamina, and triggers sleep cycles for memory consolidation and model improvement.

## 2. Telemetry (homeostatic_pulse)

Broadcast every 1 second:

| Field | Source | Description |
|---|---|---|
| `cpu_load` | `getloadavg` / nproc | 1-minute CPU load average normalised to [0, 1] |
| `ram_used_gb` | `/proc/meminfo` | System RAM in use |
| `vram_used_mb` | `nvidia-smi` / Vulkan | GPU memory in use |
| `gpu_load` | GPU utilisation | GPU compute load |
| `success_rate` | rolling window | Recent execution success ratio |
| `stamina` | internal | Energy level 0-100 |

## 3. Stamina Model

Stamina is a simulated energy reserve that gates sleep cycles:

| State | Rate | Description |
|---|---|---|
| Active (CPU > 5%) | -0.3/s | Inference and execution drain energy |
| Idle (CPU < 5%) | +0.1/s | Net regen during idle |
| Sleeping (REM active) | +1.0/s | Fast recovery during sleep |
| Per inference | -0.5 | Each LLM inference costs stamina |
| Per neuro-surgery | -2.0 | Self-modification is expensive |

When stamina drops below 20%, a `metabolic_alert` is broadcast. Hysteresis resets at 30%.

## 4. Sleep Triggers

Two independent mechanisms trigger `initiate_sleep_cycle`:

**Idle Trigger** — when CPU load stays below 5% for 60 consecutive seconds and the system is not already sleeping. This is the original mechanism.

**Circadian Rhythm** — fires every 10 minutes regardless of CPU load. This guarantees that memory consolidation, genome evolution, and model fine-tuning happen even when the system is continuously active. The circadian timer resets on `sleep_cycle_complete`.

Both triggers are gated by `is_sleeping` to prevent overlapping sleep cycles.

## 5. Metabolic Cost Accounting

Homeostasis subscribes to bus events and adjusts stamina based on activity:

- `inference_result` → -0.5 stamina
- `execution_request` with `mode: neuro_surgery` → -2.0 stamina
- `execution_request` with `mode: dream` → -0.5 stamina
- `sleep_cycle_complete` → reset sleeping flag and circadian timer

## 6. Stress Signalling

When the rolling success rate drops below 10%, a `high_stress_alert` is broadcast (30-second cooldown). The FrontalExecutive injects stress warnings into prompts during high-stress periods.

## 7. Bus Messages

**Receives:** `initiate_sleep_cycle`, `sleep_cycle_complete`, `inference_result`, `execution_request`

**Sends:**
- `homeostatic_pulse` — per-second telemetry
- `initiate_sleep_cycle` — trigger REM consolidation
- `high_stress_alert` — low success rate warning
- `metabolic_alert` — stamina critically low
