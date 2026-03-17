# NeuroSwarm: Principles of Digital Functional Neuro-Anatomy

<div align="center">
  <h3>An Asynchronous Orchestration Framework for Autonomous Cognitive Emulation</h3>
  <p>Technical Specification v1.7.0 | Distributed & Observable Implementation</p>
</div>

---

## I. Abstract
**NeuroSwarm** is a distributed system designed to emulate the functional partitioning, homeostatic regulation, and endogenous activity of the human brain. v1.7.0 introduces **Neural Observability (EEG)**, providing a real-time visual dashboard of the system's internal stimuli and cognitive state.

## II. System Anatomy: The Functional & Distributed Matrix

NeuroSwarm operates as a **Decentralized Cortical Matrix**. Lobes communicate via the Thalamus, allowing for a self-evolving system that can be spread across multiple physical nodes.

```mermaid
graph TD
    subgraph "External Stimuli"
        User((User Input))
        Env((Environment))
    end

    subgraph "Central Nervous Bus (Thalamus)"
        TH{THALAMUS: Global Router}
    end

    subgraph "Cognitive Core (Prefrontal Cortex)"
        FE[Frontal Executive: Planning]
        WN[Wernicke: Semantic NLU]
        BR[Broca: Articulation]
    end

    subgraph "Observability (EEG)"
        VZ[Visualizer: Web Dashboard]
    end

    subgraph "Synaptic Hub (VRAM Engine)"
        SC[Synaptic Controller: Inference]
        MM[ModelManager: LoRA Multiplexing]
    end

    subgraph "Memory & Learning (Limbic System)"
        HP[Hippocampus: Vector Memory]
        REM[REM Engine: Active Learning]
    end

    subgraph "Autonomic System"
        HM[Homeostasis: Pulse & Stress]
        MC[Meta-Cognition: Reflection]
    end

    subgraph "Execution (Motor Lobe)"
        MT[Motor Cortex: OS Actions]
    end

    %% Flow
    User --> WN
    WN --> TH
    TH <--> FE
    FE <--> MT
    TH <--> SC
    SC <--> MM
    FE <--> HP
    HP -- Semantic Clusters --> REM
    REM -- Synaptic Update --> SC
    HM -- Stress Level --> FE
    HM -- Vitals --> MC
    MC -- Mood --> BR
    TH -- Neural Traffic --> VZ
    MT --> Env

    %% Styling
    style TH fill:#000,stroke:#00ffcc,stroke-width:4px,color:#fff
    style FE fill:#2d2d2d,stroke:#ff3300,color:#fff
    style HM fill:#003366,stroke:#3399ff,color:#fff
    style MC fill:#ffcc00,stroke:#fff,color:#000
    style REM fill:#660066,stroke:#cc33ff,color:#fff
    style SC fill:#1a1a1a,stroke:#00ff00,color:#fff
    style VZ fill:#004444,stroke:#00ffff,color:#fff
```

## III. Core Innovation: Neural Observability

v1.7.0 adds the **Visualizer Lobe**, a dedicated EEG-like interface that captures the asynchronous traffic of the Thalamus.
*   **Real-Time Dashboard:** Served at `http://localhost:8080`.
*   **Lobe Activity Monitor:** Visualizes which regions of the "brain" are firing in response to specific stimuli.
*   **Engram Stream:** A live feed of intent-objects flowing through the nervous bus.

---

## IV. Operational Benchmarks (v1.7.0)

| Metric | Specification |
| :--- | :--- |
| **Search Mode** | Semantic (Neural Embedding) |
| **Observability** | Real-time Web Dashboard (8080) |
| **Node Support** | Distributed (TCP/IP) |
| **Communication** | ZeroMQ v4.3.x (Asynchronous) |

## V. Awakening Sequence
```bash
# Compile and Start the Matrix
./scripts/neuroswarm_daemon.sh

# Watch the Brain Thinking
# Open http://localhost:8080 in your browser

# Talk to the system
./build/broca_chat
```

---
*NeuroSwarm: Advancing the frontier of distributed digital intelligence.*
