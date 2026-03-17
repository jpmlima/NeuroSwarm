# Hippocampus Lobe: Neural Persistence & RAG Engine

## Overview
The **Hippocampus Lobe** serves as the central repository for the NeuroSwarm's working memory and long-term state. It implements a high-performance retrieval system for **Engram Traces**, enabling the "Iterative Resonance" loop to maintain context across asynchronous task cycles without bloating the Synaptic Controller's KV cache.

## Technical Specification

### 1. Architectural Role
The Hippocampus is a passive/active hybrid lobe. It asynchronously ingests neural stimuli from the **Thalamus (Neural Bus)** and indexes them into a versioned JSONL ledger. When queried by the **Frontal Executive (Cortex)**, it performs semantic retrieval to reconstruct the "Current State of Mind."

### 2. Engram Trace Protocol
Memory is stored as atomic **Engrams**. Each engram is a JSON object containing:
- `cid`: Correlation ID (The "Thought Stream" identifier).
- `synapse_ts`: Unix timestamp of the firing.
- `origin`: The source lobe (e.g., `motor_cortex`, `amygdala`).
- `payload`: The raw data or observation.
- `vector_id`: (Future) Pointer to a FAISS/Hnswlib vector index.

### 3. Retrieval Strategy: Synaptic Replay
Instead of maintaining a massive context window, NeuroSwarm uses **Synaptic Replay**:
1. The **Frontal Executive** sends a `RECALL_REQUEST` with a `cid`.
2. The **Hippocampus** reads the associated `.jsonl` ledger.
3. It filters and summarizes the last $N$ engrams.
4. It injects this "distilled experience" back into the **Synaptic Controller**'s next inference cycle.

## Engineering Standards
- **Thread Safety:** Implements `std::mutex` for ledger I/O to prevent race conditions during parallel lobe firings.
- **Data Integrity:** Append-only architecture ensures a non-repudiable audit log of all "thoughts" and "actions."
- **Performance:** Designed for O(1) writes and O(N) linear retrieval (optimized with indexing in future iterations).

---
*NeuroSwarm Engineering - High-Performance Biomimetic Systems*
