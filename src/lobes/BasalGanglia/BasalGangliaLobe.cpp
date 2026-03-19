#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <chrono>
#include <thread>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>

using json = nlohmann::json;

namespace neuroswarm {

// BasalGangliaLobe — intrinsic motivation engine.
//
// Biological analogue: basal ganglia (action selection via reward prediction)
// + cerebellum (prediction error computation) + VTA (dopamine/reward signal).
//
// Maintains a self-model: a registry of 14 capability domains with success/
// failure counts, predicted vs actual success rates, novelty scores, and
// example commands. Computes a fitness function F(d) for each domain to
// select the next intrinsic goal when no external tasks remain.
//
// Inspired by Karl Friston's Free Energy Principle — the system minimises
// prediction error through action, exploring domains where its self-model
// is least accurate or least explored.
//
// Listens to: execution_result, homeostatic_pulse, time_pulse, intrinsic_goal_request
// Publishes:  intrinsic_goal, dopamine_signal, self_model_updated

class BasalGangliaLobe {
public:
    BasalGangliaLobe(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub),
          self_model_path("./data/self_model.json") {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, "");

        mkdir("./data", 0755);
        load_self_model();
        init_domain_commands();

        std::cout << "[BASAL_GANGLIA] Intrinsic motivation engine online. "
                  << domains.size() << " capability domains tracked." << std::endl;
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (!sub.recv(msg, zmq::recv_flags::dontwait)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                if (raw.empty() || raw[0] != '{') continue;
                auto j = json::parse(raw);

                std::string origin = j.value("origin", "");
                std::string intent = j.value("intent", "");

                if (origin == "motor_cortex" && intent == "execution_result") {
                    handle_execution_result(j);
                }
                else if (intent == "intrinsic_goal_request") {
                    handle_goal_request(j);
                }
                else if (origin == "homeostasis" && intent == "homeostatic_pulse") {
                    system_stress *= 0.85f;
                }
                else if (origin == "homeostasis" && intent == "high_stress_alert") {
                    system_stress = 1.0f;
                }
                else if (origin == "chronos" && intent == "time_pulse") {
                    current_timestamp = j.value("timestamp", "");
                    // Check cooldown expiry
                    check_cooldowns();
                }
                else if (origin == "frontal_executive" && intent == "intrinsic_goal_result") {
                    handle_intrinsic_result(j);
                }
            } catch (...) {}
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    std::string self_model_path;
    float system_stress = 0.0f;
    std::string current_timestamp;

    // Domain state
    struct DomainState {
        int success = 0;
        int failure = 0;
        long last_attempt_ts = 0;
        float predicted_success_rate = 0.5f;
        float actual_success_rate = 0.0f;
        float prediction_error = 0.5f;
        std::vector<std::string> example_commands;
        float novelty_score = 1.0f;
        // Cooldown: consecutive failures and cooldown expiry
        int consecutive_failures = 0;
        long cooldown_until = 0;
    };

    std::vector<std::string> domains = {
        "file_read", "file_write", "file_search", "process_inspection",
        "network_diagnostics", "source_modification", "compilation",
        "git_operations", "system_monitoring", "data_analysis",
        "script_creation", "self_inspection", "memory_analysis", "log_analysis"
    };

    std::map<std::string, DomainState> self_model;

    // Suggested commands per domain — concrete templates for small models
    std::map<std::string, std::vector<std::string>> domain_commands;

    // Keyword → domain classification
    struct KeywordRule {
        std::string domain;
        std::vector<std::string> keywords;
    };
    std::vector<KeywordRule> classification_rules;

    void init_domain_commands() {
        domain_commands["file_read"] = {
            "cat src/brainstem/Thalamus.cpp | head -50",
            "wc -l src/brainstem/*.cpp",
            "file data/engrams/memory_index.jsonl",
            "stat src/brainstem/FrontalExecutive.cpp"
        };
        domain_commands["file_write"] = {
            "echo '# diagnostic report' > /tmp/neuro_diag.md",
            "date >> data/activity_log.txt",
            "cp tasks.json tasks.json.bak"
        };
        domain_commands["file_search"] = {
            "find src/ -name '*.cpp' -newer build/thalamus",
            "grep -r 'intent' src/brainstem/ --include='*.cpp' -l",
            "find data/ -name '*.jsonl' -size +0c"
        };
        domain_commands["process_inspection"] = {
            "ps aux | grep -E '(thalamus|motor_lobe|frontal)' | grep -v grep",
            "pgrep -la 'synaptic_controller'",
            "ls -la /proc/self/fd/ | wc -l"
        };
        domain_commands["network_diagnostics"] = {
            "ss -tlnp | grep -E '(5555|5556|8080)'",
            "nc -z localhost 5555 && echo 'ZMQ PUB alive' || echo 'ZMQ PUB down'",
            "curl -s -o /dev/null -w '%{http_code}' http://localhost:8080"
        };
        domain_commands["source_modification"] = {
            "grep -c 'TODO\\|FIXME\\|HACK' src/brainstem/*.cpp",
            "wc -l src/brainstem/*.cpp src/lobes/*/*.cpp | tail -1",
            "diff <(head -5 src/brainstem/Thalamus.cpp) <(head -5 src/brainstem/Homeostasis.cpp)"
        };
        domain_commands["compilation"] = {
            "cmake --build build --target thalamus 2>&1 | tail -5",
            "make -C build -n thalamus 2>&1 | head -3",
            "ls -lt build/thalamus build/motor_lobe build/frontal_executive | head -3"
        };
        domain_commands["git_operations"] = {
            "git -C . log --oneline -5",
            "git -C . status --short",
            "git -C . diff --stat HEAD~1"
        };
        domain_commands["system_monitoring"] = {
            "uptime",
            "free -h | head -2",
            "df -h / | tail -1"
        };
        domain_commands["data_analysis"] = {
            "wc -l data/metrics/*.jsonl 2>/dev/null || echo 'no metrics yet'",
            "tail -3 data/metrics/ralph_cycles.jsonl 2>/dev/null || echo 'no ralph data'",
            "cat data/self_model.json 2>/dev/null | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d),\"domains tracked\")' 2>/dev/null || echo 'no self-model'"
        };
        domain_commands["script_creation"] = {
            "echo '#!/bin/bash' > /tmp/ns_health.sh && echo 'ps aux | grep -c neuroswarm' >> /tmp/ns_health.sh && chmod +x /tmp/ns_health.sh && /tmp/ns_health.sh",
            "bash -c 'for f in build/{thalamus,motor_lobe,frontal_executive}; do [ -x \"$f\" ] && echo \"OK: $f\" || echo \"MISSING: $f\"; done'"
        };
        domain_commands["self_inspection"] = {
            "cat data/self_model.json 2>/dev/null | head -20 || echo 'self-model not yet populated'",
            "du -sh data/ build/ src/ 2>/dev/null",
            "find src/ -name '*.cpp' | wc -l"
        };
        domain_commands["memory_analysis"] = {
            "wc -l data/engrams/memory_index.jsonl 2>/dev/null || echo 'no engrams'",
            "tail -1 data/engrams/memory_index.jsonl 2>/dev/null || echo 'empty memory'",
            "ls -la data/system_knowledge.md 2>/dev/null || echo 'no behavioural knowledge'"
        };
        domain_commands["log_analysis"] = {
            "tail -5 progress.txt 2>/dev/null || echo 'no progress log'",
            "tail -3 data/metrics/system_events.jsonl 2>/dev/null || echo 'no system events'",
            "wc -l data/metrics/*.jsonl 2>/dev/null | sort -n | tail -5"
        };

        // Classification rules: keyword → domain
        classification_rules = {
            {"file_read",           {"cat", "head", "tail", "less", "more", "wc -l", "file ", "stat ", "md5sum", "sha256sum", "readlink"}},
            {"file_write",          {"echo", "tee", "cp ", "mv ", "touch", "mkdir", ">>", "> "}},
            {"file_search",         {"find ", "grep", "locate", "which", "whereis", "fd ", "rg "}},
            {"process_inspection",  {"ps ", "pgrep", "top", "htop", "pidof", "kill", "/proc/"}},
            {"network_diagnostics", {"ss ", "netstat", "nc ", "curl", "wget", "ping", "nmap", "ip addr", "ifconfig"}},
            {"source_modification", {"sed ", "awk ", "patch", "diff ", "src/"}},
            {"compilation",         {"cmake", "make", "gcc", "g++", "build"}},
            {"git_operations",      {"git "}},
            {"system_monitoring",   {"uptime", "free", "df ", "vmstat", "iostat", "sensors", "nvidia-smi", "lscpu", "uname"}},
            {"data_analysis",       {"python3", "awk", "sort", "uniq", "cut ", "jq ", "data/"}},
            {"script_creation",     {"#!/", "chmod +x", "bash -c", "sh -c"}},
            {"self_inspection",     {"self_model", "neuroswarm", "du -s"}},
            {"memory_analysis",     {"engram", "memory_index", "system_knowledge"}},
            {"log_analysis",        {"progress.txt", "metrics/", "journal", "syslog", "dmesg"}}
        };
    }

    std::string classify_command(const std::string& cmd) {
        std::string cmd_lower = cmd;
        std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);

        for (auto& rule : classification_rules) {
            for (auto& kw : rule.keywords) {
                if (cmd_lower.find(kw) != std::string::npos) {
                    return rule.domain;
                }
            }
        }
        return ""; // unclassified
    }

    // Fitness function: F(d) = 0.20*Coverage + 0.15*Trend + 0.30*PredictionError + 0.25*Novelty - 0.10*Stress
    float compute_fitness(const std::string& domain) {
        auto& d = self_model[domain];
        int total = d.success + d.failure;

        // Find max attempts across all domains
        int max_attempts = 1;
        for (auto& [name, state] : self_model) {
            int t = state.success + state.failure;
            if (t > max_attempts) max_attempts = t;
        }

        // Coverage: 1.0 - (attempts_d / max_attempts) — favours under-explored
        float coverage = 1.0f - (float)total / (float)max_attempts;

        // Trend: recent_success_rate - predicted — favours improving domains
        float trend = d.actual_success_rate - d.predicted_success_rate;
        // Clamp to [-1, 1]
        trend = std::max(-1.0f, std::min(1.0f, trend));

        // Prediction error: |predicted - actual| — core Free Energy term
        float pred_error = d.prediction_error;

        // Novelty: exp(-attempts / 10) — never-attempted = 1.0
        float novelty = std::exp(-(float)total / 10.0f);

        float fitness = 0.20f * coverage
                      + 0.15f * trend
                      + 0.30f * pred_error
                      + 0.25f * novelty
                      - 0.10f * system_stress;

        return fitness;
    }

    void handle_execution_result(const json& j) {
        std::string cmd = j.value("command", "");
        if (cmd.empty()) {
            // Try to extract from proprioception context
            cmd = j.value("original_command", "");
        }
        if (cmd.empty()) return;

        std::string domain = classify_command(cmd);
        if (domain.empty()) return;

        std::string status = j.value("status", "");
        bool success = (status == "success");

        auto& d = self_model[domain];
        int total_before = d.success + d.failure;

        if (success) {
            d.success++;
            d.consecutive_failures = 0;
        } else {
            d.failure++;
            d.consecutive_failures++;

            // Learned helplessness: 5 consecutive failures → 10-minute cooldown
            if (d.consecutive_failures >= 5) {
                long now_ts = std::time(nullptr);
                d.cooldown_until = now_ts + 600; // 10 minutes
                std::cout << "[BASAL_GANGLIA] Learned helplessness: domain '" << domain
                          << "' entering 10-minute cooldown after " << d.consecutive_failures
                          << " consecutive failures." << std::endl;
            }
        }

        d.last_attempt_ts = std::time(nullptr);

        // Update actual success rate
        int total = d.success + d.failure;
        d.actual_success_rate = (total > 0) ? (float)d.success / (float)total : 0.0f;

        // Compute prediction error
        d.prediction_error = std::fabs(d.predicted_success_rate - d.actual_success_rate);

        // Update predicted success rate (exponential moving average)
        float alpha = 0.2f;
        d.predicted_success_rate = (1.0f - alpha) * d.predicted_success_rate
                                 + alpha * d.actual_success_rate;

        // Update novelty
        d.novelty_score = std::exp(-(float)total / 10.0f);

        // Track example commands (keep last 5)
        if (d.example_commands.size() >= 5) {
            d.example_commands.erase(d.example_commands.begin());
        }
        if (cmd.size() <= 120) {
            d.example_commands.push_back(cmd);
        }

        // Check for dopamine signal — novel capability or high prediction error surprise
        bool is_novel = (total_before == 0 && total == 1);
        bool is_surprising = (d.prediction_error > 0.3f);

        if (is_novel || is_surprising) {
            emit_dopamine(domain, is_novel ? "novel_capability" : "prediction_surprise",
                         d.prediction_error);
        }

        save_self_model();
        emit_self_model_updated(domain);
    }

    void handle_intrinsic_result(const json& j) {
        std::string domain = j.value("domain", "");
        bool success = j.value("success", false);
        std::string cmd = j.value("command", "");

        if (domain.empty()) return;

        // This is handled by handle_execution_result via the bus,
        // but we use this for explicit domain tagging when FE reports back
        auto& d = self_model[domain];
        if (success) {
            d.consecutive_failures = 0;
        }
    }

    void handle_goal_request(const json& j) {
        std::string cid = j.value("cid", "");

        // Find the domain with highest fitness, respecting cooldowns
        long now_ts = std::time(nullptr);
        std::string best_domain;
        float best_fitness = -999.0f;

        for (auto& domain : domains) {
            auto& d = self_model[domain];

            // Skip domains in cooldown
            if (d.cooldown_until > now_ts) continue;

            float f = compute_fitness(domain);
            if (f > best_fitness) {
                best_fitness = f;
                best_domain = domain;
            }
        }

        if (best_domain.empty()) {
            std::cout << "[BASAL_GANGLIA] All domains in cooldown. No intrinsic goal available." << std::endl;
            return;
        }

        auto& d = self_model[best_domain];
        int total = d.success + d.failure;

        // Build context string
        std::string context;
        if (total == 0) {
            context = "Never attempted. ";
        } else {
            context = "Attempted " + std::to_string(total) + " times. "
                    + "Success rate: " + std::to_string((int)(d.actual_success_rate * 100)) + "%. "
                    + "Prediction error: " + std::to_string(d.prediction_error).substr(0, 5) + ". ";
        }
        context += "System stress: " + std::to_string((int)(system_stress * 100)) + "%.";

        // Get suggested commands for this domain
        auto& cmds = domain_commands[best_domain];

        json goal = {
            {"cid", cid},
            {"origin", "basal_ganglia"},
            {"intent", "intrinsic_goal"},
            {"domain", best_domain},
            {"fitness", best_fitness},
            {"suggested_commands", cmds},
            {"context", context}
        };

        std::string s = goal.dump();
        zmq::message_t m(s.size());
        memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);

        std::cout << "[BASAL_GANGLIA] Intrinsic goal published: domain='" << best_domain
                  << "' fitness=" << best_fitness << " (" << context << ")" << std::endl;
    }

    void emit_dopamine(const std::string& domain, const std::string& reason, float magnitude) {
        json signal = {
            {"origin", "basal_ganglia"},
            {"intent", "dopamine_signal"},
            {"domain", domain},
            {"reason", reason},
            {"magnitude", magnitude},
            {"timestamp", current_timestamp}
        };

        std::string s = signal.dump();
        zmq::message_t m(s.size());
        memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);

        std::cout << "[BASAL_GANGLIA] Dopamine signal: " << reason
                  << " in domain '" << domain << "' (magnitude=" << magnitude << ")" << std::endl;
    }

    void emit_self_model_updated(const std::string& domain) {
        json update = {
            {"origin", "basal_ganglia"},
            {"intent", "self_model_updated"},
            {"domain", domain}
        };

        std::string s = update.dump();
        zmq::message_t m(s.size());
        memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);
    }

    void check_cooldowns() {
        long now_ts = std::time(nullptr);
        for (auto& [domain, d] : self_model) {
            if (d.cooldown_until > 0 && d.cooldown_until <= now_ts) {
                d.cooldown_until = 0;
                d.consecutive_failures = 0;
                std::cout << "[BASAL_GANGLIA] Cooldown expired for domain '" << domain << "'." << std::endl;
            }
        }
    }

    void load_self_model() {
        // Initialise all domains with defaults
        for (auto& domain : domains) {
            self_model[domain] = DomainState{};
        }

        std::ifstream f(self_model_path);
        if (!f.is_open()) return;

        try {
            json doc;
            f >> doc;

            for (auto& [domain, data] : doc.items()) {
                if (self_model.find(domain) == self_model.end()) continue;
                auto& d = self_model[domain];
                d.success               = data.value("success", 0);
                d.failure               = data.value("failure", 0);
                d.last_attempt_ts       = data.value("last_attempt_ts", 0L);
                d.predicted_success_rate = data.value("predicted_success_rate", 0.5f);
                d.actual_success_rate   = data.value("actual_success_rate", 0.0f);
                d.prediction_error      = data.value("prediction_error", 0.5f);
                d.novelty_score         = data.value("novelty_score", 1.0f);
                d.consecutive_failures  = data.value("consecutive_failures", 0);
                d.cooldown_until        = data.value("cooldown_until", 0L);

                if (data.contains("example_commands") && data["example_commands"].is_array()) {
                    for (auto& cmd : data["example_commands"]) {
                        d.example_commands.push_back(cmd.get<std::string>());
                    }
                }
            }

            std::cout << "[BASAL_GANGLIA] Loaded self-model from " << self_model_path << std::endl;
        } catch (...) {
            std::cout << "[BASAL_GANGLIA] Could not parse self-model, using defaults." << std::endl;
        }
    }

    void save_self_model() {
        json doc;
        for (auto& [domain, d] : self_model) {
            doc[domain] = {
                {"success",               d.success},
                {"failure",               d.failure},
                {"last_attempt_ts",       d.last_attempt_ts},
                {"predicted_success_rate", d.predicted_success_rate},
                {"actual_success_rate",   d.actual_success_rate},
                {"prediction_error",      d.prediction_error},
                {"example_commands",      d.example_commands},
                {"novelty_score",         d.novelty_score},
                {"consecutive_failures",  d.consecutive_failures},
                {"cooldown_until",        d.cooldown_until}
            };
        }

        std::ofstream f(self_model_path);
        if (f.is_open()) {
            f << doc.dump(2);
        }
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[++i];
    }
    neuroswarm::BasalGangliaLobe bg(ip);
    bg.start();
    return 0;
}
