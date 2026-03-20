#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>

// NeuroSwarm Thalamus — Specialised Routing via XPUB/XSUB proxy.
//
// Biological analogue: the thalamus routes sensory signals to specific
// cortical areas rather than broadcasting everything everywhere.
//
// ZMQ XPUB/XSUB enables subscription forwarding: when a lobe subscribes
// to "critic_validate", that subscription propagates through the proxy
// to the XSUB side. Publishers only send messages that match at least
// one downstream subscriber's filter — zero-copy at the transport layer.
//
// Port 5555 (XSUB): lobes publish messages here (connect PUB → 5555)
// Port 5556 (XPUB): lobes subscribe here (connect SUB → 5556)
//
// Bridge mode (--bridge <remote_ip> --bridge-topics <topic1,topic2,...>):
//   Forwards selected topics to/from a remote Thalamus instance via
//   dedicated PUB/SUB sockets on ports 5557/5558.
//   When no --bridge flag is set, falls back to the zero-overhead zmq::proxy().

using json = nlohmann::json;

struct BridgeConfig {
    bool enabled = false;
    std::string remote_ip;
    int pub_port = 5557;   // outbound: we bind PUB here
    int sub_port = 5557;   // inbound: we connect SUB to remote's pub_port
    std::vector<std::string> topics;
    std::string instance_id;
    int max_hops = 2;
};

// Parse CLI: --bridge <ip> --bridge-topics <t1,t2,...> --instance-id <id>
BridgeConfig parse_bridge_args(int argc, char** argv) {
    BridgeConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bridge" && i + 1 < argc) {
            cfg.enabled = true;
            cfg.remote_ip = argv[++i];
        } else if (arg == "--bridge-topics" && i + 1 < argc) {
            std::string topics_str = argv[++i];
            std::istringstream ss(topics_str);
            std::string topic;
            while (std::getline(ss, topic, ',')) {
                if (!topic.empty()) cfg.topics.push_back(topic);
            }
        } else if (arg == "--instance-id" && i + 1 < argc) {
            cfg.instance_id = argv[++i];
        } else if (arg == "--bridge-pub-port" && i + 1 < argc) {
            cfg.pub_port = std::stoi(argv[++i]);
        }
    }

    // Default instance ID from hostname
    if (cfg.instance_id.empty()) {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            cfg.instance_id = hostname;
        } else {
            cfg.instance_id = "unknown";
        }
    }

    // Default topics if bridge enabled but no topics specified
    if (cfg.enabled && cfg.topics.empty()) {
        cfg.topics = {
            "execution_result", "self_model_updated", "dopamine_signal",
            "lobe_crash", "lobe_death", "specialist_report",
            "primordial_ready", "operators_synced"
        };
    }

    return cfg;
}

// Check if a message topic matches any bridge topic
bool topic_matches(const std::string& msg_topic, const std::vector<std::string>& topics) {
    for (const auto& t : topics) {
        if (msg_topic.find(t) != std::string::npos) return true;
    }
    return false;
}

// Bind with retry
void bind_with_retry(zmq::socket_t& sock, const std::string& addr, const std::string& label) {
    while (true) {
        try {
            sock.bind(addr);
            break;
        } catch (const zmq::error_t& e) {
            std::cerr << "[THALAMUS] " << label << " busy, retrying in 2s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}

// Read all frames of a multipart message
std::vector<zmq::message_t> recv_multipart(zmq::socket_t& sock, zmq::recv_flags flags = zmq::recv_flags::none) {
    std::vector<zmq::message_t> frames;
    while (true) {
        zmq::message_t frame;
        auto res = sock.recv(frame, flags);
        if (!res.has_value()) break;  // would_block for dontwait, or error
        frames.push_back(std::move(frame));
        if (!sock.get(zmq::sockopt::rcvmore)) break;
    }
    return frames;
}

// Send all frames of a multipart message
void send_multipart(zmq::socket_t& sock, std::vector<zmq::message_t>& frames) {
    for (size_t i = 0; i < frames.size(); ++i) {
        auto flag = (i < frames.size() - 1) ? zmq::send_flags::sndmore : zmq::send_flags::none;
        sock.send(zmq::message_t(frames[i].data(), frames[i].size()), flag);
    }
}

int main(int argc, char** argv) {
    auto cfg = parse_bridge_args(argc, argv);

    zmq::context_t ctx(1);

    // Frontend: receives published messages from all lobes
    zmq::socket_t frontend(ctx, zmq::socket_type::xsub);
    frontend.set(zmq::sockopt::linger, 0);
    bind_with_retry(frontend, "tcp://0.0.0.0:5555", "Port 5555");

    // Backend: distributes messages to subscribing lobes
    zmq::socket_t backend(ctx, zmq::socket_type::xpub);
    backend.set(zmq::sockopt::linger, 0);
    bind_with_retry(backend, "tcp://0.0.0.0:5556", "Port 5556");

    // No bridge — use zero-overhead zmq::proxy()
    if (!cfg.enabled) {
        std::cout << "[THALAMUS] Specialised Routing active (XPUB/XSUB)." << std::endl;
        zmq::proxy(frontend, backend);
        return 0;
    }

    // Bridge mode — manual poll loop with bridge sockets
    std::cout << "[THALAMUS] Bridge mode: " << cfg.instance_id
              << " ↔ " << cfg.remote_ip << std::endl;
    std::cout << "[THALAMUS] Bridged topics:";
    for (const auto& t : cfg.topics) std::cout << " " << t;
    std::cout << std::endl;

    // Outbound bridge: PUB socket bound locally
    zmq::socket_t bridge_pub(ctx, zmq::socket_type::pub);
    bridge_pub.set(zmq::sockopt::linger, 0);
    bind_with_retry(bridge_pub, "tcp://0.0.0.0:" + std::to_string(cfg.pub_port), "Bridge PUB");

    // Inbound bridge: SUB socket connecting to remote's PUB
    zmq::socket_t bridge_sub(ctx, zmq::socket_type::sub);
    bridge_sub.set(zmq::sockopt::linger, 0);
    bridge_sub.connect("tcp://" + cfg.remote_ip + ":" + std::to_string(cfg.sub_port));
    // Subscribe to all bridged topics
    for (const auto& topic : cfg.topics) {
        bridge_sub.set(zmq::sockopt::subscribe, topic);
    }

    std::cout << "[THALAMUS] Specialised Routing active (XPUB/XSUB + Bridge)." << std::endl;

    // Manual poll loop replacing zmq::proxy()
    zmq::pollitem_t items[] = {
        { static_cast<void*>(frontend),   0, ZMQ_POLLIN, 0 },
        { static_cast<void*>(backend),    0, ZMQ_POLLIN, 0 },
        { static_cast<void*>(bridge_sub), 0, ZMQ_POLLIN, 0 }
    };

    while (true) {
        zmq::poll(items, 3, std::chrono::milliseconds(-1));

        // Frontend → Backend: data messages from local publishers
        if (items[0].revents & ZMQ_POLLIN) {
            auto frames = recv_multipart(frontend);
            if (!frames.empty()) {
                // Check if this message should be bridged
                std::string topic(static_cast<char*>(frames[0].data()), frames[0].size());
                if (topic_matches(topic, cfg.topics)) {
                    // Parse JSON to inject bridge metadata
                    // The routing library sends topic as first frame, JSON as second
                    if (frames.size() >= 2) {
                        try {
                            std::string body(static_cast<char*>(frames[1].data()), frames[1].size());
                            auto j = json::parse(body);
                            int hops = j.value("_bridge_hops", 0);
                            if (hops < cfg.max_hops) {
                                j["_bridge_hops"] = hops + 1;
                                j["_origin_instance"] = cfg.instance_id;
                                std::string tagged = j.dump();
                                // Forward to bridge
                                zmq::message_t topic_frame(frames[0].data(), frames[0].size());
                                zmq::message_t body_frame(tagged.data(), tagged.size());
                                bridge_pub.send(std::move(topic_frame), zmq::send_flags::sndmore);
                                bridge_pub.send(std::move(body_frame), zmq::send_flags::none);
                            }
                        } catch (...) {
                            // Not JSON or parse failed — still forward locally, skip bridge
                        }
                    }
                }
                // Always forward to local subscribers
                send_multipart(backend, frames);
            }
        }

        // Backend → Frontend: subscription messages from local subscribers
        if (items[1].revents & ZMQ_POLLIN) {
            auto frames = recv_multipart(backend);
            if (!frames.empty()) {
                send_multipart(frontend, frames);
            }
        }

        // Bridge SUB → Backend: messages from remote instance
        if (items[2].revents & ZMQ_POLLIN) {
            auto frames = recv_multipart(bridge_sub);
            if (frames.size() >= 2) {
                try {
                    std::string body(static_cast<char*>(frames[1].data()), frames[1].size());
                    auto j = json::parse(body);

                    // Echo prevention: drop if originated from us
                    std::string origin_instance = j.value("_origin_instance", "");
                    if (origin_instance == cfg.instance_id) continue;

                    // Hop count check
                    int hops = j.value("_bridge_hops", 0);
                    if (hops >= cfg.max_hops) continue;

                    // Tag for traceability
                    j["_bridged_from"] = origin_instance;

                    std::string tagged = j.dump();
                    zmq::message_t topic_frame(frames[0].data(), frames[0].size());
                    zmq::message_t body_frame(tagged.data(), tagged.size());

                    // Inject into local backend so local subscribers receive it
                    backend.send(std::move(topic_frame), zmq::send_flags::sndmore);
                    backend.send(std::move(body_frame), zmq::send_flags::none);
                } catch (...) {
                    // Not valid JSON — forward raw
                    send_multipart(backend, frames);
                }
            }
        }
    }

    return 0;
}
