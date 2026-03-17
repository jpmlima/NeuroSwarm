# Motor Cortex (Execution Lobe) Specification

## 1. Biological Function
In the human brain, the motor cortex generates the neural impulses that control the execution of movement. In NeuroSwarm, the Motor Cortex executes discrete, deterministic actions on the host operating system.

## 2. The Iterative Resonance Integration
The Motor Cortex is the endpoint of the **Iterative Resonance**.
1.  **Stimulus:** Receives an execute_bash intent from the Thalamus.
2.  **Action:** Spawns a child process.
3.  **Proprioceptive Feedback:** Captures stdout, stderr, and exit_code.
4.  **Synaptic Return:** Fires the result back to the Thalamus.
