# Synaptic Protocol Specification (v1.0)

## 1. Communication Layer: The Neural Bus
*   **Transport:** ZeroMQ (ipc:// for local, tcp:// for remote).
*   **Pattern:** ROUTER/DEALER for synchronous requests; PUB/SUB for sensory broadcasts.
*   **Performance Target:** < 1ms inter-lobe latency.

## 2. Message Format: The "Engram"
Every signal in the brain is a **Engram**. Engram Traces are structured JSON (simdjson-ready) containing:
*   `cid`: Correlation ID (the "Thought Stream" ID).
*   `origin`: Source lobe (e.g., "auditory_cortex").
*   `priority`: 0 (Background) to 5 (Amygdala Hijack/Emergency).
*   `payload`: The actual stimulus or action.
*   `context_ref`: A Git SHA or file path (Environment as Memory).

## 3. The Iterative Resonance Lifecycle
1.  **Stimulus:** Thalamus receives a Engram.
2.  **Gating:** Amygdala assesses priority.
3.  **Inference:** ModelManager fires the base SLM with the required LoRA.
4.  **Action:** Motor Cortex executes (Bash/File/Keyboard).
5.  **Proprioception:** Result is captured, a new Engram is generated, and the loop repeats until `<done>` is reached.

## 4. Resource Gating (Homeostasis)
*   **Active Mode:** Lobe is in VRAM (Hot).
*   **Dormant Mode:** Lobe weights are in System RAM (Warm).
*   **Pruned Mode:** Lobe is on Disk (Cold).
