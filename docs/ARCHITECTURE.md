# NeuroSwarm Architecture: Distributed Cortical Matrix (v3.1.0)

## 1. Vision

NeuroSwarm is a self-bootstrapping cognitive architecture that runs as 20+ concurrent Unix processes communicating via a ZeroMQ message bus. It starts knowing nothing about its host and builds its capabilities through direct interaction — learning operators, constructing plans, and modifying its own structure at runtime.

## 2. Core Architectural Pillars

### I. The Neural Bus (Thalamus)

Following Global Workspace Theory, all information is broadcast through the Thalamus — a ZMQ XPUB/XSUB relay on tcp:5555/5556. Topic-based subscription filtering ensures lobes only receive relevant intents. Every message is a JSON object with a Correlation ID (CID) for lineage tracking.

### II. Recursive Cognitive Pipeline

1. **Semantic Interpretation** — Wernicke Lobe converts user input into structured intents.
2. **Three-Tier Execution** — FrontalExecutive exhausts deterministic strategies before consulting the LLM:
   - *Tier 1*: GOAP Planner with postcondition indexing and Wilson-scored operator selection
   - *Tier 2*: Domain-specific command templates from the evolved genome
   - *Tier 3*: LLM inference with GBNF grammar constraints
3. **Validation** — BK-tree fuzzy matching + CriticLobe three-tier adversarial review (blacklist → scope → red-team LLM)
4. **Dream Sandbox** — plan is executed in an isolated /tmp environment. Exit codes and stderr are observed.
5. **Reality Collapse** — if verified, the action is applied to the host OS.
6. **Precondition Verification** — before each plan step, PreconditionVerifier checks filesystem-based facts. Stale facts are invalidated, aborting plans that would fail.

### III. Intrinsic Motivation (BasalGanglia)

The BasalGanglia maintains a self-model across 14 capability domains and computes a fitness function based on Friston's Free Energy Principle:

```
F(d) = 0.20 * Coverage + 0.15 * Trend + 0.30 * PredictionError + 0.25 * Novelty - 0.10 * Stress
```

- **Self-Model** — `data/self_model.json` tracks success/failure counts, predicted vs actual rates, novelty scores per domain
- **Dopamine Signalling** — novel capability discovery or high prediction error triggers a `dopamine_signal` on the bus
- **Drive Hierarchy** — SURVIVAL → HOMEOSTASIS → EXPLORATION → MASTERY → SELF_MODIFY (Maslow-inspired)
- **Learned Helplessness** — 5 consecutive failures trigger a 10-minute cooldown per domain

### IV. GOAP Planning

The planner uses backward-chaining search over 400+ learned operators:

- **Postcondition Index** — three-tier lookup (exact → prefix → substring) eliminates linear scan
- **Wilson Scoring** — lower bound of 95% CI on success rate, balancing performance against evidence
- **Budget** — 500 nodes max, top-5 candidates per expansion step
- **Precondition Verification** — runtime filesystem checks before each plan step execution
- **Stale Fact Sweep** — every 60 seconds, all transient world state facts are re-verified

### V. Causal World Model (CausalLobe)

The CausalLobe maintains a directed graph of action→effect relationships using Pearl's do-calculus:

- **P(Y|do(X))** via backdoor adjustment — stratifies observation windows by confounder presence patterns
- **Confounder detection** — co-occurring actions with shared effects are flagged; d-separation prevents post-treatment bias
- **Wilson confidence** — causal strength is the lower bound of a 95% CI on interventional probability
- **Counterfactual tracking** — records effect-without-action and action-without-effect frequencies
- **Base rate decay** — recomputed from rolling window; effects that stop appearing see their rate decline

### VI. Memory Consolidation

Three-tier memory hierarchy unified by a circadian sleep cycle:

- **Short-term** — per-CID ledgers in Hippocampus (raw engrams)
- **Long-term** — memory_index.jsonl (768-dim embeddings, cosine similarity, 50K cap with importance-based eviction)
- **Procedural** — OperatorRegistry (operators.jsonl, postcondition index, Wilson scoring)
- **Consolidation** — Homeostasis triggers sleep every 10 min. Hippocampus archives low-importance engrams, compacts the index. REM Engine synthesises knowledge, evolves the genome, exports training data, and fine-tunes the local model.

### VII. Self-Modification

- **Neuro-Surgery** — FrontalExecutive generates source patches, CriticLobe validates, MotorLobe patches/builds in `neuro_surgery` mode, CerebralMatrix restarts affected lobes
- **Neurogenesis** — BasalGanglia detects chronic domain failure → generates specialist C++ lobe → MotorLobe compiles → CerebralMatrix injects as live process
- **Apoptosis** — redundant specialists terminated via `lobe_terminate`; lateral inhibition prunes overlapping specialists

## 3. Physical Distribution

Lobes are location-agnostic. The Thalamus relay can bridge between machines:

- **VRAM Hub** — SynapticController on a GPU server
- **Edge Sensors** — Auditory and Visual lobes on devices with microphones/cameras
- **Network Expansion** — PrimordialLoop discovers SSH hosts, deploys copies of itself, syncs operators

---

*NeuroSwarm: a framework for non-reactive, self-sustaining digital intelligence.*
