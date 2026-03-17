/**
 * @file VisualLobe.cpp
 * @brief Sensory gateway for environmental awareness.
 * 
 * Implements the "Eyes" of the NeuroSwarm by monitoring workspace 
 * state and system changes, converting them into Visual Engrams.
 */

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <thread>
#include <filesystem>
#include <vector>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class VisualLobe {
public:
    VisualLobe(const std::string& thalamus_ip = "localhost", 
               const std::string& target_path = ".") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub), root_path(target_path) {
        
        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, "");
        
        std::cout << "[VISUAL LOBE] Sensory system online. Watching: " << fs::absolute(root_path) << std::endl;
    }

    void start() {
        std::cout << "[VISUAL LOBE] Initializing Cortical Visual Stream..." << std::endl;
        
        // Start ZMQ listener for dynamic visual requests
        std::thread listener(&VisualLobe::listen_zmq, this);
        listener.detach();

        while (true) {
            auto current_scene = scan_workspace();
            auto changes = find_saliency_changes(current_scene);
            
            if (!changes.empty()) {
                generate_visual_stimulus(changes);
                last_known_scene = current_scene;
            }

            // Retinal Refresh Rate (10s)
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    fs::path root_path;
    
    struct FileState {
        fs::path path;
        long long mtime;
        size_t size;
    };
    std::vector<FileState> last_known_scene;

    void listen_zmq() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    auto j = json::parse(raw);
                    if (j.value("intent", "") == "sensory_visual_input") {
                        std::string image_path = j.value("image_path", "");
                        std::cout << "[VISUAL LOBE] Processing visual stimulus from: " << image_path << std::endl;
                        
                        // Simulated LLava analysis
                        std::string description = "Simulated visual analysis: Image contains a diagram of a neural network.";
                        
                        json req = {
                            {"cid", j.value("cid", "global")},
                            {"origin", "visual_lobe"},
                            {"intent", "user_input"},
                            {"text", description}
                        };
                        dispatch(req);
                    }
                } catch (...) {}
            }
        }
    }

    std::vector<FileState> scan_workspace() {
        std::vector<FileState> scene;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(root_path)) {
                if (entry.path().string().find("/.") != std::string::npos || 
                    entry.path().string().find("/build") != std::string::npos || 
                    entry.path().string().find("/external") != std::string::npos) {
                    continue;
                }

                if (fs::is_regular_file(entry)) {
                    scene.push_back({
                        entry.path(),
                        fs::last_write_time(entry).time_since_epoch().count(),
                        fs::file_size(entry)
                    });
                }
            }
        } catch (...) {}
        return scene;
    }

    std::vector<std::string> find_saliency_changes(const std::vector<FileState>& current_scene) {
        std::vector<std::string> changes;
        
        for (const auto& current : current_scene) {
            bool found = false;
            for (const auto& last : last_known_scene) {
                if (current.path == last.path) {
                    found = true;
                    if (current.mtime != last.mtime) {
                        changes.push_back("Modified: " + current.path.filename().string());
                    }
                    break;
                }
            }
            if (!found) {
                changes.push_back("New Stimulus: " + current.path.filename().string());
            }
        }
        return changes;
    }

    void generate_visual_stimulus(const std::vector<std::string>& changes) {
        std::string description = "Visual cortical update. Saliency changes detected:\n";
        for (const auto& c : changes) description += "- " + c + "\n";

        std::cout << "[VISUAL LOBE] Distributing visual stimulus to Thalamus." << std::endl;

        json stimulus = {
            {"cid", "visual_" + std::to_string(std::time(nullptr))},
            {"origin", "visual_lobe"},
            {"intent", "visual_stimulus"},
            {"text", description},
            {"saliency_score", changes.size() > 5 ? "HIGH" : "NORMAL"}
        };

        dispatch(stimulus);
    }

    void dispatch(const json& data) {
        std::string payload = data.dump();
        zmq::message_t msg(payload.size());
        memcpy(msg.data(), payload.c_str(), payload.size());
        pub.send(msg, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[i+1];
    }
    neuroswarm::VisualLobe visual(ip);
    visual.start();
    return 0;
}
