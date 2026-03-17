#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <filesystem>
#include <map>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class MetaCognition {
public:
    MetaCognition(const std::string& pub_addr = "tcp://localhost:5555",
                  const std::string& sub_addr = "tcp://localhost:5556")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {
        
        pub.connect(pub_addr);
        sub.connect(sub_addr);
        sub.set(zmq::sockopt::subscribe, "");
        
        std::cout << "[META-COGNITION] Self-observation layer active." << std::endl;
        init_diary();
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    auto j = json::parse(raw);
                    process_event(j);
                } catch (...) {}
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    
    float last_stress = 0.0f;
    float last_success_rate = 1.0f;
    int total_sleep_cycles = 0;
    int total_learned_memories = 0;
    std::time_t last_reflection_time = 0;

    void init_diary() {
        if (!fs::exists("logs/THOUGHTS.md")) {
            std::ofstream f("logs/THOUGHTS.md");
            f << "# NeuroSwarm: Meta-Cognitive Thought Stream\n\n";
            f << "*Chronicle of a digital organism's evolution.*\n\n---\n";
        }
    }

    void process_event(const json& event) {
        std::string intent = event.value("intent", "");
        
        if (intent == "homeostatic_pulse") {
            last_success_rate = event.value("success_rate", 1.0f);
            // Derive stress (conceptual)
            last_stress = 1.0f - last_success_rate;
            if (last_stress < 0) last_stress = 0;
        } 
        else if (intent == "sleep_cycle_complete") {
            total_sleep_cycles++;
            total_learned_memories += event.value("learned_memories", 0);
            record_reflection("Sleep Cycle Complete. Integrated " + 
                              std::to_string(event.value("learned_memories", 0)) + 
                              " new success memories into executive policy.");
        }

        // Periodic reflection every 5 minutes if there's significant state
        std::time_t now = std::time(nullptr);
        if (now - last_reflection_time > 300) {
            periodic_reflection();
            last_reflection_time = now;
        }
    }

    void periodic_reflection() {
        std::string mood = (last_stress > 0.5f) ? "Stressed/Unstable" : "Optimal/Efficient";
        std::string thought = "System state is " + mood + ". Success Rate at " + 
                             std::to_string((int)(last_success_rate * 100)) + "%. " +
                             "Total evolution cycles: " + std::to_string(total_sleep_cycles) + ".";
        record_reflection(thought);
    }

    void record_reflection(const std::string& thought) {
        std::ofstream f("logs/THOUGHTS.md", std::ios::app);
        std::time_t now = std::time(nullptr);
        char timestamp[64];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

        f << "### [" << timestamp << "] Reflection\n";
        f << "> " << thought << "\n\n";
        std::cout << "[META-COGNITION] " << thought << std::endl;
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::MetaCognition mc;
    mc.start();
    return 0;
}
