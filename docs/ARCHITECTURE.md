# NeuroSwarm Architecture: Distributed Cortical Matrix (v2.0.0)

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

### III. Synaptic Evolution & Meta-Cognition
*   **Active Learning (REM Engine):** Successful "Reality Collapses" are archived and used for offline LoRA fine-tuning, upgrading the system's "Executive" policy autonomously.
*   **Meta-Cognition:** A dedicated observer layer monitors homeostatic stress (success/failure rates) and maintains an internal "Thought Stream" diary.

## 3. Physical Node Distribution
Lobes are location-agnostic. They can be distributed across a local network:
*   **VRAM Hub:** The `SynapticController` can run on a high-end GPU server.
*   **Edge Sensors:** `Auditory` and `Visual` lobes can run on devices with microphones/webcams.
*   **Orchestration:** The `Thalamus` and `Executive` manage the swarm from a central controller.

## 4. The Self-Modification (Neuro-Surgery)
NeuroSwarm can extend its own anatomy. By using the `neuro_surgery` execution mode, the system can write new C++ lobes, update the `CMakeLists.txt`, and trigger a recompilation. This allows the system to autonomously add new "organs" (e.g., a Database Lobe or a Web Crawler Lobe) as needed.

---
*NeuroSwarm: A framework for the exploration of non-reactive, self-sustaining digital intelligence.*
