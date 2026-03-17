# Amygdala: Priority Gating Specification

## 1. Objective
To protect the system's metabolic resources (GPU/VRAM) by filtering and tagging incoming stimuli based on urgency, importance, and emotional tone.

## 2. Priority Levels (The Salience Map)
*   **P0: Background** - Routine logging (e.g., periodic file system scans).
*   **P1: Normal** - Standard user requests.
*   **P2: Interest** - Novel patterns detected in logs or vision.
*   **P3: Alert** - Recoverable errors (e.g., command retry needed).
*   **P4: Critical** - High-importance events (e.g., human-in-the-loop required).
*   **P5: Hijack** - Immediate emergency (e.g., thermal critical, hardware failure, or destructive command confirmation).

## 3. The Hijack Mechanism
If a stimulus is tagged as **P5**, the Amygdala broadcasts a `GLOBAL_HALT` signal to the Thalamus. This pauses all lower-priority Iterative Resonance cycles to focus 100% of the GPU/CPU on the emergency.

## 4. Implementation Strategy
*   **Technology:** Pure C++ using a fast Keyword/Heuristic Engine combined with a tiny CPU-based classifier (e.g., FastText or a small Random Forest).
*   **Latency Target:** < 5ms (Must be faster than the Thalamus routing logic).
