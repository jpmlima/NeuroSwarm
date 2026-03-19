# BasalGanglia Lobe: Intrinsic Motivation Engine

## 1. Overview
The **BasalGanglia** lobe implements intrinsic motivation for NeuroSwarm. Rather than relying on human-authored task lists or LLM-generated goals, it maintains a **self-model** of the system's capabilities and uses a **fitness function** inspired by Karl Friston's Free Energy Principle to select the most informative domain to explore next.

**Biological analogue:** Basal ganglia (action selection via reward prediction) + cerebellum (prediction error computation) + VTA (dopaminergic reward signal).

## 2. Self-Model (`data/self_model.json`)

The self-model tracks 14 fixed capability domains:

| Domain | Description |
|---|---|
| `file_read` | Reading file contents, metadata, line counts |
| `file_write` | Creating, copying, appending to files |
| `file_search` | Finding files by name, content, or pattern |
| `process_inspection` | Listing, querying, managing OS processes |
| `network_diagnostics` | Socket inspection, port checking, HTTP probes |
| `source_modification` | Analyzing or patching source code |
| `compilation` | Building targets via cmake/make/gcc |
| `git_operations` | Version control: log, status, diff, commit |
| `system_monitoring` | Uptime, memory, disk, CPU telemetry |
| `data_analysis` | Parsing metrics, sorting, counting, aggregating |
| `script_creation` | Writing and executing shell scripts |
| `self_inspection` | Examining NeuroSwarm's own state and structure |
| `memory_analysis` | Inspecting engrams, memory index, behavioural knowledge |
| `log_analysis` | Reading progress logs, metrics files, system events |

Each domain tracks:
```json
{
  "success": 12,
  "failure": 2,
  "last_attempt_ts": 1773800000,
  "predicted_success_rate": 0.85,
  "actual_success_rate": 0.857,
  "prediction_error": 0.007,
  "example_commands": ["cat src/brainstem/Thalamus.cpp | head -50"],
  "novelty_score": 0.1,
  "consecutive_failures": 0,
  "cooldown_until": 0
}
```

## 3. Fitness Function

The fitness function determines which domain the system should explore next:

```
F(d) = 0.20 * Coverage(d) + 0.15 * Trend(d) + 0.30 * PredictionError(d) + 0.25 * Novelty(d) - 0.10 * Stress
```

| Term | Weight | Formula | Purpose |
|---|---|---|---|
| **Coverage** | 0.20 | `1.0 - (attempts_d / max_attempts_any)` | Favours under-explored domains |
| **Trend** | 0.15 | `actual_success_rate - predicted_success_rate` | Rewards improving competence |
| **PredictionError** | 0.30 | `|predicted - actual|` | Core Free Energy term — highest weight |
| **Novelty** | 0.25 | `exp(-attempts / 10)` | Exponential decay; never-attempted = 1.0 |
| **Stress** | -0.10 | Current system stress from Homeostasis | Suppresses exploration under load |

The domain with the highest F(d) is selected as the next intrinsic goal, provided it is not in cooldown.

## 4. Command Classification

Commands are classified into domains via deterministic keyword matching (no LLM involved). Each domain has a set of keywords that are matched against the command string:

- `cat`, `head`, `tail` → `file_read`
- `grep`, `find` → `file_search`
- `ps`, `pgrep` → `process_inspection`
- `git` → `git_operations`
- etc.

This ensures zero-latency classification of every `execution_result` on the bus.

## 5. Suggested Commands

Each domain has 3-5 pre-defined command templates. When an intrinsic goal is published, these are included so that the small model (Phi-4-mini) can **select** rather than **invent** commands:

```json
{
  "intent": "intrinsic_goal",
  "domain": "network_diagnostics",
  "fitness": 0.82,
  "suggested_commands": [
    "ss -tlnp | grep -E '(5555|5556|8080)'",
    "nc -z localhost 5555 && echo 'ZMQ PUB alive' || echo 'ZMQ PUB down'",
    "curl -s -o /dev/null -w '%{http_code}' http://localhost:8080"
  ],
  "context": "Never attempted. System stress: 0%."
}
```

## 6. Dopamine Signalling

The lobe emits `dopamine_signal` events in two scenarios:

1. **Novel capability discovery:** First successful execution in a domain that was never attempted before.
2. **Prediction error surprise:** When the prediction error for a domain exceeds 0.3 (30%), indicating the self-model was significantly wrong.

```json
{
  "intent": "dopamine_signal",
  "domain": "network_diagnostics",
  "reason": "novel_capability",
  "magnitude": 0.5
}
```

## 7. Learned Helplessness

After **5 consecutive failures** in a domain, the domain enters a **10-minute cooldown**. During cooldown:
- The domain is excluded from fitness ranking
- No intrinsic goals will be generated for it
- On cooldown expiry, the consecutive failure count resets to zero

This prevents the system from endlessly retrying tasks that are beyond its current capabilities, analogous to learned helplessness in biological systems.

## 8. Bus Integration

| Listens to | Source | Purpose |
|---|---|---|
| `execution_result` | `motor_cortex` | Classify command, update self-model |
| `intrinsic_goal_request` | `frontal_executive` | Compute fitness, publish goal |
| `intrinsic_goal_result` | `frontal_executive` | Explicit domain success/failure feedback |
| `homeostatic_pulse` | `homeostasis` | Decay stress level |
| `high_stress_alert` | `homeostasis` | Set stress to maximum |
| `time_pulse` | `chronos` | Check cooldown expiry |

| Publishes | Purpose |
|---|---|
| `intrinsic_goal` | Selected domain, fitness, suggested commands |
| `dopamine_signal` | Novel capability or prediction surprise |
| `self_model_updated` | Domain state changed |

## 9. Distributed Operation

The BasalGanglia can run on a separate machine from the Thalamus:
```bash
./build/basal_ganglia --thalamus <IP>
```
