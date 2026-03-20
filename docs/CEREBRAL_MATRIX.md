# Cerebral Matrix: System Engineering Specification

## 1. Objective
To construct a distributed, micro-service based AI architecture that replicates the functional modularity and iterative refinement of the human brain.

## 2. Iterative Resonance (The Execution Engine)
Unlike traditional LLM agents that rely on long-context "vibe" history, NeuroSwarm operates via **Iterative Resonance**. 
*   **Decoupled Context:** Functional units (lobes) are stateless.
*   **State Alignment:** A unit fires, observes the physical environment (Files/Shell), and re-fires if the "resonance" (alignment) between the goal and reality is not achieved.
*   **Hardcore Persistence:** Resonance cycles persist across hardware restarts by reloading the last state from the physical medium.

## 3. Engram Traces (The Synaptic Ledger)
Memory is handled as a stream of **Engram Traces**. 
*   **Atomic Persistence:** Every thought and action is serialized as an Engram.
*   **Versioned Worktrees:** Each "Active Column" works in a dedicated Git worktree, ensuring that divergent thought-streams do not contaminate the primary "Main Neocortex" (Main Branch).

## 4. Frontal Executive (The Orchestrator)
The Frontal Executive does not execute code. It generates **Cognitive Task Graphs**. It assigns these graphs to Active Columns and monitors the "Nervous Bus" (ZeroMQ) for resonance completion tags.

## 5. BasalGanglia (The Motivation Engine)
The BasalGanglia implements intrinsic motivation via a deterministic self-model and fitness function. It maintains a capability registry across 14 domains in `data/self_model.json`, computing a fitness score for each domain based on coverage, trend, prediction error, novelty, and system stress. When the Frontal Executive exhausts all external tasks, it requests an intrinsic goal from the BasalGanglia, which responds with the most informative domain to explore and concrete command templates for the small model to select from. Dopamine signals are emitted on novel capability discovery or high prediction error events.

### Spawned by CerebralMatrix
| Binary | Process | Function |
|---|---|---|
| `thalamus` | Neural Bus | ZMQ relay |
| `synaptic_controller` | Inference | Phi-4-mini + Nomic-Embed |
| `motor_lobe` | Execution | Dream/Reality/Neuro-Surgery |
| `frontal_executive` | Orchestration | Goal management, three-tier task selection |
| `amygdala` | Emotional gating | Priority assignment |
| `wernicke_lobe` | NLU | Intent classification |
| `visual_lobe` | Sensation | Filesystem watcher |
| `homeostasis` | Regulation | CPU/RAM/GPU telemetry |
| `metacognition` | Reflection | Internal state diary |
| `critic_lobe` | Safety | Three-tier validation |
| `visualizer` | Observability | 2D network dashboard |
| `rem_engine` | Learning | Behavioural prompt evolution |
| `chronos_lobe` | Temporal | Time pulse broadcaster |
| `statistics_lobe` | Metrics | JSONL event recorder |
| `basal_ganglia` | Motivation | Intrinsic goal generation, self-model, dopamine, neurogenesis |

## 6. Self-Preservation (Crash Detection & Recovery)
CerebralMatrix monitors all child processes via non-blocking `waitpid(WNOHANG)`. When a lobe crashes:
1. Exit reason is extracted (signal number or exit code)
2. `lobe_crash` event is broadcast on the bus
3. Crash counter is incremented; restart is attempted with exponential backoff (2s → 4s → 8s → 16s)
4. After 5 consecutive failures, the lobe is marked permanently **DEAD** and a `lobe_death` event is broadcast

Orphaned processes from previous sessions are cleaned up at startup by scanning `/proc` for executables in the build directory.

## 7. Neurogenesis (Runtime Lobe Injection)
CerebralMatrix subscribes to `inject_lobe` signals on a dedicated ZMQ sub socket. When a new lobe binary is compiled (by MotorLobe via `genesis_request`):
1. Binary path is validated (exists + executable via `stat()`)
2. Deduplication check prevents spawning a lobe that is already alive
3. `fork()`/`exec()` launches the new process
4. `lobe_injected` event is broadcast on the bus

## 8. Apoptosis (Lobe Termination)
On receiving a `lobe_terminate` signal:
1. Target lobe receives `SIGTERM`
2. 2-second grace period for clean shutdown
3. `SIGKILL` if still alive
4. `lobe_terminated` event is broadcast on the bus
