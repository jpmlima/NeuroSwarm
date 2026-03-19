# The NeuroSwarm Cognitive Cycle

## 1. Stimulus Ingestion (Sensation)
The cycle begins with a stimulus from the environment or from intrinsic motivation:
*   **Terminal:** Raw text from `broca_chat`.
*   **Auditory:** Sound path from `auditory_lobe`.
*   **Visual:** Saliency changes from `visual_lobe`.
*   **Intrinsic:** Goal selected by `basal_ganglia` via fitness function (Free Energy minimisation).

## 2. Semantic Mapping (NLU)
The **Wernicke Lobe** extracts the "intent-object" from the stimulus. It avoids raw conversation and converts the input into a structured technical summary.

## 3. Consensus Monologue (The "Thought" Phase)
The **Frontal Executive** receives the goal and proposes a plan. 
*   **Adversarial Loop:** The **Critic Lobe** attempts to find logical flaws.
*   **Iterative Refinement:** If rejected, the Executive re-thinks the strategy with the Critic's feedback. 
*   **Consensus:** Action is only authorized upon an `APPROVED` signal.

## 4. Counterfactual Reasoning (The "Dream" Phase)
Before physical execution, the **Motor Lobe** enters `mode: dream`.
*   **Sandbox Isolation:** A temporary worktree is created in `data/dreams/`.
*   **Verification:** The code or command is executed virtually. The system observes if it produces the intended effect.

## 5. Reality Collapse (Action)
If the dream is verified as successful, the Executive issues the final `mode: reality` request. The command is applied to the host machine.

## 6. Synaptic Consolidation (Learning)
*   **Immediate:** Success/Failure rates update the **Homeostasis** and **Meta-Cognition** layers.
*   **Self-Model Update:** Every `execution_result` is classified into one of 14 capability domains by the **BasalGanglia**. Success/failure counts, prediction errors, and novelty scores are updated in `data/self_model.json`. If the result reveals a novel capability or high prediction error, a `dopamine_signal` is emitted on the bus.
*   **Delayed:** During idle periods, the **REM Engine** uses the recorded success pattern to perform LoRA fine-tuning, physically updating the neural weights of the Executive.

## 7. Task Selection (Three-Tier Motivation)
When the Executive is idle and no active goals remain, `pick_next_task()` follows a three-tier priority:
1.  **Tier 1 — External tasks:** Highest-priority incomplete task from `tasks.json` (backward compatible Ralph loop).
2.  **Tier 2 — Intrinsic motivation:** FE publishes `intrinsic_goal_request`; BasalGanglia responds with the domain that maximises the fitness function F(d), along with concrete suggested commands for the model to select from.
3.  **Tier 3 — Epistemic fallback:** Hardcoded self-analysis goal (last resort if BasalGanglia is unresponsive).
