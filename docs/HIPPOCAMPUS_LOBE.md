# Hippocampus Lobe: Episodic Memory and Consolidation

## 1. Overview

The Hippocampus manages episodic memory storage, semantic retrieval, and memory consolidation. It implements three memory tiers: short-term per-CID ledgers, a long-term embedded index, and trajectory sequences for multi-step command patterns.

## 2. Memory Storage

**Per-CID Ledgers** — every execution result and neural event is appended to `data/engrams/{cid}.jsonl`. Each engram is tagged with:
- `synapse_ts` — timestamp
- `importance` — default 1.0, raised for significant events
- `success` — boolean outcome

**Global Stream** — all events are also recorded in `data/engrams/global_stream.jsonl` for cross-CID analysis.

## 3. Semantic Embedding Pipeline

Execution results are embedded via the Nomic-Embed model (768-dim vectors):

1. Hippocampus constructs a descriptive text: `"SUCCESS: {command} RESULT: {output}"` (or FAILURE)
2. Sends `embedding_request` to SynapticController with a unique embed CID
3. On `embedding_result`, the engram + embedding are stored in `data/engrams/memory_index.jsonl`

**Pending Embed Cleanup** — requests that receive no response within 60 seconds are expired. This prevents unbounded growth of the pending_embeds map.

## 4. Retrieval

**Semantic Search** (`search_memory`) — query is embedded, then compared against all indexed engrams via cosine similarity. Top 5 results are returned with similarity scores. Falls back to keyword search if embedding times out (10s deadline).

**Recall** (`recall_memory`) — returns the N most recent engrams from a CID's ledger (short-term memory).

**Trajectory Search** (`search_trajectory`) — keyword-scored search over multi-step command sequences. Scoring: domain match (+3), query substring in summary (+2), successful outcome (+1).

## 5. Memory Consolidation

Triggered by `consolidate_memories` intent (sent by REM Engine during sleep cycles):

**Per-CID Consolidation:**
1. Load all engrams from the CID's ledger
2. Sort by importance (descending), then by timestamp
3. Keep the top 200 entries + all high-importance (≥0.5) + all successes in the active ledger
4. Archive the rest to `{cid}_consolidated.jsonl`
5. If active set still exceeds 400 entries, cap at 400

**Bulk Consolidation:** all per-CID ledgers exceeding 100KB are automatically consolidated (up to 20 per cycle).

**Memory Index Compaction:** when `memory_index.jsonl` exceeds 50,000 entries:
1. Score each entry: `importance × 0.6 + recency × 0.3 + success × 0.1`
2. Keep top 50,000 entries
3. Write atomically via temp file rename

## 6. Trajectory Memory

Multi-step command sequences within a goal are tracked per CID:

- Steps accumulate while the CID is active
- After 120 seconds of inactivity (or 240s for single-step), the trajectory is finalised
- Sequences of ≥2 steps are persisted to `data/trajectories.jsonl` with domain, outcome, and timing

## 7. Periodic Maintenance

Every 5 minutes (independent of sleep cycles):
- Stale pending embed requests are expired
- Memory index is compacted if oversized
- Stale trajectory buffers are finalised

## 8. Bus Messages

**Receives:**
- `execution_result` — record engram, request embedding, track trajectory
- `search_memory` — semantic search with embedding
- `recall_memory` — short-term recall from CID ledger
- `consolidate_memories` — trigger consolidation pipeline
- `search_trajectory` — keyword search over trajectories
- `embedding_result` — resolve pending embed, store indexed memory

**Sends:**
- `memory_recalled` — recent traces for a CID
- `search_result` — semantic or keyword search results
- `consolidation_complete` — consolidation stats (kept/archived counts)
- `trajectory_result` — matched command sequences
- `embedding_request` — request embedding for new engram

## 9. Distributed Operation

The Hippocampus can run on a separate machine: `--thalamus <IP>` connects to the central bus. This offloads memory indexing from the main GPU host.
