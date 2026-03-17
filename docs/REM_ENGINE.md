# REM Engine: Synaptic Consolidation and Active Learning

## 1. Overview
The **REM (Rapid Engram Modification) Engine** is the NeuroSwarm's offline optimization subsystem. It transitions the system from static inference to an evolving heuristic model by fine-tuning synaptic weights based on successful task resolutions.

## 2. The Learning Cycle (SFT)
During the REM phase (triggered by `Homeostasis` or `Thalamus`), the engine performs the following:
1.  **Memory Scanning:** Iterates through `data/engrams/*.jsonl` to identify `execution_result` nodes with `status: "success"`.
2.  **Dataset Construction:** Backtracks to the preceding `inference_request` (Prompt) and `inference_result` (Response). Formats these into a supervised fine-tuning (SFT) dataset in `data/train_data.txt`.
3.  **Weight Optimization:** Executes `./external/llama.cpp/build/bin/llama-finetune`.
    *   **Target:** Generates a LoRA adapter (`models/executive_new.gguf`).
    *   **Parameters:** 10 epochs (default), 4 threads, 512 context window.

## 3. Synaptic Deployment
Upon successful completion, the new adapter replaces `models/executive.gguf`. The `ModelManager` dynamically loads this adapter in the next `FrontalExecutive` cycle, effectively "remembering" successful patterns.

## 4. Hardware Requirements
*   **VRAM:** Training requires sufficient headroom to load the base model and gradient tensors.
*   **Model Compatibility:** Currently optimized for non-quantized base models (FP32/FP16) or supported LoRA-capable formats.
