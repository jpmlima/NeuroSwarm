# Synaptic Controller (ModelManager) Specification

## Overview
The `ModelManager` is the bare-metal execution environment for the NeuroSwarm lobes. It leverages `llama.cpp` as the primary inference backend and implements a high-speed **LoRA Multiplexer**.

## Core Strategy: Weights as Tissue, Adapters as Logic
To fit 100+ specialized functional units into 11GB of VRAM, the engine follows a "Shared Tissue" model:
1.  **Foundational Gray Matter:** A single, high-performance SLM (Small Language Model) like *Qwen2.5-1.5B-Instruct* or *Llama-3.2-1B* is loaded into VRAM in 4-bit or 8-bit quantization.
2.  **Specialized Synapses:** Each brain lobe (Amygdala, Wernicke, Motor) is represented by a **LoRA Adapter** (Rank 8-64).
3.  **Micro-Switching:** When a message arrives via the `Thalamus` (ZeroMQ), the `ModelManager` applies the corresponding LoRA weights to the base model tensors *without* a full reload. Latency is minimized to the microsecond range.

## Communication Protocol: The Synaptic Fire
Lobes do not exchange "vibe-based" chat history. They exchange **Encoded Stimuli**:
*   **Input:** Current task engram + relevant environment state (from Git/FS).
*   **Output:** Deterministic action or semantic extraction.
*   **Iterative Resonance Enforcement:** The `ModelManager` resets the KV Cache between every fire to prevent "context rot" and ensure 100% predictable output.

## VRAM Allocation Map (11GB Target)
| Component | Memory Usage | Note |
| :--- | :--- | :--- |
| Base Model (Q4_K_M) | ~1.2 GB | Persistent "Gray Matter" |
| KV Cache (4K Context) | ~0.5 GB | Shared across all firing events |
| LoRA Buffer (Swappable) | ~0.3 GB | Holds active adapters |
| System/Overhead | ~1.0 GB | OS and ZeroMQ buffers |
| **Available for Swarm** | **~8.0 GB** | Massive headroom for parallel Iterative Resonances |
