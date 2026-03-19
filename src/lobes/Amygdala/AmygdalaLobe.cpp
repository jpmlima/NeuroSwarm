#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <string>
#include <iostream>
#include <thread>
#include <ctime>
#include "Homeostasis.hpp"

using json = nlohmann::json;

namespace neuroswarm {

class AmygdalaLobe {
public:
    AmygdalaLobe(const std::string& pub_addr = "tcp://localhost:5555", 
                 const std::string& sub_addr = "tcp://localhost:5556") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {
        
        pub.connect(pub_addr);
        sub.connect(sub_addr);
        routing::subscribe_all(sub); // Monitors all traffic for threat detection

        std::cout << "[AMYGDALA] Neural amygdala online (PUB/SUB)." << std::endl;
    }

    void start() {
        std::thread homeostasis_monitor(&AmygdalaLobe::hardware_loop, this);
        homeostasis_monitor.detach();

        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;
            try {
                evaluate_stimulus(j);
            } catch (...) {}
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    void evaluate_stimulus(const json& j) {
        std::string origin = j.value("origin", "unknown");
        std::string intent = j.value("intent", "");
        std::string cid = j.value("cid", "global");
        std::string text = j.value("text", "");
        std::string command = j.value("command", "");

        // 1. Fear Circuit: Risk detection in commands
        if (!command.empty()) {
            if (command.find("rm ") != std::string::npos || command.find("mkfs") != std::string::npos || 
                command.find("sudo") != std::string::npos || command.find("> /") != std::string::npos) {
                
                std::cout << "[AMYGDALA] FEAR RESPONSE: Risky command detected -> " << command << std::endl;
                json threat = {
                    {"cid", cid},
                    {"origin", "amygdala"},
                    {"intent", "threat_assessment"},
                    {"level", "CRITICAL"},
                    {"sentiment", "fear"},
                    {"text", "DANGER: Risky OS operation detected. Proceed with extreme caution."}
                };
                dispatch(threat);
            }
        }

        // 2. Emotional Tagging: Injecting bias for the Executive
        if (text.find("error") != std::string::npos || text.find("fail") != std::string::npos) {
            json stress = {
                {"cid", cid},
                {"origin", "amygdala"},
                {"intent", "emotional_tag"},
                {"sentiment", "stress"},
                {"level", "HIGH"}
            };
            dispatch(stress);
        } else if (intent == "user_input") {
            json curiosity = {
                {"cid", cid},
                {"origin", "amygdala"},
                {"intent", "emotional_tag"},
                {"sentiment", "curiosity"},
                {"level", "NORMAL"}
            };
            dispatch(curiosity);
        }
    }

    void hardware_loop() {
        int check_interval = 10;
        while (true) {
            HardwareState state = Homeostasis::pulse();
            
            // Adaptive stress monitoring
            if (state.vram_used_pct > 0.85f || state.gpu_temp_c > 75.0f) {
                check_interval = 2; // Increased pulse frequency during high load
            } else {
                check_interval = 10;
            }

            if (state.gpu_temp_c > 82.0f) {
                trigger_alert("THERMAL_PANIC", "GPU Overheat (" + std::to_string(state.gpu_temp_c) + "C). Throttling suggested.");
            }

            if (state.vram_used_pct > 0.92f) {
                trigger_alert("MEMORY_PANIC", "VRAM Exhaustion. Immediate consolidation required.");
                
                // Active Homeostasis: Requesting Hippocampal REM phase (consolidation)
                json rem_req = {
                    {"cid", "autonomic_" + std::to_string(std::time(nullptr))},
                    {"origin", "amygdala"},
                    {"intent", "consolidate_memories"},
                    {"target", "global_stream"}
                };
                dispatch(rem_req);
            }

            std::this_thread::sleep_for(std::chrono::seconds(check_interval));
        }
    }

    void trigger_alert(const std::string& type, const std::string& reason) {
        std::cout << "[AMYGDALA] ALERT [" << type << "]: " << reason << std::endl;
        
        json alert = {
            {"cid", "autonomic_signal_" + std::to_string(std::time(nullptr))},
            {"origin", "amygdala"},
            {"intent", "urgent_interrupt"},
            {"type", type},
            {"sentiment", "stress"},
            {"text", "SYSTEM_ALERT: " + reason}
        };
        dispatch(alert);
    }

    void dispatch(const json& data) {
        routing::publish(pub, data);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::AmygdalaLobe amygdala;
    amygdala.start();
    return 0;
}
