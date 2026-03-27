#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
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
        routing::subscribe_all(sub);
        
        std::cout << "[HOMEOSTASIS] Autonomic nervous system online." << std::endl;
    }

    void start_monitoring() {
        // Also subscribe to sleep-related intents for stamina regeneration tracking
        routing::subscribe(sub, {"initiate_sleep_cycle", "sleep_cycle_complete",
                                  "inference_result", "execution_request"});

        while (true) {
            // Phase 5: Process bus events for metabolic accounting
            process_metabolic_events();

            auto state = collect_telemetry();
            float success_rate = calculate_success_rate();

            // Phase 5: Stamina drain/regen based on activity
            if (is_sleeping) {
                stamina = std::min(100.0f, stamina + 1.0f);  // +1.0/s during REM
            } else if (state[0] < 0.05f) {
                stamina = std::min(100.0f, stamina + 0.2f);  // +0.2/s idle regen
                stamina = std::max(0.0f, stamina - 0.1f);    // -0.1/s idle drain (net +0.1)
            } else {
                stamina = std::max(0.0f, stamina - 0.3f);    // -0.3/s active drain
            }

            json pulse = {
                {"origin", "homeostasis"},
                {"intent", "homeostatic_pulse"},
                {"cpu_load", state[0]},
                {"ram_used_gb", state[1]},
                {"vram_used_mb", state[2]},
                {"gpu_load", state[3]},
                {"success_rate", success_rate},
                {"stamina", stamina},
                {"ts", std::time(nullptr)}
            };

            dispatch(pulse);

            // Emit a high-stress alert when success rate drops below 10%, subject to a 30 s cooldown
            static int alert_cooldown = 0;
            if (success_rate < 0.1f && success_rate >= 0.0f && alert_cooldown <= 0) {
                json alert = {
                    {"origin", "homeostasis"},
                    {"intent", "high_stress_alert"},
                    {"reason", "low_success_rate"},
                    {"value", success_rate}
                };
                dispatch(alert);
                alert_cooldown = 30;
            }
            if (alert_cooldown > 0) alert_cooldown--;

            // Phase 5: Metabolic alert when stamina critically low
            if (stamina < 20.0f && !metabolic_alert_sent) {
                json met_alert = {
                    {"origin", "homeostasis"},
                    {"intent", "metabolic_alert"},
                    {"stamina", stamina},
                    {"reason", "low_stamina"}
                };
                dispatch(met_alert);
                metabolic_alert_sent = true;
                std::cout << "[HOMEOSTASIS] Metabolic alert: stamina=" << (int)stamina << "%" << std::endl;
            }
            if (stamina >= 30.0f) metabolic_alert_sent = false;  // reset hysteresis

            // Trigger a sleep-cycle request after sustained CPU idleness (< 5% load for ~60 s)
            // Only request sleep if not already sleeping (prevents spam)
            if (state[0] < 0.05f) {
                idle_ticks++;
                if (idle_ticks > 60 && !is_sleeping) {
                    json sleep_req = {
                        {"origin", "homeostasis"},
                        {"intent", "initiate_sleep_cycle"},
                        {"reason", "system_idle"}
                    };
                    dispatch(sleep_req);
                    idle_ticks = 0;
                    is_sleeping = true;
                }
            } else {
                idle_ticks = 0;
                is_sleeping = false;  // CPU active — no longer idle/sleeping
            }

            // Circadian rhythm: trigger sleep every ~10 minutes regardless of load.
            // This ensures memory consolidation, genome evolution, and fine-tuning
            // happen even when the system is continuously active.
            circadian_ticks_++;
            if (circadian_ticks_ >= 600 && !is_sleeping) {  // 600s = 10 min
                json sleep_req = {
                    {"origin", "homeostasis"},
                    {"intent", "initiate_sleep_cycle"},
                    {"reason", "circadian_rhythm"}
                };
                dispatch(sleep_req);
                is_sleeping = true;
                circadian_ticks_ = 0;
                std::cout << "[HOMEOSTASIS] Circadian sleep cycle triggered (10-min interval)" << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    int idle_ticks = 0;
    int circadian_ticks_ = 0;  // time since last sleep cycle

    // Phase 5: Metabolic Cost Accounting
    float stamina = 100.0f;  // 0-100 energy scale
    bool is_sleeping = false;
    bool metabolic_alert_sent = false;

    // Phase 5: Process bus events for metabolic cost accounting
    void process_metabolic_events() {
        // Non-blocking drain of relevant events
        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) break;

            std::string intent = j.value("intent", "");
            if (intent == "sleep_cycle_complete") {
                is_sleeping = false;
                circadian_ticks_ = 0;  // reset circadian timer after successful sleep
            } else if (intent == "initiate_sleep_cycle") {
                is_sleeping = true;
            } else if (intent == "inference_result") {
                // Inference costs energy: -0.5 per inference
                stamina = std::max(0.0f, stamina - 0.5f);
            } else if (intent == "execution_request") {
                std::string mode = j.value("mode", "");
                if (mode == "neuro_surgery") {
                    // Compilation costs more energy: -2.0
                    stamina = std::max(0.0f, stamina - 2.0f);
                }
            }
        }
    }

    std::vector<float> collect_telemetry() {
        float cpu = 0.0f;
        float ram = 0.0f;
        float vram = 0.0f;
        float gpu = 0.0f;

        // CPU load: 1-minute exponential moving average normalised by logical core count
        double load[3];
        if (getloadavg(load, 3) != -1) cpu = (float)load[0] / std::thread::hardware_concurrency();

        // RAM utilisation: derived from /proc/meminfo (MemTotal - MemAvailable), converted to GB
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        long total = 0, free = 0;
        while (std::getline(meminfo, line)) {
            if (line.find("MemTotal:") == 0) total = std::stol(line.substr(10));
            if (line.find("MemAvailable:") == 0) free = std::stol(line.substr(13));
        }
        if (total > 0) ram = (float)(total - free) / 1024.0f / 1024.0f; // kB → GB

        // GPU utilisation and VRAM: queried via nvidia-smi; gracefully degrades on non-NVIDIA hardware
        FILE* pipe = popen("nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buffer[128];
            if (fgets(buffer, 128, pipe)) {
                sscanf(buffer, "%f, %f", &gpu, &vram);
                gpu /= 100.0f; // Normalise utilisation percentage to [0.0, 1.0]
            }
            pclose(pipe);
        } else {
            // Fallback for non-NVIDIA or Vulkan backends: estimate GPU load from CPU proxy
            gpu = (cpu > 0.5f) ? 0.8f : 0.1f;
            vram = 1120.0f; // Estimated VRAM consumption for the loaded quantised model (MB)
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
            if (lines.size() > (size_t)window * 10) lines.erase(lines.begin()); // Bound the working set to 10× the evaluation window
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
        routing::publish(pub, data);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::Homeostasis homeo;
    homeo.start_monitoring();
    return 0;
}
