# REM Engine — Sleep Consolidation and Model Fine-Tuning

## Overview

The REM Engine activates during sleep cycles and performs four tasks: knowledge synthesis from execution history, memory consolidation in the Hippocampus, genome evolution, and model fine-tuning. It is the primary mechanism through which transient experience becomes permanent capability.

## Trigger

Sleep cycles are initiated by two mechanisms:

1. **Idle trigger** — Homeostasis sends `initiate_sleep_cycle` when CPU load stays below 5% for 60 seconds
2. **Circadian rhythm** — Homeostasis fires a sleep cycle every 10 minutes regardless of load

The second mechanism guarantees consolidation even when the system is continuously active.

## What Happens During Sleep

```
1. KNOWLEDGE SYNTHESIS
   - Load last 500 execution traces from data/engrams/global_stream.jsonl
   - Compute success rate per mode (reality / dream / neuro_surgery)
   - Extract success/failure token patterns (command vocabulary)
   - Write data/system_knowledge.md
   - Broadcast prompt_update → FrontalExecutive updates its prompt live

2. MEMORY CONSOLIDATION (via Hippocampus)
   - consolidate_memories intent triggers importance-based archival
   - Per-CID ledgers sorted by importance + timestamp
   - Top 200 + high-importance entries remain in active ledger
   - Rest archived to *_consolidated.jsonl
   - All ledgers >100KB are bulk-consolidated
   - memory_index.jsonl compacted if >50K entries
   - Stale pending embed requests (>60s) are expired

3. GENOME EVOLUTION
   - Load data/command_genome.json
   - Prune operators with fitness <0.1 and generation >20
   - Crossover: swap pipe stages between high-fitness templates
   - Cap at 20 templates per domain
   - Save evolved genome

4. TRAINING DATA EXPORT + FINE-TUNING
   - If 50+ new successful reality-mode traces since last export:
     - Filter trivial commands (echo, true, pwd, etc.)
     - Deduplicate and export as ChatML plain text
     - Spawn llama-finetune (CPU-only, no VRAM contention)
     - On completion: broadcast training_complete
     - FrontalExecutive switches to fine-tuned model
```

## Training Data Quality

Exported traces are filtered through a TRIVIAL_CMDS list that excludes commands with no learning value (`echo`, `true`, `test -f`, `pwd`, `cat /dev/null`, etc.). Each trace is deduplicated by exact command match. Only reality-mode successes are exported — dream and surgery traces are excluded.

## Fine-Tuning Pipeline

The system uses `llama-finetune` (from llama.cpp) for full model fine-tuning, not LoRA. Training data is exported as plain text in ChatML format (`-f` flag). Key parameters:

- Output: `./models/finetuned/qwen2.5-rem-{timestamp}.gguf`
- Validation split: 10%
- Flash attention enabled
- CPU-only execution (avoids VRAM conflict with inference)

Export state is persisted to `./data/training/.export_state` to track the last export count and resume correctly across restarts.

## Bus Messages

**Receives:** `initiate_sleep_cycle` from homeostasis

**Sends:**
- `prompt_update` — FrontalExecutive injects updated behavioural knowledge
- `sleep_cycle_complete` — resets circadian timer, updates dashboard counter
- `training_complete` — FrontalExecutive switches to fine-tuned model adapter
