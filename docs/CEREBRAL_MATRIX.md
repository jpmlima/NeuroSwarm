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
| `critic_lobe` | Safety | Two-tier validation |
| `visualizer` | Observability | 3D dashboard |
| `rem_engine` | Learning | Behavioural prompt evolution |
| `chronos_lobe` | Temporal | Time pulse broadcaster |
| `statistics_lobe` | Metrics | JSONL event recorder |
| `basal_ganglia` | Motivation | Intrinsic goal generation, self-model, dopamine |
