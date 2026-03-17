# ModelManager: The Synaptic Multiplexer

## 1. Overview
The **ModelManager** is the system's interface to the Large Language Model (LLM) backend. It abstracts the complexities of `llama.cpp` and provides functional lobes with a unified "inference firing" API.

## 2. Technical Strategy (v2.0.0)
To ensure system-wide stability across varying hardware, the ModelManager is configured with the following parameters:
*   **Engine:** `llama.cpp` (C++ Interface).
*   **Execution Mode:** CPU-Only (for deterministic stability and VRAM overhead avoidance).
*   **Context Window (n_ctx):** 4096 tokens (to support deep internal monologues).
*   **Batching (n_batch):** 2048 (optimized to prevent GGML assertion failures during long JSON planning).

## 3. LoRA Multiplexing
The manager supports dynamic loading of **Low-Rank Adapters (LoRA)**. 
*   **Base Model:** `qwen2.5-1.5b-instruct-q4_k_m.gguf`.
*   **Hot-Swapping:** Depending on the `adapter` field in the neural stimulus, the manager applies the corresponding weights (e.g., `executive`, `critic`, `nlu_specialist`) in milliseconds.

## 4. Stability Hooks
The implementation includes a custom logger (`brain_step.log`) that tracks every model load and inference cycle, providing granular diagnostics during multi-agent consensus loops.
