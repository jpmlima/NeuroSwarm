# NeuroSwarm: Principles of Digital Functional Neuro-Anatomy

<div align="center">
  <img src="https://img.shields.io/badge/Version-2.0.0-blue?style=for-the-badge" alt="v2.0.0">
  <img src="https://img.shields.io/badge/Architecture-Distributed_Cortical_Matrix-green?style=for-the-badge" alt="Architecture">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-orange?style=for-the-badge" alt="C++17">
  <br>
  <h3>An Asynchronous Orchestration Framework for Autonomous Cognitive Emulation</h3>
</div>

---

## I. Abstract: The Society of Mind
**NeuroSwarm** is a biomimetic framework that rejects the paradigm of monolithic AI. Instead of relying on a single "god-model," it implements Marvin Minsky's **Society of Mind** theory: intelligence is not a single process, but the emergent result of many "small idiot agents" (specialized SLMs) collaborating through feedback loops.

> "A brain is not an intelligent 'thing', it is a set of 'small idiot agents' that, by collaborating, create emergent intelligence." — *Marvin Minsky*

## II. The Cognitive Hierarchy (v2.0.0)

### 1. Epistemic Drive (Intention & Curiosity)
The system is no longer purely reactive. When idle, the **Default Mode Network (DMN)** triggers self-assigned learning goals. The system explores its environment and codebases autonomously to "satisfy" its curiosity drive.

### 2. Inner Monologue & Consensus
Every plan goes through an adversarial validation cycle:
*   **The Proposer (Frontal Executive):** Synthesizes goals into actions.
*   **The Validator (Critic Lobe):** Analyzes the plan for security risks, logical fallacies, and efficiency.
*   **Consensus:** Action only occurs when the Critic grants `APPROVED` status.

### 3. World Simulation (Dream Sandbox)
NeuroSwarm performs **Counterfactual Reasoning**. It simulates its actions in an isolated filesystem (`data/dreams/`) before "collapsing the dream" into physical reality. This ensures 100% safety and predictability in system-level operations.

### 4. Neuro-Surgery (Live Self-Modification)
The system can alter its own source code. The Executive can write new C++ Lobes, inject them into the CMake build system, and trigger a live recompilation of the Matrix without downtime.

---

## III. System Anatomy (Neural Bus Topology)

The following diagram illustrates the flow of a single stimulus through the matrix:

```mermaid
graph TD
    subgraph "Sensory Inputs"
        SI[User Terminal / Voice / Image]
    end

    subgraph "Autonomic Nervous System"
        TH{THALAMUS: Global Router}
        HM[Homeostasis: Vitals & Stress]
        MC[Meta-Cognition: Reflection/Diary]
    end

    subgraph "Cognitive Core (Inner Monologue)"
        WN[Wernicke: Semantic NLU]
        FE[Frontal Executive: Orchestration]
        CL[Critic Lobe: Adversarial Logic]
    end

    subgraph "Limbic & Memory System"
        HP[Hippocampus: Vector RAG]
        REM[REM Engine: LoRA Fine-tuning]
    end

    subgraph "Execution & Simulation"
        DS[Dream Sandbox: Simulation]
        MT[Motor Cortex: OS Actions & Surgery]
    end

    %% Flow
    SI --> WN
    WN --> TH
    TH <--> FE
    FE -- Internal Thought --> CL
    CL -- Validation/Feedback --> FE
    FE -- Dream Command --> DS
    DS -- Simulation Result --> FE
    FE -- Reality Collapse --> MT
    HM -- Stress Alert --> FE
    HP -- context --> FE
    
    style TH fill:#000,stroke:#00ffcc,stroke-width:4px,color:#fff
    style FE fill:#2d2d2d,stroke:#ff3300,color:#fff
    style DS fill:#330066,stroke:#cc33ff,color:#fff
    style CL fill:#660000,stroke:#ff0000,color:#fff
    style MT fill:#003300,stroke:#00ff00,color:#fff
```

---

## IV. Technical Specification

| Subsystem | Technology | Function |
| :--- | :--- | :--- |
| **Neural Bus** | ZeroMQ (PUB/SUB) | Nanosecond inter-lobe routing. |
| **Inference** | llama.cpp (CPU/Vulkan) | LoRA Multiplexing of 1.5B - 8B models. |
| **Memory** | Neural Vector Search | Semantic retrieval via cosine similarity. |
| **Observability** | 3D Neural Mesh (Three.js) | Real-time EEG visual monitoring (8080). |
| **Safety** | Isolated Dream Sandbox | Pre-execution verification of bash/python. |

## V. Deployment & Awakening

```bash
# 1. Compile the Matrix
mkdir build && cd build
cmake .. && make -j$(nproc)

# 2. Activate the Daemon
./scripts/neuroswarm_daemon.sh

# 3. Enter the Mind
./build/broca_chat
```

---
*True AGI is not found in the size of the model, but in the complexity of the connections.*
