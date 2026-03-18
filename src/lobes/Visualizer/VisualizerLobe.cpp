#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <deque>
#include <ctime>

using json = nlohmann::json;

namespace neuroswarm {

class VisualizerLobe {
public:
    VisualizerLobe(const std::string& thalamus_ip = "localhost") 
        : ctx(1), sub(ctx, zmq::socket_type::sub) {
        
        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, "");
        
        std::cout << "[VISUALIZER] EEG Subsystem online. Web Dashboard on http://localhost:8080" << std::endl;
    }

    void start() {
        std::thread zmq_thread(&VisualizerLobe::listen_zmq, this);
        zmq_thread.detach();

        httplib::Server svr;

        svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(get_dashboard_html(), "text/html");
        });

        svr.Get("/events", [this](const httplib::Request&, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(event_mutex);
            json data = event_buffer;
            res.set_content(data.dump(), "application/json");
        });

        svr.listen("0.0.0.0", 8080);
    }

private:
    zmq::context_t ctx;
    zmq::socket_t sub;
    std::mutex event_mutex;
    std::deque<json> event_buffer;
    const size_t max_events = 300;

    void listen_zmq() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                try {
                    auto j = json::parse(static_cast<char*>(msg.data()), static_cast<char*>(msg.data()) + msg.size());
                    std::lock_guard<std::mutex> lock(event_mutex);
                    j["ui_ts"] = std::time(nullptr);
                    event_buffer.push_front(j);
                    if (event_buffer.size() > max_events) event_buffer.pop_back();
                } catch (...) {}
            }
        }
    }

    std::string get_dashboard_html() {
        return R"V0G0(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>NEUROSWARM // EVOLUTION DASHBOARD</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        :root {
            --bg: #030303;
            --panel: #0a0a0a;
            --accent: #00f2ff;
            --accent-dim: rgba(0, 242, 255, 0.1);
            --danger: #ff0055;
            --success: #00ffaa;
            --evolution: #aa00ff;
            --text-main: #f0f0f0;
            --text-dim: #555555;
            --border: #151515;
        }
        * { box-sizing: border-box; }
        body { 
            margin: 0; background: var(--bg); color: var(--text-main); 
            font-family: 'Inter', 'Segoe UI', sans-serif;
            display: grid; grid-template-columns: 320px 1fr 400px; height: 100vh;
            overflow: hidden;
        }

        /* Sidebars */
        .sidebar {
            background: var(--panel); border-right: 1px solid var(--border);
            display: flex; flex-direction: column; padding: 15px; gap: 15px;
            overflow-y: auto;
        }
        .sidebar-right { border-right: none; border-left: 1px solid var(--border); }

        /* Panels */
        .panel { border: 1px solid var(--border); padding: 15px; position: relative; background: rgba(10,10,10,0.8); border-radius: 4px; }
        .panel-label { font-size: 10px; font-weight: 800; text-transform: uppercase; color: var(--accent); margin-bottom: 12px; letter-spacing: 1.5px; opacity: 0.8; }
        
        /* Evolutionary Metrics */
        .evo-stat { text-align: center; margin-bottom: 20px; }
        .evo-value { font-size: 32px; font-weight: 900; color: var(--evolution); text-shadow: 0 0 15px var(--evolution); }
        .evo-label { font-size: 10px; color: var(--text-dim); text-transform: uppercase; }

        /* Homeostatic Details */
        .homeo-card { display: flex; flex-direction: column; gap: 10px; }
        .homeo-metric { background: #000; padding: 10px; border-radius: 3px; border-left: 3px solid var(--border); }
        .homeo-metric.danger { border-left-color: var(--danger); }
        .homeo-metric.success { border-left-color: var(--success); }
        .h-label { font-size: 11px; color: #888; display: block; }
        .h-val { font-size: 16px; font-weight: bold; }
        .h-desc { font-size: 9px; color: #444; font-style: italic; }

        /* Breakthroughs */
        #breakthroughs { flex-grow: 1; overflow-y: auto; display: flex; flex-direction: column; gap: 10px; }
        .bt-entry { background: #111; padding: 10px; border-radius: 4px; border: 1px solid #1a1a1a; animation: slideIn 0.3s ease-out; }
        .bt-tag { font-size: 9px; font-weight: bold; padding: 2px 5px; border-radius: 2px; margin-bottom: 5px; display: inline-block; }
        .bt-text { font-size: 11px; line-height: 1.4; color: #ccc; }
        .bt-time { font-size: 9px; color: #444; float: right; }

        @keyframes slideIn { from { transform: translateX(20px); opacity: 0; } to { transform: translateX(0); opacity: 1; } }

        /* Main Viewport */
        #main-view { position: relative; display: flex; flex-direction: column; }
        #viewport { flex-grow: 1; }
        #osd {
            position: absolute; top: 20px; left: 20px; right: 20px;
            display: flex; gap: 20px; pointer-events: none;
        }
        .osd-panel { background: rgba(0,0,0,0.8); border: 1px solid var(--border); padding: 15px; border-radius: 4px; flex: 1; }

        /* Logs */
        #log-content { font-size: 9px; display: flex; flex-direction: column; gap: 4px; overflow-y: auto; height: 150px; border-top: 1px solid #111; padding-top: 10px; }
        .log-entry { color: #555; }
        .log-origin { color: var(--accent); font-weight: bold; margin-right: 5px; }

        #thought-console { background: #000; border: 1px solid #111; padding: 10px; height: 350px; overflow-y: auto; font-family: monospace; font-size: 11px; }
    </style>
</head>
<body>
    <div class="sidebar">
        <div class="evo-stat">
            <div id="evo-count" class="evo-value">0</div>
            <div class="evo-label">Evolutionary Memories</div>
        </div>

        <div class="panel">
            <div class="panel-label">Homeostatic Health</div>
            <div class="homeo-card">
                <div id="stress-card" class="homeo-metric">
                    <span class="h-label">Cognitive Friction (Stress)</span>
                    <span id="stress-val" class="h-val">0.00</span>
                    <span class="h-desc">High friction causes system paralysis.</span>
                </div>
                <div id="success-card" class="homeo-metric">
                    <span class="h-label">Synaptic Accuracy (Success)</span>
                    <span id="success-val" class="h-val">1.00</span>
                    <span class="h-desc">Percentage of goals achieved recently.</span>
                </div>
            </div>
            <div style="height: 100px; margin-top: 15px;"><canvas id="homeoChart"></canvas></div>
        </div>

        <div class="panel">
            <div class="panel-label">Neural Workload</div>
            <div class="metric"><span class="metric-label">Inference Load</span><span id="gpu-val" class="metric-value">0%</span></div>
            <div style="height: 80px;"><canvas id="gpuChart"></canvas></div>
            <div class="metric" style="margin-top:10px;"><span class="metric-label">System Pulse</span><span id="cpu-val" class="metric-value">0%</span></div>
        </div>
    </div>

    <div id="main-view">
        <div id="osd">
            <div class="osd-panel">
                <div class="panel-label">Current Cognitive Focus</div>
                <div id="active-goal" style="color: var(--success); font-weight: bold;">Idle...</div>
            </div>
            <div class="osd-panel">
                <div class="panel-label">Active Thought</div>
                <div id="last-thought" style="color: #888; font-style: italic;">Standby.</div>
            </div>
        </div>
        <div id="viewport"></div>
    </div>

    <div class="sidebar sidebar-right">
        <div class="panel" style="flex-grow: 1; display: flex; flex-direction: column;">
            <div class="panel-label">Major Breakthroughs & Learning</div>
            <div id="breakthroughs">
                <div style="color: #333; text-align: center; margin-top: 50px;">Waiting for evolution trace...</div>
            </div>
        </div>
        
        <div class="panel" style="height: 40%; display: flex; flex-direction: column;">
            <div class="panel-label">Thought Dialogue</div>
            <div id="thought-console"></div>
            <div id="log-content"></div>
        </div>
    </div>

    <script>
        let scene, camera, renderer, points;
        let totalSuccesses = 0;
        let totalLearned = 0;

        const homeoChart = new Chart(document.getElementById('homeoChart'), {
            type: 'line', data: { labels: [], datasets: [{ label: 'Stress', data: [], borderColor: '#ff0055', tension: 0.4, fill: false }, { label: 'Success', data: [], borderColor: '#00ffaa', tension: 0.4, fill: false }] },
            options: { responsive: true, maintainAspectRatio: false, scales: { x: { display: false }, y: { min: 0, max: 1 } }, plugins: { legend: { display: false } }, elements: { point: { radius: 0 } } }
        });

        const gpuChart = new Chart(document.getElementById('gpuChart'), {
            type: 'line', data: { labels: [], datasets: [{ label: 'GPU', data: [], borderColor: '#aa00ff', tension: 0.4, fill: true, backgroundColor: 'rgba(170, 0, 255, 0.1)' }] },
            options: { responsive: true, maintainAspectRatio: false, scales: { x: { display: false }, y: { min: 0, max: 1 } }, plugins: { legend: { display: false } }, elements: { point: { radius: 0 } } }
        });

        function init3D() {
            scene = new THREE.Scene();
            camera = new THREE.PerspectiveCamera(45, (window.innerWidth-720)/window.innerHeight, 0.1, 1000);
            camera.position.set(15, 12, 20); camera.lookAt(0, 0, 0);
            renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
            renderer.setSize(window.innerWidth - 720, window.innerHeight);
            document.getElementById('viewport').appendChild(renderer.domElement);
            const geometry = new THREE.BufferGeometry(); const verts = [];
            for (let i = 0; i < 5000; i++) {
                const x = (Math.random() - 0.5) * 18; const y = (Math.random() - 0.5) * 14; const z = (Math.random() - 0.5) * 14;
                if ((x*x)/81 + (y*y)/49 + (z*z)/49 < 1) verts.push(x, y, z);
            }
            geometry.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
            points = new THREE.Points(geometry, new THREE.PointsMaterial({ color: 0x1a1a1a, size: 0.05 }));
            scene.add(points);
            animate();
        }

        function animate() { requestAnimationFrame(animate); points.rotation.y += 0.0003; renderer.render(scene, camera); }

        function addBreakthrough(tag, text, type = 'success') {
            const container = document.getElementById('breakthroughs');
            if (container.innerText.includes("Waiting")) container.innerHTML = "";
            const entry = document.createElement('div');
            entry.className = 'bt-entry';
            const color = type === 'success' ? '#00ffaa' : '#aa00ff';
            const now = new Date().toLocaleTimeString();
            entry.innerHTML = `<span class="bt-time">${now}</span>
                               <span class="bt-tag" style="background:${color}22; color:${color}">${tag}</span>
                               <div class="bt-text">${text}</div>`;
            container.prepend(entry);
            if (container.childNodes.length > 15) container.removeChild(container.lastChild);
        }

        async function update() {
            try {
                const res = await fetch('/events');
                const events = await res.json();
                if (events.length === 0) return;

                events.reverse().forEach(event => {
                    if (event.intent === "homeostatic_pulse") {
                        const success = event.success_rate < 0 ? 0 : event.success_rate;
                        const stress = 1.0 - success;
                        document.getElementById('stress-val').innerText = stress.toFixed(2);
                        document.getElementById('success-val').innerText = success.toFixed(2);
                        document.getElementById('gpu-val').innerText = (event.gpu_load * 100).toFixed(0) + "%";
                        document.getElementById('cpu-val').innerText = (event.cpu_load * 100).toFixed(0) + "%";
                        
                        document.getElementById('stress-card').className = 'homeo-metric ' + (stress > 0.5 ? 'danger' : '');
                        document.getElementById('success-card').className = 'homeo-metric ' + (success > 0.8 ? 'success' : '');

                        homeoChart.data.labels.push("");
                        homeoChart.data.datasets[0].data.push(stress);
                        homeoChart.data.datasets[1].data.push(success);
                        if (homeoChart.data.labels.length > 30) { homeoChart.data.labels.shift(); homeoChart.data.datasets[0].data.shift(); homeoChart.data.datasets[1].data.shift(); }
                        homeoChart.update('none');

                        gpuChart.data.labels.push("");
                        gpuChart.data.datasets[0].data.push(event.gpu_load);
                        if (gpuChart.data.labels.length > 30) { gpuChart.data.labels.shift(); gpuChart.data.datasets[0].data.shift(); }
                        gpuChart.update('none');
                        return;
                    }

                    if (event.intent === "execution_result") {
                        if (event.status === "success") {
                            totalSuccesses++;
                            addBreakthrough("GOAL ACHIEVED", event.proprioception || "Task completed successfully.", "success");
                        }
                    }

                    if (event.intent === "sleep_cycle_complete") {
                        totalLearned += event.learned_memories;
                        document.getElementById('evo-count').innerText = totalLearned;
                        addBreakthrough("EVOLUTION COMPLETE", `Integrated ${event.learned_memories} new successful memories.`, "evo");
                    }

                    if (event.origin === "frontal_executive") {
                        if (event.intent === "inference_request") {
                             const goalMatch = event.text.match(/GOAL: (.*?)\n/);
                             if (goalMatch) document.getElementById('active-goal').innerText = goalMatch[1];
                        }
                    }
                    
                    if (event.origin === "synaptic_controller" && event.intent === "inference_result") {
                        try {
                            const thoughtMatch = event.text.match(/"thought": "(.*?)"/);
                            if (thoughtMatch) document.getElementById('last-thought').innerText = thoughtMatch[1];
                            
                            if (event.adapter === "critic") {
                                const console = document.getElementById('thought-console');
                                const div = document.createElement('div');
                                div.style.color = event.text.includes("APPROVED") ? "#00ffaa" : "#888";
                                div.innerHTML = `<span style="color:#444">></span> ${event.text.substring(0, 200)}...`;
                                console.prepend(div);
                            }
                        } catch(e) {}
                    }

                    const log = document.getElementById('log-content');
                    const line = document.createElement('div');
                    line.className = 'log-entry';
                    line.innerHTML = `<span class="log-origin">${event.origin}</span> ${event.intent}`;
                    log.prepend(line);
                    if (log.childNodes.length > 20) log.removeChild(log.lastChild);
                });
            } catch(e) {}
        }

        init3D(); setInterval(update, 1000);
        window.onresize = () => { renderer.setSize(window.innerWidth-720, window.innerHeight); camera.aspect = (window.innerWidth-720)/window.innerHeight; camera.updateProjectionMatrix(); };
    </script>
</body>
</html>
)V0G0";
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[i+1];
    }
    neuroswarm::VisualizerLobe visualizer(ip);
    visualizer.start();
    return 0;
}
