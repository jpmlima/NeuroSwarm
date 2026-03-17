# Wernicke Lobe: Semantic Comprehension Specification

## 1. Objective
To transform raw, unstructured linguistic stimuli (text/speech) into high-fidelity, structured Engram Traces (JSON) containing Intent, Entities, and Context.

## 2. Input: Sensory Stream
The Wernicke Lobe listens to the Nervous Bus (ZeroMQ) for afferent stimuli tagged as `raw_linguistic_input`. This data usually originates from:
*   `auditory_cortex`: Transcribed speech from Whisper.
*   `interface_port`: Direct text input from the user.

## 3. Parsing Logic (Semantic Resonator)
The Wernicke Lobe employs a two-tier comprehension model:
1.  **Heuristic Mapping:** Rapid keyword-based intent detection (High speed, zero VRAM).
2.  **Semantic Refinement:** Utilizes a tiny SLM (e.g., SmolLM-135M) via the `ModelManager` to resolve complex instructions or ambiguous pronouns.

## 4. Output: The Structured Engram
The final output is a JSON payload containing:
*   `intent`: The primary goal (e.g., `execute_command`, `query_memory`, `express_emotion`).
*   `entities`: List of identified files, paths, people, or objects.
*   `confidence`: A float representing semantic certainty.
