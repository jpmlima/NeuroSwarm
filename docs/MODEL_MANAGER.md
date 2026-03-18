# ModelManager — LLM & Embedding Backend

## Overview

ModelManager is the interface between NeuroSwarm and `llama.cpp`. It manages two independent model contexts:

1. **Generative context** — Qwen2.5-1.5B-Instruct (Q4_K_M), used for all inference requests. Grammar-constrained via GBNF to enforce JSON output.
2. **Embedding context** — nomic-embed-text-v1.5 (Q8_0, 137M), dedicated model for semantic embeddings. Mean-pooled, L2-normalised output.

The two contexts share no state. The embedding model is loaded separately so embedding requests never interfere with the generation KV cache.

## API

```cpp
// Text generation — returns raw LLM output (JSON when grammar is set)
std::string fire(const std::string& adapter_name,
                 const std::string& prompt,
                 const std::string& grammar_str = "");

// Semantic embedding — returns L2-normalised float vector (768-dim for nomic-embed)
std::vector<float> get_embeddings(const std::string& text);

bool is_alive() const; // both contexts initialised
```

## Models

| Context | Model | Size | Purpose |
|---|---|---|---|
| Generative | `qwen2.5-1.5b-instruct-q4_k_m.gguf` | ~1GB | All inference |
| Embedding | `nomic-embed-text-v1.5.Q8_0.gguf` | ~137MB | Semantic search |

## Inference Parameters

- `n_ctx = 4096` — generative context window
- `n_batch = 32` — conservative batch size for stability
- Grammar enforcement via `llama_sampler_init_grammar` (GBNF)
- Temperature: 0.7, repetition penalties enabled
- Max generation: 1024 tokens

## Embedding Parameters

- `n_ctx = 2048` — sufficient for task descriptions
- `pooling_type = LLAMA_POOLING_TYPE_MEAN` — mean pooling over all tokens
- Output: L2-normalised vector, cosine similarity = dot product
- Fallback: if nomic-embed fails to load, falls back to base model context

## Adapter Names

The `adapter_name` parameter in `fire()` is used for logging and routing only — all adapters currently use the same base model. Planned: load different GGUF files per adapter (e.g., Qwen-Coder for `neuro_surgery`).

Current adapter names: `executive`, `critic`, `nlu_specialist`, `default`
