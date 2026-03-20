# Visualizer Lobe: Cognitive Observability Dashboard

## 1. Overview
The **Visualizer Lobe** provides real-time observability into NeuroSwarm's cognitive state. It renders a VOSviewer-inspired 2D network graph showing all active lobes, their interconnections, and system-wide metrics — designed for academic presentation and thesis documentation.

## 2. Technical Stack
*   **Backend:** C++ implementation using `cpp-httplib` for an ultra-lightweight web server.
*   **Frontend:** Pure HTML5 Canvas for 2D network graph rendering. No external JS dependencies.
*   **Communication:** Subscribes to all traffic on the ZMQ Thalamus bus and serves events via a JSON endpoint (`/events`).
*   **Typography:** Inter (sans-serif) + IBM Plex Mono (monospace) — clean academic styling.
*   **Theme:** White/light professional theme with muted Tailwind-inspired colour palette.

## 3. Dashboard Layout

### Centre — Network Graph (Canvas)
*   16 core lobe nodes with cluster-based colouring (cognitive core, memory, sensory, autonomic, execution, observability)
*   Radial gradient density clouds per cluster (VOSviewer-style)
*   Curved bezier edges representing known pub/sub connections (22 static edges)
*   Node size proportional to cognitive importance (FrontalExecutive largest)
*   Animated edge particles showing message flow direction
*   Dynamic specialist nodes appear/disappear via neurogenesis/apoptosis events

### Left Panel — System State
*   **Active Lobes** — count with coloured status indicator
*   **Active Drive** — current Maslow hierarchy level (SURVIVAL → HOMEOSTASIS → EXPLORATION → MASTERY → SELF_MODIFY) with colour coding and target domain
*   **System Metrics** (2x2 grid):
    - Success Rate (%) — from `homeostatic_pulse`
    - Stamina (%) — from `homeostatic_pulse`
    - Tasks Done — incremented on each `execution_result`
    - REM Cycles — incremented on `initiate_sleep_cycle`
*   **Neurogenesis Stats** — Specialists (active count), Genesis (total generated), Apoptosis (total terminated)
*   **Learning Curve** — sparkline of success rate over last 30 data points

### Right Panel — Event Stream
*   Chronological log of bus events with origin tags and colour coding
*   Auto-scrolling with newest events at top

### Bottom Bar — Spike Task Ticker
*   Horizontal scrolling ticker showing completed spike tasks by order of entry
*   NeuroSwarm version watermark (bottom-right)

## 4. Data Sources

| Event | Fields Used |
|---|---|
| `homeostatic_pulse` | `success_rate`, `stamina`, `cpu_load`, `ram_used_gb`, `gpu_load` |
| `intrinsic_goal` | `drive_level`, `domain`, `fitness` |
| `execution_result` | `status`, `command`, `cid` (task count) |
| `lobe_injected` | `lobe_name` (genesis count, specialist node) |
| `lobe_terminated` | `lobe_name` (apoptosis count, remove specialist node) |
| `initiate_sleep_cycle` | (REM cycle count) |
| `spike_done` | `cid`, `status` (task ticker) |

## 5. Operation
*   **Address:** `http://localhost:8080`
*   **Distributed Support:** Can be configured to connect to a remote Thalamus using the `--thalamus <IP>` argument.
*   **Refresh Rate:** Events polled every 500ms via `/events` endpoint.
