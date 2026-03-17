# Auditory Lobe: Afferent Acoustic Specification

## 1. Objective
To provide the Cerebral Matrix with real-time acoustic perception by transcribing vocal stimuli into semantic Engram Traces.

## 2. Sensory Pathway: The Cochlea-to-Cortex
*   Continuous Sampling: Listens to the system default microphone via a low-latency C++ audio API.
*   VAD (Voice Activity Detection): A lightweight C++ heuristic to detect speech before firing the heavy transcription engine.

## 3. Transcription Engine (Whisper Core)
*   Technology: Integrated whisper.cpp (C++ bare-metal).
*   Hardware Allocation: Runs strictly on CPU.
*   Output: Generates a raw_linguistic_input stimulus and fires it into the Nervous Bus.

## 4. Resource Allocation
*   CPU Impact: < 10% on modern multicore systems.
*   VRAM Impact: 0MB.
*   Latency: < 500ms for sentence-level transcription.
