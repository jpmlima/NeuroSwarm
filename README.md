# NeuroSwarm: Distributed Cognitive Architecture

<div align="center">
  <img src="https://img.shields.io/badge/Version-3.1.0-blue?style=for-the-badge" alt="v3.1.0">
  <img src="https://img.shields.io/badge/Architecture-Distributed_Cortical_Matrix-green?style=for-the-badge" alt="Architecture">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-orange?style=for-the-badge" alt="C++17">
  <img src="https://img.shields.io/badge/Inference-Qwen2.5--7B_Q4__K__M-purple?style=for-the-badge" alt="Qwen2.5-7B">
  <br><br>
  <h3>A biomimetic cognitive architecture that bootstraps from zero knowledge</h3>
  <p><em>Autopoiesis, intrinsic motivation, and structural self-modification in a distributed C++ system</em></p>
</div>

---

<div align="center">
  <img src="docs/neuroswarm_dashboard.png" alt="NeuroSwarm Cognitive Dashboard" width="900"/>
  <br>
  <sub>Real-time cognitive dashboard — 2D network graph with density clusters, cognitive pipeline indicator, Maslow drive hierarchy, neurogenesis metrics, spike task ticker</sub>
</div>

---

## I. Core Concept

NeuroSwarm is a computational organism built on five hardcoded axioms: **Execution** (act and observe), **Surprise** (prediction error as drive), **Associative Memory** (store state-action pairs), **Variation** (blind mutation of learned operators), and **Self-Reference** (maintain a boundary between self and world). Everything beyond these axioms — sensors, tools, specialist lobes, world knowledge — emerges through experience.

The system starts knowing nothing about its host. It discovers users, filesystems, binaries, network topology, and programming languages through direct probing (PrimordialLoop bootstrap). It plans using a GOAP backward-chaining planner over learned operators, generates novel operators through genetic variation tested in a sandboxed dream environment, and autonomously spawns new specialist lobes when capability domains chronically fail.

Each lobe is an independent OS process with a precisely scoped cognitive function. Communication is exclusively via ZeroMQ PUB/SUB through a central Thalamus relay. No lobe has visibility into the internals of another — only the message schema is shared. This yields a system that is:

- **Self-bootstrapping** — discovers its environment from zero, persists state, runs incremental bootstrap on restart
- **Self-planning** — GOAP planner over learned operators with postcondition indexing, Wilson scoring, and runtime precondition verification
- **Self-extending** — neurogenesis pipeline generates, compiles, and injects specialist C++ lobes at runtime
- **Self-healing** — crash detection, exponential backoff restart, stale fact invalidation, circadian memory consolidation
- **Observable** — all inter-lobe state is visible on the bus and rendered on a real-time dashboard
- **Distributed** — deploys copies of itself to remote machines via SSH, synchronises learned operators

---

## II. Architecture

The system runs 20+ concurrent processes. Rather than one monolithic diagram, the architecture is presented in four views: process topology, cognitive pipeline, learning loop, and memory hierarchy.

### Process Topology

All lobes connect to the Thalamus, a ZeroMQ XPUB/XSUB relay on tcp:5555/5556. CerebralMatrix fork-execs every process at startup and monitors them via `waitpid()`.

```
                         ┌─────────────────────────────────┐
                         │        CerebralMatrix           │
                         │   fork/exec · crash recovery    │
                         │   neurogenesis injection        │
                         └──────────────┬──────────────────┘
                                        │ manages
        ┌───────────────────────────────┼───────────────────────────────┐
        │               ┌──────────────┐│┌──────────────┐               │
        │    SENSORY     │  BrocaChat   │││ Visual Lobe  │  SENSORY     │
        │    INPUT       │  terminal IO │││ fs watcher   │  INPUT       │
        │                │  Auditory    │││              │               │
        │                └──────┬───────┘│└──────┬───────┘              │
        │                       │        │       │                      │
        │              ┌────────▼────────▼───────▼────────┐            │
        │              │                                   │            │
        │              │    THALAMUS — Neural Bus           │            │
        │              │    ZMQ XPUB/XSUB relay            │            │
        │              │    topic-filtered routing          │            │
        │              │                                   │            │
        │              └──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬────┘            │
        │                 │  │  │  │  │  │  │  │  │  │                 │
┌───────▼──┐ ┌──▼──┐ ┌──▼──┐ ┌▼──┐ ┌▼──┐ ┌▼──┐ ┌▼──┐ ┌──▼──┐ ┌──▼──┐
│Primordial│ │Front│ │Motor│ │Cri│ │Hip│ │REM│ │BG │ │Meta │ │Home │
│  Loop    │ │ Exec│ │Lobe │ │tic│ │po │ │Eng│ │   │ │ Cog │ │ost. │
│bootstrap │ │3-tier│ │exec │ │   │ │   │ │   │ │   │ │     │ │     │
│GOAP plan │ │pipel.│ │modes│ │3T │ │sem│ │sle│ │fit│ │gaps │ │tele│
│operators │ │goals │ │     │ │val│ │mem│ │ep │ │nes│ │prec│ │metr│
└──────────┘ └─────┘ └─────┘ └───┘ └───┘ └───┘ └───┘ └─────┘ └─────┘
                ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐ ┌──▼──┐
                │Wern.│ │Amyg.│ │Causal│ │Conc.│ │Chron│ │Stats│
                │NLU  │ │prio │ │world │ │clust│ │time │ │metr.│
                │     │ │gate │ │model │ │abs. │ │pulse│ │     │
                └─────┘ └─────┘ └──────┘ └─────┘ └─────┘ └─────┘

              ┌──────┐ ┌──────┐
              │Spike │ │Speci-│   Ephemeral processes:
              │Worker│ │alist │   fork+exec'd on demand,
              │(×N)  │ │lobes │   auto-terminate on completion
              └──────┘ └──────┘
```

### Cognitive Pipeline

A single goal passes through this pipeline. The three-tier execution strategy ensures the LLM is a last resort.

```
 ┌─────────────┐     ┌──────────────────────────────────────────────────────┐
 │ BasalGanglia │     │           FrontalExecutive — 3-Tier Pipeline         │
 │              │     │                                                      │
 │ F(d) fitness │────▶│  Tier 1: GOAP Planner                               │
 │ domain select│     │   postcondition index → backward chain → plan steps  │
 │ dopamine sig.│     │   PreconditionVerifier checks each step at runtime   │
 └──────────────┘     │                                                      │
                      │  Tier 2: Suggested Commands                          │
                      │   domain-specific templates from command genome       │
                      │                                                      │
                      │  Tier 3: LLM Oracle                                  │
                      │   structured prompt → Qwen2.5-7B → GBNF grammar     │
                      └─────────────────────┬────────────────────────────────┘
                                            │
                                            ▼
                      ┌──────────────────────────────────────────────────────┐
                      │              Validation Gate                          │
                      │                                                      │
                      │  BK-tree fuzzy match (Levenshtein to known-good)     │
                      │  CriticLobe 3-tier: blacklist → scope → red-team LLM │
                      └─────────────────────┬────────────────────────────────┘
                                            │
                           ┌────────────────┼────────────────┐
                           ▼                ▼                ▼
                      ┌─────────┐    ┌───────────┐    ┌───────────┐
                      │ REALITY │    │   DREAM   │    │  SURGERY  │
                      │ execute │    │ sandboxed │    │ self-mod  │
                      │ observe │    │ /tmp iso. │    │ src patch │
                      └────┬────┘    └─────┬─────┘    └─────┬─────┘
                           │               │                │
                           └───────────────┼────────────────┘
                                           ▼
                                  ┌─────────────────┐
                                  │ execution_result │
                                  │ → learn operator │
                                  │ → update model   │
                                  │ → store engram   │
                                  └─────────────────┘
```

### Learning Loop

Every execution feeds back into the system, closing a loop that makes each cycle more capable than the last.

```
     ┌──────────────────────────────────────────────────────────────────┐
     │                                                                  │
     │   ┌────────────┐    ┌────────────┐    ┌────────────────────┐    │
     │   │   EXECUTE   │───▶│  OBSERVE   │───▶│     LEARN          │    │
     │   │  MotorLobe  │    │ exit code  │    │ new operator with  │    │
     │   │  runs cmd   │    │ output     │    │ postconditions     │    │
     │   └────────────┘    │ fs changes │    │ (inferred from cmd │    │
     │                      └────────────┘    │  pattern matching) │    │
     │                                        └─────────┬──────────┘    │
     │                                                  │               │
     │   ┌────────────┐    ┌────────────┐    ┌──────────▼─────────┐    │
     │   │   SELECT    │◀───│    PLAN    │◀───│  OPERATOR REGISTRY │    │
     │   │ BasalGanglia│    │   GOAP     │    │  postcond index    │    │
     │   │ F(d) picks  │    │  backward  │    │  Wilson scoring    │    │
     │   │ next domain │    │  chain     │    │  ~400 operators    │    │
     │   └────────────┘    └────────────┘    └────────────────────┘    │
     │         │                                       ▲               │
     │         │           ┌────────────┐              │               │
     │         └──────────▶│  VARIATION  │─────────────┘               │
     │                     │ mutate      │  dream-tested               │
     │                     │ recombine   │  candidates promoted        │
     │                     │ assemble    │  to operator registry       │
     │                     └────────────┘                              │
     └──────────────────────────────────────────────────────────────────┘
```

### Memory Hierarchy

Three memory systems with distinct timescales, unified by the circadian sleep cycle.

```
 SHORT-TERM                    LONG-TERM                     PROCEDURAL
 (per-goal, seconds)           (indexed, persistent)         (operators, permanent)
 ┌──────────────────┐          ┌──────────────────┐          ┌──────────────────┐
 │ Hippocampus      │          │ Hippocampus      │          │ OperatorRegistry │
 │ Per-CID ledgers  │ ──────▶ │ memory_index     │          │ operators.jsonl  │
 │ raw engrams      │ consol. │ 768-dim embeddings│          │ preconditions    │
 │                  │         │ cosine similarity │          │ postconditions   │
 └──────────────────┘          │ importance-based  │          │ success rate     │
                               │ eviction (50K cap)│          │ Wilson scoring   │
  CAUSAL                       └──────────────────┘          └──────────────────┘
 ┌──────────────────┐                    │                           │
 │ CausalLobe       │                    ▼                           │
 │ do-calculus DAG  │          ┌──────────────────┐                  │
 │ P(Y|do(X))      │          │ REM Engine        │                  │
 │ backdoor adjust. │          │ sleep consolidation│                 │
 │ confounder detect│          │ knowledge synthesis│                 │
 │ Wilson strength  │          │ model fine-tuning  │                 │
 └──────────────────┘          └──────────────────┘                  │
                                        │                            │
  WORLD STATE                           │ training data              │
 ┌──────────────────┐                   ▼                            │
 │ PrimordialLoop   │          ┌──────────────────┐                  │
 │ world_state.json │          │ Fine-tuned Model  │                  │
 │ ~18 facts        │          │ Qwen2.5 + domain  │                  │
 │ sweep every 60s  │          │ knowledge         │                  │
 │ stale invalidation│         └──────────────────┘                  │
 └──────────────────┘                                                │
         ▲                                                           │
         └──────────── postcondition enrichment ─────────────────────┘

 CIRCADIAN CYCLE: Homeostasis triggers sleep every ~10 min →
   REM consolidates engrams, evolves genome, exports training data, fine-tunes model
   Hippocampus archives low-importance engrams, compacts memory index
```

---

## III. The Cognitive Cycle

The system runs a continuous autonomous loop with no external prompting:

```
 1. BasalGanglia evaluates F(d) for all 14 domains → selects highest-priority goal
 2. FrontalExecutive receives goal → enters three-tier pipeline:
      Tier 1 — GOAP Planner: postcondition index lookup, backward-chain,
               Wilson-scored operator selection, top-5 candidate pruning
      Tier 2 — Suggested Commands: domain-specific genome templates
      Tier 3 — LLM Oracle: structured prompt → local inference → GBNF output
 3. Validation: local filter (blacklist, prose detection) + BK-tree fuzzy matching
 4. CriticLobe: pattern blacklist + scope check + adversarial LLM review
 5. MotorLobe executes (reality / dream / neuro-surgery mode)
 6. PreconditionVerifier: runtime filesystem checks before each plan step
 7. PrimordialLoop learns: successful command → new operator with postconditions
 8. BasalGanglia updates domain statistics, prediction errors, BK-tree
 9. MetaCognition classifies errors, updates knowledge gap graph, infers preconditions
10. CausalLobe strengthens action→effect edges, updates do-calculus scores
11. Hippocampus stores execution as episodic engram, requests embedding
12. If circadian timer fires → REM consolidates memories, evolves genome, fine-tunes
13. If domain chronically failing → resolution pipeline → variation → neurogenesis
```

The three-tier pipeline ensures the LLM is a **last resort**. As the operator registry grows, Tier 1 resolves an increasing fraction of goals without any LLM call.

---

## IV. Bootstrap and Autopoiesis

### PrimordialLoop — From Zero to Operational

The PrimordialLoop is the first process launched. It encodes 5 axioms and runs 7 sequential phases:

| Phase | Name | Action |
|-------|------|--------|
| 0 | Existence | Confirm ability to execute and observe |
| 1 | First Contact | `whoami`, `hostname`, `uname`, `$HOME`, `$PATH` → self-model |
| 2 | Capability Discovery | Enumerate `$PATH` binaries, test write permissions |
| 3 | Sense Acquisition | Network, GPU, audio, display detection (always re-probes) |
| 4 | Tool Discovery | Programming languages and compilers |
| 5 | Active Exploration | Probe unknown binaries (`--help`, 1s timeout, skip GUI apps) |
| 6 | Planner Self-Test | Initialize GOAP planner, run canary plan |
| 7 | Network Expansion | Discover SSH hosts, deploy kernel, sync operators |

Each successful probe becomes a **learned operator** — a reusable action template with command, preconditions, postconditions, success rate, and fragments for recombination. Operators persist to `data/operators.jsonl`.

On subsequent startups, persisted state is loaded and phases skip known results. Bootstrap drops from ~60s to ~10s.

### Intrinsic Motivation (BasalGanglia)

BasalGanglia tracks 14 capability domains (`file_read`, `file_write`, `compilation`, `network_diagnostics`, etc.) and selects goals via a fitness function:

```
F(d) = 0.20 * Coverage + 0.15 * Trend + 0.30 * PredictionError + 0.25 * Novelty - 0.10 * Stress
```

Drive hierarchy (Maslow-inspired): SURVIVAL → HOMEOSTASIS → EXPLORATION → MASTERY → SELF_MODIFY. Higher drives preempt lower ones. On novel capability discovery, a `dopamine_signal` is broadcast.

### Domain Resolution Pipeline

Before neurogenesis, chronic failures go through a 4-stage resolution pipeline in the PrimordialLoop:

```
Variation (blind mutation) → Mutation (targeted) → Planner (find chain) → LLM Oracle
```

Each stage tests candidates in the Dream Sandbox. Only if all four fail does BasalGanglia trigger neurogenesis.

---

## V. GOAP Planner

The planner uses backward-chaining search over learned operators to decompose goals into executable steps.

**Postcondition Index** — three-tier lookup: exact match, prefix match, substring fallback. Eliminates linear scan of 400+ operators.

**Wilson Scoring** — operators are ranked by a lower confidence bound that balances success rate against evidence:

```
score = (p + z²/2n - z√(p(1-p)/n + z²/4n²)) / (1 + z²/n)
```

where p = success rate, n = times used, z = 1.96 (95% CI). This favours operators with both high success *and* sufficient evidence over untested ones with 100% on 1 trial.

**Budget and Pruning** — search is capped at 500 nodes. At each expansion step, only the top 5 candidates (by Wilson score) are explored.

**Runtime Precondition Verification** — before each plan step executes, `PreconditionVerifier` checks filesystem-based preconditions (`path_exists`, `file_exists`, `binary_exists`). If a precondition fails:
- The stale fact is removed from world state
- The plan is aborted
- The failure is classified and fed back into operator strengthening

**Periodic Sweep** — every 60 seconds, all transient facts in world state are re-verified. Stale facts (deleted files, moved directories) are invalidated. This prevents the planner from generating plans based on outdated state.

---

## VI. Memory Architecture

### Procedural Memory (Operator Registry)

Every learned action is stored as an `Operator` — a command template with preconditions, postconditions, success rate, duration stats, and decomposed fragments for genetic recombination. Operators are learned from bootstrap probing, runtime execution, variation, and LLM generation. The postcondition index enables O(1) lookup by effect. Operators with success rate ≥80% over ≥5 uses are *stable*; those with <10% over ≥20 uses are *dying* and get pruned (apoptosis). Persisted to `data/operators.jsonl`.

### Episodic Memory (Hippocampus)

Execution traces are embedded with `nomic-embed-text-v1.5` (768-dim) and stored in `data/engrams/`. Memory search uses L2-normalised cosine similarity over the index. The consolidation pipeline runs during sleep cycles:

- **Importance-based archival** — high-importance and successful engrams stay in the active ledger; low-importance ones are moved to `*_consolidated.jsonl` archives
- **Memory index compaction** — capped at 50,000 entries. When exceeded, entries are scored by `importance×0.6 + recency×0.3 + success×0.1` and the bottom entries are evicted
- **Bulk consolidation** — on sleep trigger, all per-CID ledgers exceeding 100KB are consolidated
- **Pending embed cleanup** — embedding requests that receive no response within 60s are expired

### Causal Memory (CausalLobe)

The CausalLobe maintains a directed graph of action→effect relationships. Unlike simple temporal correlation, it implements Pearl's do-calculus for genuine causal inference:

- **P(Y|do(X))** — computed via backdoor adjustment, stratifying by confounder presence patterns across observation windows
- **Confounder detection** — actions that frequently co-occur and share edges to the same effect are flagged as potential confounders
- **d-separation** — BFS ancestor check prevents post-treatment variables from entering the adjustment set
- **Wilson confidence** — causal strength is the lower bound of a 95% CI on the interventional probability. Edges need ≥3 observations to register any causal strength
- **Counterfactual tracking** — for each edge, the system records how often the effect occurs without the action (and vice versa)
- **Base rate decay** — effect base rates decay when not observed, preventing convergence to 1.0

### Behavioural Learning (REM Engine)

During sleep cycles (triggered every ~10 minutes by the circadian rhythm in Homeostasis):

1. **Knowledge synthesis** — analyses last 500 execution traces, extracts success/failure patterns, writes to `data/system_knowledge.md`
2. **Genome evolution** — prunes low-fitness operators, performs crossover of high-fitness templates
3. **Training data export** — successful reality-mode traces are exported as ChatML-format training data
4. **Model fine-tuning** — `llama-finetune` produces specialised GGUFs that replace the base model for future inference

---

## VII. Neuro-Surgery & Neurogenesis

### Neuro-Surgery (Self-Modification)

1. FrontalExecutive generates a patch (shell command targeting source files)
2. CriticLobe validates against the three-tier safety pipeline
3. MotorLobe executes in `neuro_surgery` mode: patch source → `cmake` → `make -j$(nproc)`
4. CerebralMatrix restarts the modified lobe

### Neurogenesis (Self-Extension)

1. BasalGanglia detects chronic failure in a domain (<30% over 20+ attempts)
2. Specialist lobe is generated from a parameterised C++ template
3. MotorLobe compiles as standalone executable
4. CerebralMatrix validates and fork-execs the binary as a live process
5. Specialist monitors its domain, publishes `specialist_advice` and `specialist_report`

### Apoptosis (Self-Pruning)

Lobes that are no longer useful are terminated via `lobe_terminate` signals. Lateral inhibition prunes redundant specialists when one handles less than 50% of a rival's commands.

---

## VIII. Component Map

| Process | Binary | Function |
|---|---|---|
| **CerebralMatrix** | `CerebralMatrix` | Process supervisor — fork/exec, crash recovery, neurogenesis injection, apoptosis |
| **PrimordialLoop** | `primordial_loop` | Bootstrap, GOAP planner, operator registry, variation, surprise, runtime learning, precondition sweep |
| **Thalamus** | `thalamus` | ZMQ XPUB/XSUB relay — all messages transit here |
| **SynapticController** | `synaptic_controller` | LLM inference — Qwen2.5-7B + Nomic-Embed, multi-slot ModelManager, fine-tuned model loading |
| **FrontalExecutive** | `frontal_executive` | 3-tier execution pipeline, goal pursuit, spike worker delegation |
| **CriticLobe** | `critic_lobe` | Three-tier validation: pattern blacklist + scope + red-team LLM |
| **MotorLobe** | `motor_lobe` | Command execution: reality, dream (sandboxed), neuro-surgery |
| **Hippocampus** | `hippocampus` | Semantic episodic memory — embedding index, consolidation, trajectory tracking |
| **REM Engine** | `rem_engine` | Sleep consolidation, knowledge synthesis, genome evolution, model fine-tuning |
| **Homeostasis** | `homeostasis` | CPU/RAM/GPU telemetry, stamina, circadian sleep trigger (10-min cycle) |
| **WernickeLobe** | `wernicke_lobe` | NLU — intent classification, entity extraction |
| **Amygdala** | `amygdala` | Priority gating — urgency assignment, threat detection |
| **MetaCognition** | `metacognition` | Error classification, knowledge gap graph, precondition chains, exploration targets |
| **VisualLobe** | `visual_lobe` | Filesystem watcher — environmental state changes |
| **AuditoryLobe** | `auditory_lobe` | Voice input via Whisper.cpp |
| **Visualizer** | `visualizer` | 2D Canvas dashboard (port 8080) |
| **ChronosLobe** | `chronos_lobe` | Temporal awareness — ISO timestamps, uptime, circadian phase |
| **StatisticsLobe** | `statistics_lobe` | Bus observer — per-cycle metrics to `data/metrics/` |
| **BasalGanglia** | `basal_ganglia` | Intrinsic motivation — fitness function, dopamine, neurogenesis trigger, BK-tree validation |
| **ConceptLobe** | `concept_lobe` | Online clustering, abstraction hierarchy, composite operations |
| **CausalLobe** | `causal_lobe` | Causal world model — do-calculus DAG, backdoor adjustment, confounder detection |
| **SpikeWorker** | `spike_worker` | Ephemeral per-goal worker — fork+exec'd, CID-isolated, 120s timeout |
| *Specialists* | `build/{name}` | Runtime-generated lobes for failing domains (neurogenesis) |

---

## IX. Message Protocol

Every message on the neural bus is a JSON object:

```json
{
  "cid":    "correlation identifier — threads related messages",
  "origin": "source lobe name",
  "intent": "semantic action descriptor"
}
```

Core intents (40+ total, grouped by function):

| Group | Intents |
|---|---|
| **Cognition** | `stimulus`, `inference_request/result`, `critic_validate/result`, `execution_request/result` |
| **Planning** | `goal_plan_request/goal_plan`, `operator_request`, `primordial_ready`, `domain_resolve_request/result` |
| **Motivation** | `intrinsic_goal_request/goal/result`, `dopamine_signal`, `self_model_updated`, `rlaif_reinforce` |
| **Memory** | `search_memory/result`, `recall_memory/recalled`, `embedding_request/result`, `consolidate_memories/complete` |
| **Workers** | `spike_ready`, `spike_assign`, `spike_done` |
| **Neurogenesis** | `genesis_request/result`, `inject_lobe`, `lobe_injected/terminate/terminated/crash/death` |
| **Specialist** | `specialist_advice`, `specialist_report`, `validate_command/validated` |
| **Autonomic** | `homeostatic_pulse`, `initiate_sleep_cycle/complete`, `time_pulse`, `exploration_target`, `knowledge_gap` |
| **World Model** | `causal_query/prediction/update`, `concept_query/response/update`, `visual_stimulus` |
| **Training** | `training_complete`, `prompt_update` |

Full specification: [`docs/SYNAPTIC_PROTOCOL.md`](docs/SYNAPTIC_PROTOCOL.md)

---

## X. Stack

| Layer | Technology |
|---|---|
| **Neural Bus** | ZeroMQ 4.x — PUB/SUB with XPUB/XSUB relay, topic-filtered routing |
| **Inference** | llama.cpp (Vulkan GPU offload) — Qwen2.5-7B-Instruct Q4_K_M |
| **Embeddings** | nomic-embed-text-v1.5 Q8_0 — 768-dim, mean-pooled, L2-normalised |
| **Causal Inference** | Do-calculus with backdoor adjustment, Wilson confidence bounds |
| **Planning** | GOAP backward-chaining with postcondition indexing and precondition verification |
| **Grammar** | GBNF — llama.cpp grammar-constrained decoding for structured JSON |
| **Safety** | Three-tier CriticLobe + Dream Sandbox isolation + BK-tree validation |
| **Self-Modification** | GCC compilation + CerebralMatrix fork/exec injection |
| **Observability** | HTML5 Canvas + cpp-httplib — 2D network graph with live metrics |
| **Build System** | CMake 3.16+ / C++17 |
| **OS** | Linux (Vulkan compute) |

---

## XI. Deployment

```bash
# 1. Build
mkdir build && cd build
cmake .. && make -j$(nproc)
cd ..

# 2. Download models (first time only)
python3 -c "
from huggingface_hub import hf_hub_download
hf_hub_download('Qwen/Qwen2.5-7B-Instruct-GGUF',
                'qwen2.5-7b-instruct-q4_k_m.gguf', local_dir='models')
hf_hub_download('nomic-ai/nomic-embed-text-v1.5-GGUF',
                'nomic-embed-text-v1.5.Q8_0.gguf', local_dir='models')
"

# 3. Launch
./build/CerebralMatrix

# 4. Conversation
./build/broca_chat

# 5. Dashboard
xdg-open http://localhost:8080
```

**Hardware:** GPU with Vulkan support, 8GB+ VRAM recommended. Tested on GTX 1080 Ti (11GB). CPU fallback available.

---

## XII. Empirical Results

From 68 hours of autonomous operation (see `eval/results/tables.tex`):

| Metric | Value |
|---|---|
| Total goals attempted | 1,424 |
| Resolution rate | 99.7% |
| Success rate | 97.3% |
| Operators learned | 1,219 |
| Autonomy index | 0.952 |
| Mean prediction error | 0.3012 |

The autonomy index measures the fraction of goals resolved without LLM calls (Tier 1 + Tier 2). At 0.952, the system handles 95% of its goals through learned operators and genome templates alone.

---

*Intelligence is not in the size of the model. It is in the complexity of the connections.*
