# NeuroSwarm Architecture: Distributed Cortical Matrix (v2.2.0)

## 1. Vision
NeuroSwarm aims to achieve emergent intelligence by replicating the partitioned, asynchronous, and homeostatic nature of the biological brain. The system is designed to operate as a self-optimizing organism where autonomy arises from the feedback loops between specialized, lightweight cognitive nodes.

## 2. Core Architectural Pillars

### I. The Neural Bus (Thalamus)
Following **Global Workspace Theory (GWT)**, all information in NeuroSwarm is broadcast to a shared "workspace" (the Thalamus).
*   **Protocol:** ZeroMQ PUB/SUB.
*   **Asynchronicity:** Lobes process information at their own "retinal" or "cognitive" rates without blocking the central bus.
*   **Routing:** Every message is a JSON "neuro-stimulus" containing a Correlation ID (CID) to track the lineage of a thought.

### II. Recursive Cognitive Pipeline
Unlike standard AI pipelines (Input -> Output), NeuroSwarm implements a recursive loop:
1.  **Semantic Interpretation:** Wernicke Lobe converts user input into structured intents.
2.  **Adversarial Monologue:** The Frontal Executive proposes a plan; the Critic Lobe (Cingulate Cortex) attempts to find flaws. This iterates until a consensus is reached.
3.  **World Simulation:** The plan is executed in the **Dream Sandbox**, an isolated environment. The Executive observes the simulation's exit codes and stderr.
4.  **Reality Collapse:** If verified, the system applies the action to the host OS (Reality).

### III. Intrinsic Motivation (BasalGanglia)
NeuroSwarm implements a biologically-inspired motivation system based on Karl Friston's **Free Energy Principle**. Rather than relying on a human-authored task list or LLM-generated goals, the **BasalGanglia** lobe maintains a self-model of the system's capabilities across 14 domains and computes a fitness function to select the most informative domain to explore next.

*   **Self-Model:** `data/self_model.json` tracks success/failure counts, predicted vs actual success rates, and novelty scores for each capability domain.
*   **Fitness Function:** A weighted combination of coverage (under-explored domains), trend (improving competence), prediction error (Free Energy term), novelty (exponential decay), and system stress (suppresses exploration under load).
*   **Dopamine Signalling:** When the system discovers a novel capability or encounters high prediction error, it emits a `dopamine_signal` on the bus — analogous to VTA dopaminergic projections in biological reward circuits.
*   **Three-Tier Task Selection:** FrontalExecutive now selects goals via: (1) external tasks from `tasks.json`, (2) intrinsic goals from BasalGanglia, (3) hardcoded epistemic fallback.
*   **Learned Helplessness:** 5 consecutive failures in a domain trigger a 10-minute cooldown, preventing the system from repeatedly failing at tasks beyond its current capabilities.

### IV. Synaptic Evolution & Meta-Cognition
*   **Active Learning (REM Engine):** Successful "Reality Collapses" are archived and used for offline LoRA fine-tuning, upgrading the system's "Executive" policy autonomously.
*   **Meta-Cognition:** A dedicated observer layer monitors homeostatic stress (success/failure rates) and maintains an internal "Thought Stream" diary.

## 3. Physical Node Distribution
Lobes are location-agnostic. They can be distributed across a local network:
*   **VRAM Hub:** The `SynapticController` can run on a high-end GPU server.
*   **Edge Sensors:** `Auditory` and `Visual` lobes can run on devices with microphones/webcams.
*   **Orchestration:** The `Thalamus` and `Executive` manage the swarm from a central controller.

## 5. The Self-Modification (Neuro-Surgery)
NeuroSwarm can extend its own anatomy. By using the `neuro_surgery` execution mode, the system can write new C++ lobes, update the `CMakeLists.txt`, and trigger a recompilation. This allows the system to autonomously add new "organs" (e.g., a Database Lobe or a Web Crawler Lobe) as needed.

---
*NeuroSwarm: A framework for the exploration of non-reactive, self-sustaining digital intelligence.*
