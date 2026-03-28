#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <map>
#include <ctime>
#include <sys/stat.h>

using json = nlohmann::json;

namespace neuroswarm {

// StatisticsLobe — passive bus observer for empirical evaluation.
//
// Subscribes to the neural bus and records structured metrics to
// data/metrics/ in JSONL format. Zero interference with cognition:
// this lobe never publishes messages that affect other lobes.
//
// Recorded metrics per event type:
//   - Ralph cycle: duration, task_id, retry count, outcome
//   - Critic decisions: approved/rejected, rejection reason
//   - Hippocampus recalls: similarity scores, match count
//   - Inference: adapter, prompt size (approx), response size
//   - Time pulses: used for session segmentation only
//
// Output files:
//   data/metrics/ralph_cycles.jsonl     — one entry per completed Ralph task
//   data/metrics/critic_decisions.jsonl
//   data/metrics/memory_recalls.jsonl
//   data/metrics/inference_events.jsonl
//   data/metrics/system_events.jsonl    — startup, stress alerts, sleep cycles
//   data/metrics/intrinsic_goals.jsonl  — intrinsic motivation decisions, results, dopamine signals

class StatisticsLobe {
public:
    StatisticsLobe(const std::string& thalamus_ip = "localhost")
        : ctx(1), sub(ctx, zmq::socket_type::sub),
          metrics_dir("./data/metrics/") {

        routing::set_buffer_limit(sub, 200);
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe_all(sub);

        // Ensure metrics directory exists
        mkdir("./data", 0755);
        mkdir(metrics_dir.c_str(), 0755);

        std::cout << "[STATISTICS] Passive observer online. Writing to " << metrics_dir << std::endl;
    }

    void start() {
        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;
            try {
                process_event(j);
            } catch (...) {}
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t sub;
    std::string metrics_dir;

    // Track Ralph cycle start times by CID
    std::map<std::string, std::chrono::steady_clock::time_point> ralph_start_times;
    // Track retry counts by CID
    std::map<std::string, int> ralph_retries;

    std::string now_iso() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return std::string(buf);
    }

    void process_event(const json& j) {
        std::string origin = j.value("origin", "");
        std::string intent = j.value("intent", "");
        std::string cid    = j.value("cid", "");

        // Ralph cycle tracking — detect task start
        if (origin == "frontal_executive" && intent == "search_memory" && cid.find("ralph_") == 0) {
            ralph_start_times[cid] = std::chrono::steady_clock::now();
            ralph_retries[cid] = 0;
        }

        // Ralph task completion
        if (origin == "frontal_executive" && intent == "task_complete" && cid.find("ralph_") == 0) {
            long duration_ms = 0;
            if (ralph_start_times.count(cid)) {
                auto elapsed = std::chrono::steady_clock::now() - ralph_start_times[cid];
                duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                ralph_start_times.erase(cid);
            }

            json entry = {
                {"timestamp", now_iso()},
                {"cid", cid},
                {"task_id", j.value("task_id", "")},
                {"duration_ms", duration_ms},
                {"retries", ralph_retries.count(cid) ? ralph_retries[cid] : 0},
                {"outcome", "success"}
            };
            ralph_retries.erase(cid);
            append_jsonl("ralph_cycles.jsonl", entry);
        }

        // Critic decisions
        if ((origin == "critic_lobe" || (origin == "synaptic_controller" && j.value("adapter", "") == "critic"))
            && intent == "critic_result") {
            std::string text = j.value("text", "");
            bool approved = text.find("APPROVED") != std::string::npos;

            json entry = {
                {"timestamp", now_iso()},
                {"cid", cid},
                {"approved", approved},
                {"reason", approved ? "" : text.substr(0, 200)}
            };
            append_jsonl("critic_decisions.jsonl", entry);
        }

        // Memory recall results
        if (origin == "hippocampus" && intent == "search_result") {
            auto matches = j.value("matches", json::array());
            std::vector<float> scores;
            for (auto& m : matches) {
                scores.push_back(m.value("similarity", 0.0f));
            }

            json entry = {
                {"timestamp", now_iso()},
                {"cid", cid},
                {"match_count", (int)matches.size()},
                {"similarity_scores", scores},
                {"top_score", scores.empty() ? 0.0f : scores[0]}
            };
            append_jsonl("memory_recalls.jsonl", entry);
        }

        // Inference events
        if (intent == "inference_result" && origin == "synaptic_controller") {
            std::string adapter = j.value("adapter", "default");
            std::string text = j.value("text", "");

            json entry = {
                {"timestamp", now_iso()},
                {"cid", cid},
                {"adapter", adapter},
                {"response_length", (int)text.size()}
            };
            append_jsonl("inference_events.jsonl", entry);
        }

        // Inference requests — track prompt sizes
        if (intent == "inference_request") {
            std::string adapter = j.value("adapter", "default");
            std::string text = j.value("text", "");

            json entry = {
                {"timestamp", now_iso()},
                {"cid", cid},
                {"adapter", adapter},
                {"prompt_length", (int)text.size()}
            };
            append_jsonl("inference_events.jsonl", entry);
        }

        // Execution results — track retry counts for Ralph
        if (origin == "motor_cortex" && intent == "execution_result") {
            std::string status = j.value("status", "");
            if (status != "success" && ralph_retries.count(cid)) {
                ralph_retries[cid]++;
            }
        }

        // System-level events
        if ((origin == "homeostasis" && intent == "high_stress_alert") ||
            (origin == "homeostasis" && intent == "initiate_sleep_cycle") ||
            (origin == "frontal_executive" && intent == "task_complete")) {

            json entry = {
                {"timestamp", now_iso()},
                {"origin", origin},
                {"intent", intent},
                {"cid", cid},
                {"detail", j.value("text", j.value("reason", ""))}
            };
            append_jsonl("system_events.jsonl", entry);
        }

        // Intrinsic motivation events from BasalGanglia
        if (origin == "basal_ganglia" && intent == "intrinsic_goal") {
            json entry = {
                {"timestamp", now_iso()},
                {"event", "intrinsic_goal"},
                {"cid", cid},
                {"domain", j.value("domain", "")},
                {"fitness", j.value("fitness", 0.0f)},
                {"context", j.value("context", "")}
            };
            append_jsonl("intrinsic_goals.jsonl", entry);
        }

        // Intrinsic goal results from FrontalExecutive
        if (origin == "frontal_executive" && intent == "intrinsic_goal_result") {
            json entry = {
                {"timestamp", now_iso()},
                {"event", "intrinsic_goal_result"},
                {"cid", cid},
                {"domain", j.value("domain", "")},
                {"success", j.value("success", false)},
                {"command", j.value("command", "")}
            };
            append_jsonl("intrinsic_goals.jsonl", entry);
        }

        // Spike worker events
        if (origin == "spike_worker" && (intent == "spike_ready" || intent == "spike_done")) {
            json entry = {
                {"timestamp", now_iso()},
                {"event", intent},
                {"worker_id", j.value("worker_id", "")},
                {"cid", cid},
                {"success", j.value("success", true)},
                {"task_id", j.value("task_id", "")},
                {"domain", j.value("domain", "")}
            };
            append_jsonl("spike_events.jsonl", entry);
        }

        // Dopamine signals from BasalGanglia
        if (origin == "basal_ganglia" && intent == "dopamine_signal") {
            json entry = {
                {"timestamp", now_iso()},
                {"event", "dopamine_signal"},
                {"domain", j.value("domain", "")},
                {"reason", j.value("reason", "")},
                {"magnitude", j.value("magnitude", 0.0f)}
            };
            append_jsonl("intrinsic_goals.jsonl", entry);
        }
    }

    void append_jsonl(const std::string& filename, const json& entry) {
        std::ofstream f(metrics_dir + filename, std::ios::app);
        if (f.is_open()) {
            f << entry.dump() << "\n";
        }
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[++i];
    }
    neuroswarm::StatisticsLobe stats(ip);
    stats.start();
    return 0;
}
