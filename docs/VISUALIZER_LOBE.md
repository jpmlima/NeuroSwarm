# Visualizer Lobe: Neural Observability (EEG)

## 1. Overview
The **Visualizer Lobe** provides observability into the NeuroSwarm's internal stimuli and cognitive state. It implements a bridge between the asynchronous ZMQ neural bus and a web-based diagnostic dashboard.

## 2. Technical Stack
*   **Backend:** C++ implementation using `cpp-httplib` for an ultra-lightweight web server.
*   **Frontend:** Three.js for 3D neural mesh rendering and data visualization.
*   **Communication:** Periodically polls the ZMQ Thalamus bus and serves events via a JSON endpoint (`/events`).

## 3. Operation
*   **Address:** Typically serves on `http://localhost:8080`.
*   **Distributed Support:** Can be configured to connect to a remote Thalamus using the `--thalamus <IP>` argument.
