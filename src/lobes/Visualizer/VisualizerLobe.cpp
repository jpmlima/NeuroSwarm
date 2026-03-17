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
    const size_t max_events = 50;

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
    <title>NeuroSwarm // Cortex Monitor</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
    <style>
        :root {
            --bg: #0a0a0a;
            --panel: #111111;
            --accent: #00e5ff;
            --text-main: #e0e0e0;
            --text-dim: #666666;
            --border: #222222;
        }
        body { 
            margin: 0; background: var(--bg); color: var(--text-main); 
            font-family: -apple-system, BlinkMacSystemFont, "Inter", "Segoe UI", Roboto, sans-serif;
            font-weight: 300; letter-spacing: -0.02em;
            display: grid; grid-template-columns: 350px 1fr; height: 100vh;
        }
        #sidebar {
            background: var(--panel); border-right: 1px solid var(--border);
            padding: 40px; display: flex; flex-direction: column; gap: 30px;
            z-index: 10;
        }
        header h1 { font-size: 14px; font-weight: 600; text-transform: uppercase; margin: 0; color: var(--accent); }
        header p { font-size: 12px; color: var(--text-dim); margin: 5px 0 0 0; }
        
        .metric-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
        .metric-box { border-top: 1px solid var(--border); padding-top: 10px; }
        .metric-label { font-size: 10px; text-transform: uppercase; color: var(--text-dim); }
        .metric-value { font-size: 24px; font-weight: 200; font-variant-numeric: tabular-nums; }

        #event-stream {
            flex-grow: 1; overflow-y: hidden; font-family: "SF Mono", "Menlo", monospace;
            font-size: 11px; color: var(--text-dim); line-height: 1.6;
        }
        .event-line { border-bottom: 1px solid #181818; padding: 8px 0; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .event-line b { color: var(--text-main); font-weight: 500; }

        #viewport { position: relative; width: 100%; height: 100%; overflow: hidden; }
        canvas { outline: none; }
        
        #lobe-indicator {
            position: absolute; top: 40px; right: 40px; text-align: right;
            font-size: 12px; text-transform: uppercase; letter-spacing: 0.1em;
        }
    </style>
</head>
<body>
    <div id="sidebar">
        <header>
            <h1>NeuroSwarm CNS</h1>
            <p>Biomimetic Orchestration Framework</p>
        </header>

        <div class="metric-grid">
            <div class="metric-box">
                <div class="metric-label">System Stress</div>
                <div id="stress-val" class="metric-value">0.00</div>
            </div>
            <div class="metric-box">
                <div class="metric-label">Efficiency</div>
                <div id="success-val" class="metric-value">1.00</div>
            </div>
        </div>

        <div id="event-stream">
            <div class="metric-label" style="margin-bottom: 10px;">Synaptic Stream</div>
            <div id="log-content"></div>
        </div>
    </div>

    <div id="viewport">
        <div id="lobe-indicator">State: <span id="active-name" style="color: var(--accent)">Optimal</span></div>
    </div>

    <script>
        let scene, camera, renderer, points;
        const lobeMarkers = new Map();
        const coords = {
            'thalamus': {x: 0, y: 0, z: 0},
            'frontal_executive': {x: 0, y: 4, z: 6},
            'motor_cortex': {x: 0, y: 7, z: 0},
            'hippocampus': {x: 0, y: -2, z: -4},
            'synaptic_controller': {x: -4, y: 1, z: 0},
            'wernicke_lobe': {x: 4, y: 1, z: 4},
            'broca_lobe': {x: 4, y: 1, z: 6}
        };

        function init() {
            scene = new THREE.Scene();
            camera = new THREE.PerspectiveCamera(45, (window.innerWidth-350)/window.innerHeight, 0.1, 1000);
            camera.position.set(15, 10, 20);
            camera.lookAt(0, 0, 0);

            renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
            renderer.setSize(window.innerWidth - 350, window.innerHeight);
            renderer.setPixelRatio(window.devicePixelRatio);
            document.getElementById('viewport').appendChild(renderer.domElement);

            const geometry = new THREE.BufferGeometry();
            const verts = [];
            for (let i = 0; i < 3000; i++) {
                const x = (Math.random() - 0.5) * 18;
                const y = (Math.random() - 0.5) * 14;
                const z = (Math.random() - 0.5) * 14;
                if ((x*x)/81 + (y*y)/49 + (z*z)/49 < 1) verts.push(x, y, z);
            }
            geometry.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
            points = new THREE.Points(geometry, new THREE.PointsMaterial({ 
                color: 0x333333, size: 0.05, transparent: true, opacity: 0.5 
            }));
            scene.add(points);

            Object.keys(coords).forEach(name => {
                const c = coords[name];
                const nodeGeo = new THREE.IcosahedronGeometry(0.2, 1);
                const nodeMat = new THREE.MeshBasicMaterial({ color: 0x444444, wireframe: true });
                const mesh = new THREE.Mesh(nodeGeo, nodeMat);
                mesh.position.set(c.x, c.y, c.z);
                scene.add(mesh);
                lobeMarkers.set(name, mesh);
            });

            animate();
        }

        function animate() {
            requestAnimationFrame(animate);
            points.rotation.y += 0.001;
            renderer.render(scene, camera);
        }

        async function update() {
            try {
                const res = await fetch('/events');
                const events = await res.json();
                if (events.length === 0) return;

                const latest = events[0];
                const log = document.getElementById('log-content');
                
                const line = document.createElement('div');
                line.className = 'event-line';
                line.innerHTML = `<b>${latest.origin}</b> &rarr; ${latest.intent}`;
                log.prepend(line);
                if (log.childNodes.length > 12) log.removeChild(log.lastChild);

                if (lobeMarkers.has(latest.origin)) {
                    const m = lobeMarkers.get(latest.origin);
                    m.scale.set(4, 4, 4);
                    m.material.color.set(0x00e5ff);
                    setTimeout(() => {
                        m.scale.set(1, 1, 1);
                        m.material.color.set(0x444444);
                    }, 200);
                }

                if (latest.intent === "homeostatic_pulse") {
                    document.getElementById('success-val').innerText = latest.success_rate.toFixed(2);
                    document.getElementById('stress-val').innerText = (1.0 - latest.success_rate).toFixed(2);
                }
            } catch(e) {}
        }

        init();
        setInterval(update, 800);
        window.onresize = () => {
            camera.aspect = (window.innerWidth-350) / window.innerHeight;
            camera.updateProjectionMatrix();
            renderer.setSize(window.innerWidth-350, window.innerHeight);
        };
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
