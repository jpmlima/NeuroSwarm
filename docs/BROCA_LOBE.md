# Broca Lobe: Linguistic Synthesis Specification

## 1. Objective
To transform structured internal states (Engram Traces) into high-fidelity natural language output suitable for human consumption via chat or Text-to-Speech (TTS).

## 2. Input: The Semantic Stimulus
The Broca Lobe listens to the Nervous Bus (ZeroMQ) for intents tagged as `articulate_response`. It receives:
*   `internal_monologue`: The raw reasoning from the Frontal Executive.
*   `action_results`: The proprioceptive feedback from the Motor Cortex.
*   `emotional_bias`: Urgency and tone tags from the Amygdala.

## 3. The Synthesis Cycle (Iterative Resonance)
The Broca Lobe does not just "print" text. it resonates until the output matches the required emotional and technical fidelity:
1.  **Drafting:** Generate a candidate sentence using a tiny SLM (e.g., Qwen2.5-0.5B).
2.  **Refinement:** Apply LoRA adapters for specific "personalities" or "languages".
3.  **Articulation:** Broadcast the final string to the `Vocal Tract` (TTS) and `Interface` (Chat) ports.

## 4. Resource Profile
*   **Hardware:** Optimized for 4-bit quantization.
*   **VRAM Target:** < 400MB (using shared Gray Matter via ModelManager).
*   **Latency:** < 200ms for initial token generation.
