# Inner Monologue & Synaptic Consensus

## 1. Overview
The **Inner Monologue** is a recursive cognitive protocol introduced in v1.8.0. It transforms the system from a reactive agent into a reflective organism by requiring multiple functional lobes to reach a consensus before environmental interaction occurs.

## 2. The Adversarial Cycle
Unlike standard LLM chains, NeuroSwarm utilizes an adversarial relationship between two specialized adapters:
1.  **Frontal Executive (Proposer):** Generates high-level plans and tactical steps based on user stimuli.
2.  **Critic Lobe (Validator):** Analyzes the Executive's plan for logical fallacies, security risks (e.g., destructive bash commands), and efficiency.

## 3. Communication Protocol
The monologue occurs via the ZMQ neural bus using the `internal_thought` intent:
*   `FE` publishes `internal_thought`.
*   `CL` intercepts and publishes `inference_request` with the `critic` adapter.
*   `CL` receives result and publishes `consensus_feedback`.
*   If status is not `APPROVED`, `FE` re-processes the goal with the feedback injected into its context.

## 4. Engineering Impact
This architecture allows emergent intelligence to surface from the interaction of small (1.5B) models. By forcing the system to "think twice," the accuracy of complex task execution is significantly increased without increasing the base model size.
