# Synaptic Protocol Specification (v1.0)

## 1. Communication Layer: The Neural Bus
*   **Transport:** ZeroMQ (ipc:// for local, tcp:// for remote).
*   **Pattern:** ROUTER/DEALER for synchronous requests; PUB/SUB for sensory broadcasts.
*   **Performance Target:** < 1ms inter-lobe latency.

## 2. Message Format: The "Engram"
Every signal in the brain is a **Engram**. Engram Traces are structured JSON (simdjson-ready) containing:
*   `cid`: Correlation ID (the "Thought Stream" ID).
*   `origin`: Source lobe (e.g., "auditory_cortex").
*   `priority`: 0 (Background) to 5 (Amygdala Hijack/Emergency).
*   `payload`: The actual stimulus or action.
*   `context_ref`: A Git SHA or file path (Environment as Memory).

## 3. The Iterative Resonance Lifecycle
1.  **Stimulus:** Thalamus receives a Engram.
2.  **Gating:** Amygdala assesses priority.
3.  **Inference:** ModelManager fires the base SLM with the required LoRA.
4.  **Action:** Motor Cortex executes (Bash/File/Keyboard).
5.  **Proprioception:** Result is captured, a new Engram is generated, and the loop repeats until `<done>` is reached.

## 4. Intrinsic Motivation Protocol (BasalGanglia)

The BasalGanglia lobe introduces a motivation subsystem to the neural bus. Five new intents enable intrinsic goal generation and dopaminergic reward signalling:

| Intent | Origin | Target | Payload |
|---|---|---|---|
| `intrinsic_goal_request` | `frontal_executive` | `basal_ganglia` | `cid` — correlation ID for the goal cycle |
| `intrinsic_goal` | `basal_ganglia` | `frontal_executive` | `domain`, `fitness`, `suggested_commands[]`, `context` |
| `intrinsic_goal_result` | `frontal_executive` | `basal_ganglia` | `domain`, `success`, `command`, `fitness_score` |
| `dopamine_signal` | `basal_ganglia` | broadcast | `domain`, `reason` (novel_capability / prediction_surprise), `magnitude` |
| `self_model_updated` | `basal_ganglia` | broadcast | `domain` — indicates `data/self_model.json` was updated |

### Goal Request/Response Flow
```
FrontalExecutive                    BasalGanglia
      |                                  |
      |--- intrinsic_goal_request ------>|
      |                                  | compute fitness F(d) for 14 domains
      |                                  | select domain with max F(d)
      |<-------- intrinsic_goal ---------|
      |                                  |
      | execute suggested command        |
      |                                  |
      |--- intrinsic_goal_result ------->|
      |                                  | update self_model.json
      |                                  | emit dopamine_signal if novel
```

### Self-Model Schema (`data/self_model.json`)
```json
{
  "file_read": {
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
}
```

14 capability domains: `file_read`, `file_write`, `file_search`, `process_inspection`, `network_diagnostics`, `source_modification`, `compilation`, `git_operations`, `system_monitoring`, `data_analysis`, `script_creation`, `self_inspection`, `memory_analysis`, `log_analysis`.

### Fitness Function
```
F(d) = 0.20 * Coverage(d) + 0.15 * Trend(d) + 0.30 * PredictionError(d) + 0.25 * Novelty(d) - 0.10 * Stress
```

| Term | Formula | Biological Analogue |
|---|---|---|
| Coverage | `1.0 - (attempts_d / max_attempts_any)` | Curiosity towards unexplored territory |
| Trend | `recent_success_rate - predicted_success_rate` | Reward for improving competence |
| PredictionError | `|predicted - actual|` | Free Energy minimisation (Friston) |
| Novelty | `exp(-attempts / 10)` | Dopaminergic response to first encounter |
| Stress | Current system stress from Homeostasis | Suppresses exploration under load |

### Learned Helplessness
After 5 consecutive failures in a domain, the domain enters a 10-minute cooldown period. The domain is excluded from fitness ranking during cooldown. On expiry, consecutive failure count resets.

## 5. Resource Gating (Homeostasis)
*   **Active Mode:** Lobe is in VRAM (Hot).
*   **Dormant Mode:** Lobe weights are in System RAM (Warm).
*   **Pruned Mode:** Lobe is on Disk (Cold).
