#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
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
#include <cstdlib>

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
        routing::subscribe(sub, {
            "execution_result", "intrinsic_goal_request",
            "homeostatic_pulse", "high_stress_alert",
            "time_pulse", "intrinsic_goal_result",
            "lobe_crash", "lobe_death", "metabolic_alert"
        });

        mkdir("./data", 0755);
        load_self_model();
        init_domain_commands();

        std::cout << "[BASAL_GANGLIA] Intrinsic motivation engine online. "
                  << domains.size() << " capability domains tracked." << std::endl;
    }

    void start() {
        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            try {

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
                    check_cooldowns();
                }
                else if (origin == "frontal_executive" && intent == "intrinsic_goal_result") {
                    handle_intrinsic_result(j);
                }
                // Phase 3: Drive Hierarchy — survival-level events
                else if (origin == "cerebral_matrix" && intent == "lobe_crash") {
                    std::string lobe_name = j.value("lobe_name", "unknown");
                    pending_drives.push_back({DriveLevel::SURVIVAL,
                        "Lobe " + lobe_name + " crashed — diagnose and recover",
                        lobe_name});
                    std::cout << "[BASAL_GANGLIA] SURVIVAL DRIVE: lobe_crash for " << lobe_name << std::endl;
                }
                else if (origin == "cerebral_matrix" && intent == "lobe_death") {
                    std::string lobe_name = j.value("lobe_name", "unknown");
                    pending_drives.push_back({DriveLevel::SURVIVAL,
                        "Lobe " + lobe_name + " is DEAD — critical system degradation",
                        lobe_name});
                    std::cout << "[BASAL_GANGLIA] SURVIVAL DRIVE: lobe_death for " << lobe_name << std::endl;
                }
                // Phase 5: Metabolic alerts raise homeostasis drive
                else if (origin == "homeostasis" && intent == "metabolic_alert") {
                    float stamina = j.value("stamina", 0.0f);
                    current_stamina = stamina;
                    pending_drives.push_back({DriveLevel::HOMEOSTASIS,
                        "Stamina critically low (" + std::to_string((int)stamina) + "%) — request sleep",
                        "homeostasis"});
                    std::cout << "[BASAL_GANGLIA] HOMEOSTASIS DRIVE: metabolic_alert stamina=" << stamina << "%" << std::endl;
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
    float current_stamina = 100.0f;  // Phase 5: metabolic energy level
    std::string current_timestamp;

    // Phase 3: Drive Hierarchy — Maslow-style priority pyramid
    enum class DriveLevel {
        SURVIVAL = 0,     // respond to lobe_crash, lobe_death
        HOMEOSTASIS = 1,  // respond to high CPU/RAM/temp, metabolic alerts
        EXPLORATION = 2,  // intrinsic motivation — try untested domains
        MASTERY = 3,      // retry domains with low success rates
        SELF_MODIFY = 4   // source modification, compilation, code generation
    };

    struct PendingDrive {
        DriveLevel level;
        std::string description;
        std::string trigger_lobe;  // which lobe triggered this drive
    };

    std::vector<PendingDrive> pending_drives;

    DriveLevel get_current_drive() {
        if (!pending_drives.empty()) {
            // Sort by priority (lowest enum = highest priority)
            auto best = std::min_element(pending_drives.begin(), pending_drives.end(),
                [](const PendingDrive& a, const PendingDrive& b) {
                    return static_cast<int>(a.level) < static_cast<int>(b.level);
                });
            return best->level;
        }
        // Default: determine from system state
        if (system_stress > 0.8f) return DriveLevel::HOMEOSTASIS;

        // Check overall success rate for SELF_MODIFY eligibility
        int total_s = 0, total_a = 0;
        for (auto& [d, st] : self_model) {
            total_s += st.success;
            total_a += st.success + st.failure;
        }
        float overall_rate = total_a > 0 ? (float)total_s / total_a : 0.0f;
        if (overall_rate > 0.70f) return DriveLevel::SELF_MODIFY;

        // Check if any domain has low success rate for MASTERY
        for (auto& [d, st] : self_model) {
            int t = st.success + st.failure;
            if (t >= 5 && st.actual_success_rate < 0.4f) return DriveLevel::MASTERY;
        }

        return DriveLevel::EXPLORATION;
    }

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

    // Cross-domain affinity — success in one domain boosts confidence in related ones
    std::map<std::string, std::vector<std::string>> domain_affinity = {
        {"file_read",           {"self_inspection", "log_analysis", "memory_analysis"}},
        {"file_write",          {"script_creation", "source_modification"}},
        {"file_search",         {"source_modification", "log_analysis", "memory_analysis"}},
        {"process_inspection",  {"system_monitoring", "network_diagnostics"}},
        {"network_diagnostics", {"process_inspection", "system_monitoring"}},
        {"source_modification", {"compilation", "file_write"}},
        {"compilation",         {"source_modification"}},
        {"git_operations",      {"source_modification", "file_write"}},
        {"system_monitoring",   {"process_inspection"}},
        {"data_analysis",       {"log_analysis", "memory_analysis"}},
        {"script_creation",     {"file_write", "source_modification"}},
        {"self_inspection",     {"file_read", "memory_analysis"}},
        {"memory_analysis",     {"data_analysis", "self_inspection"}},
        {"log_analysis",        {"data_analysis", "file_read"}}
    };

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
        // Order matters: more specific domains first, generic I/O (file_read/write) last
        classification_rules = {
            {"self_inspection",     {"self_model", "neuroswarm", "du -s"}},
            {"memory_analysis",     {"engram", "memory_index", "system_knowledge"}},
            {"log_analysis",        {"progress.txt", "metrics/", "journal", "syslog", "dmesg"}},
            {"data_analysis",       {"python3", "jq ", "data/metrics", "data/self_model"}},
            {"git_operations",      {"git "}},
            {"compilation",         {"cmake", "make ", "gcc", "g++"}},
            {"network_diagnostics", {"ss ", "netstat", "nc ", "curl ", "wget ", "ping ", "nmap ", "ip addr", "ifconfig"}},
            {"process_inspection",  {"ps ", "pgrep", "top ", "htop", "pidof", "kill ", "/proc/"}},
            {"source_modification", {"sed ", "patch ", "diff ", "src/"}},
            {"script_creation",     {"#!/", "chmod +x", "bash -c", "sh -c"}},
            {"system_monitoring",   {"uptime", "free ", "df ", "vmstat", "iostat", "sensors", "nvidia-smi", "lscpu", "uname"}},
            {"file_search",         {"find ", "grep ", "locate ", "which ", "whereis ", "fd ", "rg "}},
            {"file_write",          {"echo ", "tee ", "cp ", "mv ", "touch ", "mkdir ", ">>", "> "}},
            {"file_read",           {"cat ", "head ", "tail ", "less ", "more ", "wc -l", "file ", "stat ", "md5sum", "sha256sum", "readlink"}}
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

        // Cross-domain transfer: propagate confidence to related domains
        if (success && domain_affinity.count(domain)) {
            for (auto& related : domain_affinity[domain]) {
                if (self_model.count(related)) {
                    auto& rd = self_model[related];
                    // Small boost — 5% toward higher confidence
                    rd.predicted_success_rate = std::min(1.0f, rd.predicted_success_rate + 0.05f);
                }
            }
        }

        // Phase 4: Update genome fitness for executed command
        update_genome_fitness(domain, cmd, success);

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

        // Phase 3: Check pending survival/homeostasis drives first
        if (!pending_drives.empty()) {
            // Sort and pop highest-priority drive
            auto best_it = std::min_element(pending_drives.begin(), pending_drives.end(),
                [](const PendingDrive& a, const PendingDrive& b) {
                    return static_cast<int>(a.level) < static_cast<int>(b.level);
                });

            PendingDrive drive = *best_it;
            pending_drives.erase(best_it);

            // SURVIVAL drives get urgency=1.0 regardless of stamina
            std::string drive_name;
            switch (drive.level) {
                case DriveLevel::SURVIVAL:    drive_name = "SURVIVAL"; break;
                case DriveLevel::HOMEOSTASIS: drive_name = "HOMEOSTASIS"; break;
                default:                      drive_name = "UNKNOWN"; break;
            }

            json goal = {
                {"cid", cid},
                {"origin", "basal_ganglia"},
                {"intent", "intrinsic_goal"},
                {"domain", "self_inspection"},
                {"fitness", 1.0f},
                {"drive_level", drive_name},
                {"suggested_commands", json::array({
                    "ps aux | grep -E '(thalamus|motor_lobe|frontal|synaptic)' | grep -v grep",
                    "ls -la /proc/self/fd/ | wc -l",
                    "free -h | head -2"
                })},
                {"context", "[" + drive_name + " DRIVE] " + drive.description}
            };
            routing::publish(pub, goal);
            std::cout << "[BASAL_GANGLIA] " << drive_name << " goal published: " << drive.description << std::endl;
            return;
        }

        // Phase 5: Stamina gating — suppress exploration when exhausted
        DriveLevel current_drive = get_current_drive();
        if (current_stamina < 20.0f && current_drive >= DriveLevel::EXPLORATION) {
            std::cout << "[BASAL_GANGLIA] Stamina too low (" << (int)current_stamina
                      << "%). Suppressing exploration." << std::endl;
            // Request sleep instead
            json sleep = {
                {"origin", "basal_ganglia"},
                {"intent", "initiate_sleep_cycle"},
                {"reason", "low_stamina"}
            };
            routing::publish(pub, sleep);
            return;
        }

        // Find the domain with highest fitness, respecting cooldowns and drive level
        long now_ts = std::time(nullptr);
        std::string best_domain;
        float best_fitness = -999.0f;

        for (auto& domain : domains) {
            auto& d = self_model[domain];

            // Skip domains in cooldown
            if (d.cooldown_until > now_ts) continue;

            // Phase 3: Drive-level filtering
            if (current_drive == DriveLevel::MASTERY) {
                // Only consider domains with poor success rates
                int t = d.success + d.failure;
                if (t < 5 || d.actual_success_rate >= 0.4f) continue;
            } else if (current_drive == DriveLevel::SELF_MODIFY) {
                // Only consider source_modification and compilation
                if (domain != "source_modification" && domain != "compilation") continue;
            }

            float f = compute_fitness(domain);

            // Phase 5: Stamina factor — scale non-survival fitness by energy
            f *= (current_stamina / 100.0f);

            if (f > best_fitness) {
                best_fitness = f;
                best_domain = domain;
            }
        }

        if (best_domain.empty()) {
            std::cout << "[BASAL_GANGLIA] No suitable domain for current drive. Falling back to exploration." << std::endl;
            // Fall back to any non-cooldown domain
            for (auto& domain : domains) {
                if (self_model[domain].cooldown_until > now_ts) continue;
                float f = compute_fitness(domain);
                if (f > best_fitness) { best_fitness = f; best_domain = domain; }
            }
            if (best_domain.empty()) {
                std::cout << "[BASAL_GANGLIA] All domains in cooldown. No intrinsic goal available." << std::endl;
                return;
            }
        }

        auto& d = self_model[best_domain];
        int total = d.success + d.failure;

        // Build context string
        std::string drive_label;
        switch (current_drive) {
            case DriveLevel::SURVIVAL:    drive_label = "SURVIVAL"; break;
            case DriveLevel::HOMEOSTASIS: drive_label = "HOMEOSTASIS"; break;
            case DriveLevel::EXPLORATION: drive_label = "EXPLORATION"; break;
            case DriveLevel::MASTERY:     drive_label = "MASTERY"; break;
            case DriveLevel::SELF_MODIFY: drive_label = "SELF_MODIFY"; break;
        }

        std::string context;
        if (total == 0) {
            context = "Never attempted. ";
        } else {
            context = "Attempted " + std::to_string(total) + " times. "
                    + "Success rate: " + std::to_string((int)(d.actual_success_rate * 100)) + "%. "
                    + "Prediction error: " + std::to_string(d.prediction_error).substr(0, 5) + ". ";
        }
        context += "Drive: " + drive_label + ". ";
        context += "Stamina: " + std::to_string((int)current_stamina) + "%. ";
        context += "Stress: " + std::to_string((int)(system_stress * 100)) + "%.";

        // Get suggested commands — Phase 4 genome templates override if available
        auto suggested = get_suggested_commands(best_domain);

        json goal = {
            {"cid", cid},
            {"origin", "basal_ganglia"},
            {"intent", "intrinsic_goal"},
            {"domain", best_domain},
            {"fitness", best_fitness},
            {"drive_level", drive_label},
            {"suggested_commands", suggested},
            {"context", context}
        };

        routing::publish(pub, goal);

        std::cout << "[BASAL_GANGLIA] Intrinsic goal published: domain='" << best_domain
                  << "' fitness=" << best_fitness << " drive=" << drive_label
                  << " (" << context << ")" << std::endl;
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

        routing::publish(pub, signal);

        std::cout << "[BASAL_GANGLIA] Dopamine signal: " << reason
                  << " in domain '" << domain << "' (magnitude=" << magnitude << ")" << std::endl;
    }

    void emit_self_model_updated(const std::string& domain) {
        json update = {
            {"origin", "basal_ganglia"},
            {"intent", "self_model_updated"},
            {"domain", domain}
        };

        routing::publish(pub, update);
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

    // Phase 4: Command Genome — evolutionary command template system
    struct CommandTemplate {
        std::string cmd;
        float fitness = 0.5f;
        int generation = 0;
    };

    std::map<std::string, std::vector<CommandTemplate>> command_genome;
    static constexpr const char* GENOME_PATH = "./data/command_genome.json";

    void load_genome() {
        std::ifstream f(GENOME_PATH);
        if (!f.is_open()) return;
        try {
            json doc;
            f >> doc;
            for (auto& [domain, templates] : doc.items()) {
                for (auto& t : templates) {
                    CommandTemplate ct;
                    ct.cmd = t.value("cmd", "");
                    ct.fitness = t.value("fitness", 0.5f);
                    ct.generation = t.value("generation", 0);
                    if (!ct.cmd.empty()) command_genome[domain].push_back(ct);
                }
            }
            std::cout << "[BASAL_GANGLIA] Loaded command genome from " << GENOME_PATH << std::endl;
        } catch (...) {
            std::cout << "[BASAL_GANGLIA] Could not parse genome, starting fresh." << std::endl;
        }
    }

    void save_genome() {
        json doc;
        for (auto& [domain, templates] : command_genome) {
            json arr = json::array();
            for (auto& t : templates) {
                arr.push_back({{"cmd", t.cmd}, {"fitness", t.fitness}, {"generation", t.generation}});
            }
            doc[domain] = arr;
        }
        std::ofstream f(GENOME_PATH);
        if (f.is_open()) f << doc.dump(2);
    }

    // Fitness-proportional selection (roulette wheel)
    std::string select_template(const std::string& domain) {
        auto it = command_genome.find(domain);
        if (it == command_genome.end() || it->second.empty()) return "";

        float total = 0.0f;
        for (auto& t : it->second) total += std::max(0.01f, t.fitness);

        float r = (float)(rand() % 10000) / 10000.0f * total;
        float accum = 0.0f;
        for (auto& t : it->second) {
            accum += std::max(0.01f, t.fitness);
            if (accum >= r) return t.cmd;
        }
        return it->second.back().cmd;
    }

    // Update template fitness after execution result
    void update_genome_fitness(const std::string& domain, const std::string& cmd, bool success) {
        auto& templates = command_genome[domain];

        // Find matching template
        bool found = false;
        for (auto& t : templates) {
            if (t.cmd == cmd) {
                float outcome = success ? 1.0f : 0.0f;
                t.fitness = 0.9f * t.fitness + 0.1f * outcome;
                found = true;
                break;
            }
        }

        // Novel command: add as new template
        if (!found && cmd.size() <= 200) {
            int max_gen = 0;
            for (auto& t : templates) max_gen = std::max(max_gen, t.generation);

            CommandTemplate ct;
            ct.cmd = cmd;
            ct.fitness = success ? 0.6f : 0.3f;
            ct.generation = max_gen + 1;
            templates.push_back(ct);

            // Cap at 20 templates per domain
            if (templates.size() > 20) {
                // Remove lowest fitness
                auto worst = std::min_element(templates.begin(), templates.end(),
                    [](const CommandTemplate& a, const CommandTemplate& b) { return a.fitness < b.fitness; });
                templates.erase(worst);
            }
        }

        save_genome();
    }

    // Get suggested commands: prefer genome templates, fall back to static commands
    json get_suggested_commands(const std::string& domain) {
        json cmds = json::array();

        // Try genome templates first (top 3 by fitness)
        auto it = command_genome.find(domain);
        if (it != command_genome.end() && !it->second.empty()) {
            auto sorted = it->second;
            std::sort(sorted.begin(), sorted.end(),
                [](const CommandTemplate& a, const CommandTemplate& b) { return a.fitness > b.fitness; });
            for (size_t i = 0; i < sorted.size() && i < 3; i++) {
                cmds.push_back(sorted[i].cmd);
            }
            // Add one random template for exploration (mutation pressure)
            if (sorted.size() > 3) {
                int idx = rand() % sorted.size();
                cmds.push_back(sorted[idx].cmd);
            }
        }

        // Fall back to static domain commands if genome is empty
        if (cmds.empty() && domain_commands.count(domain)) {
            for (auto& c : domain_commands[domain]) cmds.push_back(c);
        }

        return cmds;
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

        // Phase 4: Load command genome
        load_genome();
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
