# Hippocampus Lobe: Semantic & Vector Retrieval

## 1. Overview
The **Hippocampus Lobe** manages the persistent storage and conceptual retrieval of the system's memory. In v1.6.0, it transitioned from simple keyword matching to **Neural Vector Search**, allowing for higher-order semantic reasoning.

## 2. Semantic Embedding Pipeline
Every engram archived by the system is vectorized:
1.  **Request:** The Hippocampus sends an `embedding_request` to the `Synaptic Controller`.
2.  **Generation:** The controller uses the base model to generate a high-dimensional vector representing the text's semantic meaning.
3.  **Storage:** The engram is stored in `data/engrams/*.jsonl` with an attached `embedding` array.

## 3. Retrieval Engine (Semantic Search)
When a search is requested:
*   **Vectorization:** The search query is vectorized.
*   **Cosine Similarity:** The lobe performs a mathematical dot-product comparison between the query vector and all archived engrams.
*   **Ranking:** Memories are ranked by their conceptual proximity to the query, providing the Executive with the most relevant historical context even if the exact keywords differ.

## 4. Distributed Memory Nodes
The Hippocampus can operate on a separate machine from the Thalamus and Synaptic Controller.
*   **Connection:** Use `--thalamus <IP>` to connect to the central nervous bus.
*   **Offloading:** This allows heavy memory indexing to be offloaded from the main GPU/CPU host.
