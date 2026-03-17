# Visualizer Lobe: Neural Observability (EEG)

## 1. Overview
The **Visualizer Lobe** provides high-fidelity observability into the NeuroSwarm's internal stimuli and cognitive state. It implements a bridge between the asynchronous ZMQ neural bus and a web-based diagnostic dashboard.

## 2. Technical Stack
*   **Backend:** C++ implementation using `cpp-httplib` for an ultra-lightweight web server.
*   **Frontend:** Three.js for 3D neural mesh rendering and high-performance data visualization.
*   **Communication:** Periodically polls the ZMQ Thalamus bus and serves events via a JSON endpoint (`/events`).

## 3. Design Philosophy: Scandinavian Minimalism
The interface follows a **Scandinavian Deep-Dive** aesthetic:
*   **Functional Focus:** Prioritizes data clarity and technical precision over decorative elements.
*   **Cortex Mesh:** A subtle 3D point cloud representing the brain's global structure.
*   **Synaptic Firing:** Lobe nodes (icosahedrons) react in real-time to inter-lobe communication using a high-contrast surgical cyan (`#00e5ff`) pulse.
*   **Metrics:** Real-time tracking of System Stress and Success Efficiency.

## 4. Operation
*   **Address:** Typically serves on `http://localhost:8080`.
*   **Distributed Support:** Can be configured to connect to a remote Thalamus using the `--thalamus <IP>` argument.
