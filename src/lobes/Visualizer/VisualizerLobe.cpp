#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <deque>

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
        // Start ZMQ listener in a separate thread
        std::thread zmq_thread(&VisualizerLobe::listen_zmq, this);
        zmq_thread.detach();

        // Start Web Server
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
                    
                    // Add timestamp for the UI
                    j["ui_ts"] = std::time(nullptr);
                    event_buffer.push_front(j);
                    if (event_buffer.size() > max_events) event_buffer.pop_back();
                } catch (...) {}
            }
        }
    }

    std::string get_dashboard_html() {
        return R"(
<!DOCTYPE html>
<html>
<head>
    <title>NeuroSwarm EEG - Live Neural Activity</title>
    <style>
        body { background: #0a0a0a; color: #00ffcc; font-family: 'Courier New', monospace; margin: 20px; }
        h1 { border-bottom: 2px solid #00ffcc; padding-bottom: 10px; }
        .node-container { display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 30px; }
        .node { padding: 10px; border: 1px solid #333; border-radius: 5px; background: #1a1a1a; min-width: 120px; text-align: center; transition: all 0.2s; }
        .active { background: #00ffcc; color: #000; box-shadow: 0 0 15px #00ffcc; }
        #log { height: 400px; overflow-y: auto; border: 1px solid #333; padding: 10px; background: #050505; font-size: 12px; }
        .event { border-bottom: 1px solid #222; padding: 5px 0; }
        .origin { color: #ff3300; font-weight: bold; }
        .intent { color: #cc33ff; }
    </style>
</head>
<body>
    <h1>NEUROSWARM // CEREBRAL EEG</h1>
    <div class="node-container" id="nodes"></div>
    <div id="log"></div>

    <script>
        const nodesDiv = document.getElementById('nodes');
        const logDiv = document.getElementById('log');
        const activeNodes = new Map();

        async function update() {
            try {
                const res = await fetch('/events');
                const events = await res.json();
                
                logDiv.innerHTML = '';
                const detectedOrigins = new Set();

                events.forEach(e => {
                    detectedOrigins.add(e.origin);
                    const div = document.createElement('div');
                    div.className = 'event';
                    div.innerHTML = `<span class="origin">[${e.origin}]</span> <span class="intent">${e.intent}</span>: ${JSON.stringify(e).substring(0, 150)}...`;
                    logDiv.appendChild(div);
                });

                // Update Node UI
                detectedOrigins.forEach(origin => {
                    if (!activeNodes.has(origin)) {
                        const n = document.createElement('div');
                        n.className = 'node';
                        n.id = 'node-' + origin;
                        n.innerText = origin.toUpperCase();
                        nodesDiv.appendChild(n);
                        activeNodes.set(origin, n);
                    }
                    const el = activeNodes.get(origin);
                    el.classList.add('active');
                    setTimeout(() => el.classList.remove('active'), 500);
                });

            } catch(e) {}
        }

        setInterval(update, 1000);
    </script>
</body>
</html>
        )";
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
