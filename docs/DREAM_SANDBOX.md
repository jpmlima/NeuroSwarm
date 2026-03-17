# Dream Sandbox: World Simulation & Counterfactual Reasoning

## 1. Overview
The **Dream Sandbox** is the NeuroSwarm's simulation layer. It allows the system to perform "counterfactual reasoning"—predicting the outcome of an action by simulating it in an isolated environment before committing to physical reality.

## 2. The Simulation Protocol
The cognitive loop now includes a mandatory **Dream Phase**:
1.  **Sandbox Isolation:** The `Motor Lobe` creates a temporary workspace in `data/dreams/`.
2.  **Virtual Execution:** Commands are executed within this sandbox.
3.  **Result Analysis:** The `Frontal Executive` observes the exit codes and output of the simulation.
4.  **Reality Collapse:** Only if the simulation is verified as successful does the system replicate the action in the host environment (`mode: reality`).

## 3. Benefits for AGI
*   **Zero-Risk Exploration:** The system can test dangerous or unknown commands safely.
*   **Error Correction:** Failures in the "dream" provide immediate feedback for re-planning without real-world consequences.
*   **Validation:** Ensures that complex multi-step plans actually work before execution begins.
