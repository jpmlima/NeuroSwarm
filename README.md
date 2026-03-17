# NeuroSwarm: Principles of Digital Functional Neuro-Anatomy

<div align="center">
  <h3>An Asynchronous Orchestration Framework for Autonomous Cognitive Emulation</h3>
  <p>Technical Specification v1.6.0 | Semantic & Distributed Implementation</p>
</div>

---

## I. Abstract
**NeuroSwarm** is a distributed system designed to emulate the functional partitioning, homeostatic regulation, and endogenous activity of the human brain. v1.6.0 introduces **Semantic Memory** and **Distributed Node Clusters**, allowing specialized lobes to operate across multiple physical machines while maintaining a unified cognitive state.

## II. The Distributed Hierarchy

### 1. The Neural Bus (Thalamus v2)
The Thalamus now operates as a **Global Dispatcher**. It binds to `0.0.0.0`, enabling lobes on different hardware (e.g., Raspberry Pi, Remote GPU Servers) to connect to the central nervous system via the `--thalamus <IP>` protocol.

### 2. Semantic Memory (Vector Hippocampus)
Memory retrieval has evolved from keyword-matching to **Neural Vector Search**.
*   **Embeddings:** The `ModelManager` generates high-dimensional semantic vectors for every engram.
*   **Cosine Similarity:** The Hippocampus performs mathematical comparisons to find memories that are conceptually related to the current goal, even if the wording differs.

### 3. Functional Lobes (The Distributed Swarm)
Lobes are now location-agnostic. 
*   **Synaptic Controller:** Can be hosted on a high-VRAM GPU server.
*   **Motor Lobe:** Can run locally on the user's machine to execute OS commands.
*   **Frontal Executive:** Acts as the decentralized orchestrator.

---

## III. System Anatomy: The Functional & Distributed Matrix

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
    MT --> Env

    %% Styling
    style TH fill:#000,stroke:#00ffcc,stroke-width:4px,color:#fff
    style FE fill:#2d2d2d,stroke:#ff3300,color:#fff
    style HM fill:#003366,stroke:#3399ff,color:#fff
    style MC fill:#ffcc00,stroke:#fff,color:#000
    style REM fill:#660066,stroke:#cc33ff,color:#fff
    style SC fill:#1a1a1a,stroke:#00ff00,color:#fff
```

## IV. Core Innovation: Semantic & Distributed Processing
...

| Metric | Specification |
| :--- | :--- |
| **Search Mode** | Semantic (Neural Embedding) |
| **Node Support** | Distributed (TCP/IP) |
| **Embedding Engine** | Llama.cpp (llama_get_embeddings) |
| **Communication** | ZeroMQ v4.3.x (Asynchronous) |

## V. Awakening Sequence (Distributed)
```bash
# Start Thalamus on Central Machine (IP: 192.168.1.10)
./build/thalamus

# Connect a Lobe from another machine
./build/motor_lobe --thalamus 192.168.1.10
```

---
*NeuroSwarm: Advancing the frontier of distributed digital intelligence.*
