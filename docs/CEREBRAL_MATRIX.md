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
