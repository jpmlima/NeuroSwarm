#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class Homeostasis {
public:
    Homeostasis(const std::string& pub_addr = "tcp://localhost:5555",
                const std::string& sub_addr = "tcp://localhost:5556")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {
        
        pub.connect(pub_addr);
        sub.connect(sub_addr);
        sub.set(zmq::sockopt::subscribe, "");
        
        std::cout << "[HOMEOSTASIS] Autonomic nervous system online." << std::endl;
    }

    void start_monitoring() {
        while (true) {
            auto state = collect_telemetry();
            float success_rate = calculate_success_rate();
            
            json pulse = {
                {"origin", "homeostasis"},
                {"intent", "homeostatic_pulse"},
                {"cpu_load", state[0]},
                {"ram_used_gb", state[1]},
                {"vram_used_mb", state[2]},
                {"gpu_load", state[3]},
                {"success_rate", success_rate},
                {"ts", std::time(nullptr)}
            };
            
            dispatch(pulse);

            // Logic: If success rate < 10% AND we have enough data, signal high stress occasionally
            static int alert_cooldown = 0;
            if (success_rate < 0.1f && success_rate >= 0.0f && alert_cooldown <= 0) {
                json alert = {
                    {"origin", "homeostasis"},
                    {"intent", "high_stress_alert"},
                    {"reason", "low_success_rate"},
                    {"value", success_rate}
                };
                dispatch(alert);
                alert_cooldown = 30; // Only alert every 30 seconds
            }
            if (alert_cooldown > 0) alert_cooldown--;

            // Logic: If CPU is idle (< 5%) for a while, suggest sleep
            if (state[0] < 0.05f) {
                idle_ticks++;
                if (idle_ticks > 60) { // ~1 minute of idle
                    json sleep_req = {
                        {"origin", "homeostasis"},
                        {"intent", "initiate_sleep_cycle"},
                        {"reason", "system_idle"}
                    };
                    dispatch(sleep_req);
                    idle_ticks = 0;
                }
            } else {
                idle_ticks = 0;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    int idle_ticks = 0;

    std::vector<float> collect_telemetry() {
        float cpu = 0.0f;
        float ram = 0.0f;
        float vram = 0.0f;
        float gpu = 0.0f;

        // CPU Load (1 min avg)
        double load[3];
        if (getloadavg(load, 3) != -1) cpu = (float)load[0] / std::thread::hardware_concurrency();

        // RAM
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        long total = 0, free = 0;
        while (std::getline(meminfo, line)) {
            if (line.find("MemTotal:") == 0) total = std::stol(line.substr(10));
            if (line.find("MemAvailable:") == 0) free = std::stol(line.substr(13));
        }
        if (total > 0) ram = (float)(total - free) / 1024.0f / 1024.0f; // GB

        // GPU/VRAM (Attempt via nvidia-smi)
        FILE* pipe = popen("nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buffer[128];
            if (fgets(buffer, 128, pipe)) {
                sscanf(buffer, "%f, %f", &gpu, &vram);
                gpu /= 100.0f; // Normalize to 0.0-1.0
            }
            pclose(pipe);
        } else {
            // Fallback for non-nvidia or Vulkan generic (Mocked based on Brain activity)
            gpu = (cpu > 0.5f) ? 0.8f : 0.1f; 
            vram = 1120.0f; // Approximate for the loaded model
        }

        return {cpu, ram, vram, gpu};
    }

    float calculate_success_rate() {
        std::string path = "./data/engrams/global_stream.jsonl";
        if (!fs::exists(path)) return -1.0f;

        std::ifstream file(path);
        std::string line;
        int successes = 0;
        int failures = 0;
        int window = 20;
        std::vector<std::string> lines;

        while (std::getline(file, line)) {
            lines.push_back(line);
            if (lines.size() > (size_t)window * 10) lines.erase(lines.begin()); // Keep search space reasonable
        }

        int count = 0;
        for (auto it = lines.rbegin(); it != lines.rend() && count < window; ++it) {
            try {
                auto j = json::parse(*it);
                if (j.value("intent", "") == "execution_result") {
                    if (j.value("status", "") == "success") successes++;
                    else failures++;
                    count++;
                }
            } catch (...) {}
        }

        if (successes + failures == 0) return -1.0f;
        return (float)successes / (successes + failures);
    }

    void dispatch(const json& data) {
        std::string s = data.dump();
        zmq::message_t m(s.size());
        memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::Homeostasis homeo;
    homeo.start_monitoring();
    return 0;
}
