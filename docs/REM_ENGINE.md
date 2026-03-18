# REM Engine — Behavioural Learning During Sleep

## Overview

The REM Engine activates during idle periods (triggered by Homeostasis when CPU load is low for ~1 minute). It analyses recent execution history and distils actionable behavioural knowledge into a prompt that the FrontalExecutive injects into every future inference.

This is the system's primary self-improvement mechanism. It does not modify model weights — it improves by accumulating better context.

## Trigger

Homeostasis sends `initiate_sleep_cycle` when the system has been idle. The REM Engine is the only subscriber that acts on this signal.

## What Happens During Sleep

```
1. Load last 100 execution traces from data/engrams/global_stream.jsonl
2. Correlate execution_request (command + mode) with execution_result (success/failure)
3. Calculate:
   - Overall success rate
   - Success rate per mode (reality / dream / neuro_surgery)
   - Command vocabulary correlated with success vs. failure
   - Most recent failures (with error output)
   - Most recent successes (command pattern)
4. Derive behavioural directives from the data
5. Write data/system_knowledge.md
6. Broadcast prompt_update on the bus
7. FrontalExecutive updates its system prompt live
```

## Output — data/system_knowledge.md

```markdown
# BEHAVIORAL KNOWLEDGE (updated Thu Mar 18 01:23:00 2026)

## Performance Summary
- Analysed 47 recent executions
- Overall success rate: 62%

## Success Rate by Mode
- `reality`: 71% (15/21)
- `dream`: 55% (12/22)
- `neuro_surgery`: 25% (1/4)

## Command Vocabulary Analysis
- Patterns correlated with SUCCESS: `ls` `cat` `echo` `python3` `mkdir`
- Patterns correlated with FAILURE: `git` `apply` `patch` `cmake`

## Behavioral Directives (derived from above)
- CAUTION: Neuro-surgery has failed >50% of the time. Prefer single atomic file edits.
```

## Bus Messages

**Receives:** `intent: "initiate_sleep_cycle"` from `homeostasis`

**Sends:**
- `intent: "prompt_update"` — FrontalExecutive updates its system knowledge live
- `intent: "sleep_cycle_complete"` — Visualizer updates the dashboard counter

## What It Does NOT Do

- Does not fine-tune model weights (requires full-precision model, not GGUF)
- Does not modify source code
- Does not restart any process

Fine-tuning is on the roadmap for when a sufficient dataset exists and a full-precision model is available.
