# NeuroSwarm Architecture

**A biomimetic, C++ based multi-agent system designed for extreme efficiency and true autonomy.**

## Vision
NeuroSwarm aims to replicate the anatomical structure and functional efficiency of a biological brain. Instead of relying on a single monolithic, omnipotent LLM (Large Language Model) requiring massive GPU clusters, NeuroSwarm utilizes a constellation of ultra-small, highly specialized models (SLMs - Small Language Models). These models act as individual "lobes" or functional regions of the brain, communicating asynchronously via a high-performance C++ neural bus.

This architecture enables a swarm of agents (up to 100 specialized adapters) to run simultaneously on consumer-grade hardware (e.g., a single GTX 1080 Ti with 11GB VRAM).

## Core Principles

1.  **Biomimicry over Brute Force:** System design strictly follows neuroanatomical principles (Thalamus for routing, Prefrontal Cortex for planning, Amygdala for priority/emotion, Motor Cortex for execution).
2.  **The Iterative Resonance Engine:** Lobes do not retain bloated context windows. They operate on "Iterative Resonances"—iterative, naive, but persistent execution loops. Context is rebuilt fresh per iteration from the environment (file system/git state).
3.  **Cortical Matrix Orchestration:** The swarm is managed through functional roles and parallel execution. Tasks are isolated into temporary Git worktrees (Active Columns) to prevent state collision, with progress tracked via versioned, JSONL-based memory logs (Engram Traces).
4.  **C++ Bare-Metal Performance:** The entire orchestration layer and inference engine wrapper are written in C++ (leveraging `llama.cpp` and `ZeroMQ`), ensuring zero Python overhead and nanosecond-level inter-lobe communication.

## System Anatomy

### 1. Brainstem, Thalamus & Homeostasis (The Autonomic System)
*   **Role:** The core router and homeostatic controller.
*   **Technology:** ZeroMQ (Pub/Sub) and Autonomic Monitoring (CPU/RAM/Success Rate).
*   **Function:** All lobes publish "stimuli" (JSON payloads) to the Thalamus. The Thalamus routes these to the appropriate lobes. **Homeostasis** monitors system stress and success rates, triggering "Adrenaline Spikes" or "Sleep Cycles" autonomously.

### 2. Meta-Cognition Layer (Self-Observation)
*   **Role:** Higher-order reflective loop.
*   **Function:** Monitors homeostatic stress and REM evolution. Generates the system's "Thought Stream" (diary) and feeds real-time emotional state back to the communication interface.

### 3. Lobes (The Active Columns)
Each lobe is an independent microservice running a specific, tiny model (0.5B - 1.5B parameters) or a deterministic C++ script.

*   **Prefrontal Cortex (The Frontal Executive):** Task graph generation, reasoning, and delegation.
*   **Wernicke's Area (NLU):** Intent parsing and semantic understanding.
*   **Broca's Area (NLG):** Natural language generation for human interaction.
*   **Motor Cortex:** Code writing, bash execution, and tool usage.
*   **Amygdala:** Real-time priority classification and emotional tagging (alerts).
*   **Hippocampus:** RAG (Retrieval-Augmented Generation) management and engram persistence.

### 3. REM Engine (The Synaptic Optimizer)
*   **Role:** Background self-improvement and weight optimization.
*   **Function:** Activates during system idle periods to perform fine-tuning (LoRA) on successful interaction patterns, upgrading the system's "Executive" policy autonomously.

### 4. ModelManager (The Inference Engine)
To fit 100 models in 11GB VRAM, we use **LoRA (Low-Rank Adaptation) Multiplexing**.
*   A single foundational "Grey Matter" model (e.g., Qwen2.5-1.5B or Llama-3-1B) resides in VRAM.
*   The `ModelManager` rapidly swaps out ultra-lightweight LoRA adapters (10MB-50MB each) depending on which lobe is currently "firing" (processing a task).

## Memory Model: Engram Traces
NeuroSwarm eschews traditional monolithic databases for working memory. Instead, it uses **Engram Traces**: append-only JSONL files stored directly in the Git repository. Each engram represents a discrete thought, action, or state change, allowing any Iterative Resonance to pick up exactly where a previous one failed.

---
*True AGI exploration.*
