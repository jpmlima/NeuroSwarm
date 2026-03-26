#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <ctime>
#include <algorithm>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

// ─── Knowledge Gap Model ──────────────────────────────────────
// A structural description of something the system cannot do and WHY

struct KnowledgeGap {
    std::string id;
    std::string domain;                     // which domain fails
    std::string error_pattern;              // classified error type
    std::string missing_capability;         // what's needed to succeed
    std::string root_cause;                 // structural reason for failure
    int occurrence_count = 0;
    float importance = 0.0f;               // how many goals blocked by this
    std::vector<std::string> blocked_goals; // recent goal cids blocked
    std::string exploration_strategy;       // how to address this gap
    double last_seen = 0.0;
};

// A single failure observation
struct FailureRecord {
    std::string command;
    std::string domain;
    std::string error_pattern;
    std::string output_snippet;            // first 200 chars
    double timestamp;
};

// Precondition chain link: "to do X, I first need Y"
struct PreconditionLink {
    std::string capability;                // what I'm trying to do
    std::string requires;                  // what I need first
    int evidence_count = 0;                // how many failures support this link
};

class MetaCognition {
public:
    MetaCognition(const std::string& pub_addr = "tcp://localhost:5555",
                  const std::string& sub_addr = "tcp://localhost:5556")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect(pub_addr);
        sub.connect(sub_addr);
        routing::subscribe_all(sub);

        load_state();

        std::cout << "[META-COGNITION] Recursive self-observation active. "
                  << gaps.size() << " knowledge gaps, "
                  << precondition_chain.size() << " precondition links." << std::endl;
        init_diary();
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
    zmq::socket_t pub;
    zmq::socket_t sub;

    // ─── Original state ──────────────────────────────────────
    float last_stress = 0.0f;
    float last_success_rate = 1.0f;
    int total_sleep_cycles = 0;
    int total_learned_memories = 0;
    std::time_t last_reflection_time = 0;
    std::time_t last_gap_analysis_time = 0;

    // ─── Recursive Meta-Cognition state ──────────────────────
    std::map<std::string, KnowledgeGap> gaps;               // error_pattern → gap
    std::vector<FailureRecord> recent_failures;              // sliding window
    std::vector<PreconditionLink> precondition_chain;        // learned dependency graph
    std::map<std::string, int> domain_failure_counts;        // domain → total failures
    std::map<std::string, int> domain_success_counts;        // domain → total successes
    std::map<std::string, std::string> last_domain_error;    // domain → last error pattern
    int total_failures = 0;
    int total_successes = 0;

    // ─── Repetition Detection (self-awareness of stagnation) ───
    // Track recent commands to detect when the system is stuck in a loop
    std::vector<std::string> recent_commands;                // sliding window of last N commands
    std::map<std::string, int> command_repetition_count;     // cmd → consecutive count
    std::string last_command;
    int consecutive_same = 0;
    static constexpr int REPETITION_WINDOW = 30;
    static constexpr int REPETITION_THRESHOLD = 5;           // same cmd 5x in window → stagnation
    std::time_t last_stagnation_alert = 0;

    static constexpr int MAX_RECENT_FAILURES = 200;
    static constexpr int GAP_ANALYSIS_INTERVAL = 300;       // 5 minutes

    // ─── Error Pattern Classification ────────────────────────

    struct ErrorClass {
        std::string pattern;            // what to look for in output
        std::string error_type;         // classified name
        std::string missing_capability; // what capability is needed
        std::string exploration;        // suggested exploration strategy
    };

    static const std::vector<ErrorClass>& error_classes() {
        static const std::vector<ErrorClass> classes = {
            {"No such file or directory", "missing_file",
             "filesystem_navigation", "search_filesystem to discover correct paths"},
            {"Permission denied", "permission_denied",
             "access_control", "inspect permissions, learn chmod/chown patterns"},
            {"command not found", "missing_tool",
             "tool_discovery", "search for alternative tools or install paths"},
            {"Is a directory", "type_mismatch",
             "path_type_awareness", "use stat/file to check types before operating"},
            {"No space left on device", "resource_exhaustion",
             "disk_management", "learn du/df to monitor and clean disk space"},
            {"Connection refused", "network_unreachable",
             "network_diagnostics", "probe service status before connecting"},
            {"Segmentation fault", "crash",
             "debugging", "learn gdb/valgrind for crash analysis"},
            {"syntax error", "syntax_error",
             "language_syntax", "learn correct syntax for target language"},
            {"undefined reference", "link_error",
             "build_dependencies", "search for missing libraries and headers"},
            {"cannot find", "missing_dependency",
             "dependency_resolution", "search for package or file locations"},
            {"error:", "generic_error",
             "error_analysis", "examine error output patterns more carefully"},
            {"Error:", "generic_error",
             "error_analysis", "examine error output patterns more carefully"},
            {"fatal:", "fatal_error",
             "recovery", "understand fatal conditions and prevention strategies"},
            {"not found", "not_found",
             "resource_discovery", "search for resource locations before use"},
            {"already exists", "conflict",
             "state_awareness", "check state before creating/modifying resources"},
            {"timed out", "timeout",
             "timeout_handling", "learn to set appropriate timeouts and retries"},
        };
        return classes;
    }

    // ─── Event Processing ───────────────────────────────────

    void init_diary() {
        fs::create_directories("logs");
        if (!fs::exists("logs/THOUGHTS.md")) {
            std::ofstream f("logs/THOUGHTS.md");
            f << "# NeuroSwarm: Meta-Cognitive Thought Stream\n\n";
            f << "*Chronicle of a digital organism's evolution.*\n\n---\n";
        }
    }

    void process_event(const json& event) {
        std::string intent = event.value("intent", "");
        std::string origin = event.value("origin", "");

        if (intent == "homeostatic_pulse") {
            last_success_rate = event.value("success_rate", 1.0f);
            last_stress = std::max(0.0f, 1.0f - last_success_rate);
        }
        else if (intent == "sleep_cycle_complete") {
            total_sleep_cycles++;
            total_learned_memories += event.value("learned_memories", 0);
            record_reflection("Sleep Cycle Complete. Integrated " +
                              std::to_string(event.value("learned_memories", 0)) +
                              " new success memories into executive policy.");
        }
        else if (intent == "execution_result" && origin == "motor_cortex") {
            handle_execution_result(event);
        }

        // Periodic reflection + gap analysis
        std::time_t now = std::time(nullptr);
        if (now - last_reflection_time > 300) {
            periodic_reflection();
            last_reflection_time = now;
        }
        if (now - last_gap_analysis_time > GAP_ANALYSIS_INTERVAL) {
            analyze_gaps();
            save_state();
            last_gap_analysis_time = now;
        }
    }

    // ─── Substantive Success Filter (mirrors BasalGanglia) ────────

    bool is_substantive_success(const std::string& cmd, const std::string& output) {
        // Corrupted fragments starting with hyphens
        if (cmd.find("-") == 0) return false;

        // Loop attractors — commands the system fixates on without learning
        if (cmd == "whoami" || cmd == "id" || cmd == "hostname" || cmd == "pwd") return false;

        // Pure echo commands — exit 0 but no real work
        if (cmd.find("echo ") == 0 && cmd.find("&&") == std::string::npos
            && cmd.find("|") == std::string::npos) return false;

        // Echo + /dev/null — degenerate pattern
        if (cmd.find("echo ") != std::string::npos && cmd.find("/dev/null") != std::string::npos)
            return false;

        // Detect shell error strings in output
        std::vector<std::string> shell_errors = {
            "command not found", "No such file", "sh: line 1", "syntax error",
            "not a directory", "permission denied", "invalid option", "usage:"
        };
        std::string lower_out = output;
        std::transform(lower_out.begin(), lower_out.end(), lower_out.begin(), ::tolower);
        for (const auto& err : shell_errors) {
            if (lower_out.find(err) != std::string::npos) return false;
        }

        // Detect "|| echo" trick where output matches the echo'd fallback
        size_t echo_pos = cmd.find("|| echo ");
        if (echo_pos != std::string::npos) {
            std::string echo_val = cmd.substr(echo_pos + 8);
            echo_val.erase(std::remove(echo_val.begin(), echo_val.end(), '\''), echo_val.end());
            echo_val.erase(std::remove(echo_val.begin(), echo_val.end(), '\"'), echo_val.end());
            
            std::string trimmed_out = output;
            trimmed_out.erase(std::remove(trimmed_out.begin(), trimmed_out.end(), '\n'), trimmed_out.end());
            trimmed_out.erase(std::remove(trimmed_out.begin(), trimmed_out.end(), '\r'), trimmed_out.end());
            if (trimmed_out == echo_val) return false;
        }

        // Commands that produce no output are suspicious
        if (output.size() < 3) return false;

        // Output is just echo text
        if (output.find("Running '") == 0 || output.find("Running \"") == 0)
            return false;

        return true;
    }

    // ─── Execution Result Analysis ──────────────────────────

    void handle_execution_result(const json& event) {
        std::string mode = event.value("mode", "reality");
        if (mode == "dream") return;

        std::string cmd = event.value("command", "");
        std::string status = event.value("status", "");
        std::string output = event.value("proprioception", "");
        std::string domain = event.value("domain", "");
        std::string cid = event.value("cid", "");
        bool success = (status == "success");

        // motor_cortex doesn't include domain — classify locally
        if (domain.empty() && !cmd.empty()) {
            domain = classify_domain(cmd);
        }
        if (domain.empty()) return;

        // Apply substantive success filter — mirrors BasalGanglia logic
        // Degenerate commands (echo-only, no output, placeholder text) should
        // not count as successes; treating them as failures ensures gaps populate
        if (success && !is_substantive_success(cmd, output)) {
            success = false;
        }

        // ─── Repetition detection: notice when we're stuck in a loop ───
        detect_repetition(cmd, domain);

        if (success) {
            domain_success_counts[domain]++;
            total_successes++;
            // Success can resolve gaps — check if this domain's last error is now overcome
            check_gap_resolution(domain);
        } else {
            domain_failure_counts[domain]++;
            total_failures++;

            // Classify the error
            std::string error_type = classify_error(output);
            last_domain_error[domain] = error_type;

            // Record failure
            FailureRecord fr;
            fr.command = cmd;
            fr.domain = domain;
            fr.error_pattern = error_type;
            fr.output_snippet = output.substr(0, 200);
            fr.timestamp = (double)std::time(nullptr);
            recent_failures.push_back(fr);

            // Prune
            while (recent_failures.size() > MAX_RECENT_FAILURES)
                recent_failures.erase(recent_failures.begin());

            // Update or create knowledge gap
            update_gap(domain, error_type, cid, output);

            // Infer precondition chains
            infer_preconditions(domain, error_type);
        }
    }

    std::string classify_error(const std::string& output) {
        if (output.empty()) return "unknown_error";

        for (auto& ec : error_classes()) {
            if (output.find(ec.pattern) != std::string::npos) {
                return ec.error_type;
            }
        }
        return "unknown_error";
    }

    void update_gap(const std::string& domain, const std::string& error_type,
                    const std::string& cid, const std::string& output) {
        std::string gap_key = domain + ":" + error_type;

        if (!gaps.count(gap_key)) {
            KnowledgeGap gap;
            gap.id = "gap_" + std::to_string(gaps.size());
            gap.domain = domain;
            gap.error_pattern = error_type;
            gap.last_seen = (double)std::time(nullptr);

            // Find the matching error class for capability and strategy
            for (auto& ec : error_classes()) {
                if (ec.error_type == error_type) {
                    gap.missing_capability = ec.missing_capability;
                    gap.exploration_strategy = ec.exploration;
                    break;
                }
            }
            if (gap.missing_capability.empty()) {
                gap.missing_capability = "general_" + domain;
                gap.exploration_strategy = "explore " + domain + " with varied approaches";
            }

            gaps[gap_key] = gap;
        }

        auto& gap = gaps[gap_key];
        gap.occurrence_count++;
        gap.last_seen = (double)std::time(nullptr);

        // Track blocked goals (keep last 10)
        if (!cid.empty()) {
            gap.blocked_goals.push_back(cid);
            if (gap.blocked_goals.size() > 10)
                gap.blocked_goals.erase(gap.blocked_goals.begin());
        }

        // Infer root cause from pattern
        gap.root_cause = infer_root_cause(domain, error_type, output);
    }

    std::string infer_root_cause(const std::string& domain, const std::string& error_type,
                                  const std::string& output) {
        // Recursive reasoning: WHY does this error happen?
        if (error_type == "missing_file")
            return "path_unknown: the system does not know the correct file location";
        if (error_type == "permission_denied")
            return "access_missing: the system lacks the necessary permissions";
        if (error_type == "missing_tool")
            return "tool_absent: the required binary is not installed or not in PATH";
        if (error_type == "link_error")
            return "dependency_gap: a library or header is missing from the build";
        if (error_type == "missing_dependency")
            return "dependency_gap: a required resource has not been located";
        if (error_type == "syntax_error")
            return "syntax_model_incomplete: the system's model of this language syntax is insufficient";
        if (error_type == "type_mismatch")
            return "state_model_wrong: the system predicted a file but found a directory (or vice versa)";
        if (error_type == "conflict")
            return "state_model_stale: the system's model of current state is outdated";
        if (error_type == "crash")
            return "runtime_defect: the generated or executed code has a memory/logic error";
        if (error_type == "timeout")
            return "timing_model_wrong: the system underestimated execution time";

        return "unknown: insufficient data to determine root cause for " + domain + "/" + error_type;
    }

    // ─── Precondition Chain Inference ───────────────────────
    // "I can't do X because I need Y first"

    void infer_preconditions(const std::string& domain, const std::string& error_type) {
        // Map error types to precondition capabilities
        static const std::map<std::string, std::string> error_to_precondition = {
            {"missing_file", "filesystem_navigation"},
            {"permission_denied", "access_control"},
            {"missing_tool", "tool_discovery"},
            {"type_mismatch", "path_type_awareness"},
            {"link_error", "build_dependencies"},
            {"missing_dependency", "dependency_resolution"},
            {"not_found", "resource_discovery"},
            {"conflict", "state_awareness"},
            {"crash", "debugging"},
            {"syntax_error", "language_syntax"},
        };

        if (!error_to_precondition.count(error_type)) return;

        std::string required = error_to_precondition.at(error_type);

        // Check if this link already exists
        for (auto& link : precondition_chain) {
            if (link.capability == domain && link.requires == required) {
                link.evidence_count++;
                // Re-publish at milestones (3, 5, 10) for confidence-weighted learning
                if (link.evidence_count == 3 || link.evidence_count == 5 || link.evidence_count == 10) {
                    json discovery = {
                        {"origin", "metacognition"},
                        {"intent", "precondition_discovery"},
                        {"domain", domain},
                        {"required_capability", required},
                        {"error_type", error_type},
                        {"evidence_count", link.evidence_count}
                    };
                    routing::publish(pub, discovery);
                }
                return;
            }
        }

        PreconditionLink link;
        link.capability = domain;
        link.requires = required;
        link.evidence_count = 1;
        precondition_chain.push_back(link);

        std::cout << "[META-COGNITION] PRECONDITION CHAIN: '" << domain
                  << "' requires '" << required << "' (error: " << error_type << ")"
                  << std::endl;

        // Publish discovery so PrimordialLoop can enrich operators
        json discovery = {
            {"origin", "metacognition"},
            {"intent", "precondition_discovery"},
            {"domain", domain},
            {"required_capability", required},
            {"error_type", error_type},
            {"evidence_count", 1}
        };
        routing::publish(pub, discovery);

        // Recursive: does the required capability itself have unmet preconditions?
        // Check if we have gaps for the required capability
        for (auto& [key, gap] : gaps) {
            if (gap.missing_capability == required && gap.occurrence_count > 2) {
                std::cout << "[META-COGNITION] RECURSIVE GAP: '" << required
                          << "' is itself blocked by '" << gap.error_pattern
                          << "' in domain '" << gap.domain << "'" << std::endl;
            }
        }
    }

    // ─── Repetition Detection (self-awareness of stagnation) ───
    // "I notice I keep doing the same thing. This is not learning."

    void detect_repetition(const std::string& cmd, const std::string& domain) {
        // Track in sliding window
        recent_commands.push_back(cmd);
        if (recent_commands.size() > REPETITION_WINDOW)
            recent_commands.erase(recent_commands.begin());

        // Count occurrences of each command in window
        std::map<std::string, int> window_counts;
        for (auto& c : recent_commands) window_counts[c]++;

        // Find the most repeated command
        std::string worst_cmd;
        int worst_count = 0;
        for (auto& [c, n] : window_counts) {
            if (n > worst_count) { worst_count = n; worst_cmd = c; }
        }

        // Detect stagnation: same command dominates the window
        std::time_t now = std::time(nullptr);
        if (worst_count >= REPETITION_THRESHOLD && now - last_stagnation_alert > 30) {
            last_stagnation_alert = now;

            float repetition_ratio = (float)worst_count / recent_commands.size();

            std::cout << "[META-COGNITION] STAGNATION DETECTED: '"
                      << worst_cmd.substr(0, 50) << "' repeated "
                      << worst_count << "/" << recent_commands.size()
                      << " times (" << (int)(repetition_ratio * 100) << "%). "
                      << "Domain: " << domain << ". This is not learning." << std::endl;

            record_reflection("Stagnation detected: command '" + worst_cmd.substr(0, 50) +
                            "' repeated " + std::to_string(worst_count) + " times in last " +
                            std::to_string(recent_commands.size()) + " executions. " +
                            "The system is stuck in a success loop without novelty.");

            // Publish stagnation alert — BasalGanglia should deprioritize this domain
            json alert = {
                {"origin", "metacognition"},
                {"intent", "stagnation_alert"},
                {"domain", domain},
                {"repeated_command", worst_cmd},
                {"repetition_count", worst_count},
                {"window_size", (int)recent_commands.size()},
                {"repetition_ratio", repetition_ratio}
            };
            routing::publish(pub, alert);
        }
    }

    // ─── Gap Resolution Detection ───────────────────────────

    void check_gap_resolution(const std::string& domain) {
        // If a domain starts succeeding, its gaps may be resolving
        int successes = domain_success_counts[domain];
        int failures = domain_failure_counts[domain];
        if (successes + failures < 5) return;

        float recent_rate = (float)successes / (successes + failures);
        if (recent_rate > 0.7f) {
            // Domain is healthy — mark related gaps as resolving
            std::vector<std::string> resolving;
            for (auto& [key, gap] : gaps) {
                if (gap.domain == domain && gap.occurrence_count > 0) {
                    resolving.push_back(key);
                }
            }
            for (auto& key : resolving) {
                auto& gap = gaps[key];
                gap.importance *= 0.5f;  // Decay importance
                if (gap.importance < 0.1f && gap.occurrence_count < 3) {
                    std::cout << "[META-COGNITION] GAP RESOLVED: " << gap.domain
                              << "/" << gap.error_pattern << " (capability '"
                              << gap.missing_capability << "' acquired)" << std::endl;
                    record_reflection("Knowledge gap resolved: " + gap.domain +
                                    " no longer blocked by " + gap.error_pattern +
                                    ". Capability '" + gap.missing_capability + "' appears functional.");
                    gaps.erase(key);
                }
            }
        }
    }

    // ─── Gap Analysis & Exploration Targets ─────────────────

    void analyze_gaps() {
        if (gaps.empty()) return;

        // Compute importance for each gap: occurrence_count weighted by recency
        double now = (double)std::time(nullptr);
        for (auto& [key, gap] : gaps) {
            double age = now - gap.last_seen;
            double recency = std::exp(-age / 3600.0);  // half-life 1 hour
            gap.importance = (float)(gap.occurrence_count * recency);
        }

        // Find the most impactful gap (highest importance)
        std::string worst_key;
        float worst_importance = 0.0f;
        for (auto& [key, gap] : gaps) {
            if (gap.importance > worst_importance) {
                worst_importance = gap.importance;
                worst_key = key;
            }
        }

        if (worst_key.empty() || worst_importance < 1.0f) return;

        auto& worst = gaps[worst_key];

        // Build the recursive chain for this gap
        std::vector<std::string> chain;
        chain.push_back(worst.domain + " fails with " + worst.error_pattern);
        chain.push_back("→ needs capability: " + worst.missing_capability);

        // Walk the precondition chain
        std::string current_req = worst.missing_capability;
        std::set<std::string> visited;
        for (int depth = 0; depth < 5; ++depth) {
            if (visited.count(current_req)) break;
            visited.insert(current_req);

            bool found_deeper = false;
            for (auto& link : precondition_chain) {
                if (link.capability == current_req && link.evidence_count >= 2) {
                    chain.push_back("→ which requires: " + link.requires);
                    current_req = link.requires;
                    found_deeper = true;
                    break;
                }
            }
            if (!found_deeper) break;
        }

        // Find the deepest actionable target
        std::string exploration_target = current_req;

        // Publish exploration target
        json target_msg = {
            {"origin", "metacognition"},
            {"intent", "exploration_target"},
            {"cid", "meta_gap_" + std::to_string((int)now)},
            {"target_capability", exploration_target},
            {"target_domain", worst.domain},
            {"gap_error", worst.error_pattern},
            {"root_cause", worst.root_cause},
            {"importance", worst.importance},
            {"chain_depth", (int)chain.size()},
            {"exploration_strategy", worst.exploration_strategy}
        };
        routing::publish(pub, target_msg);

        // Publish knowledge gap summary
        json gap_msg = {
            {"origin", "metacognition"},
            {"intent", "knowledge_gap"},
            {"cid", "meta_gaps_" + std::to_string((int)now)},
            {"total_gaps", gaps.size()},
            {"total_failures", total_failures},
            {"total_successes", total_successes},
            {"worst_gap", {
                {"domain", worst.domain},
                {"error", worst.error_pattern},
                {"missing", worst.missing_capability},
                {"root_cause", worst.root_cause},
                {"occurrences", worst.occurrence_count},
                {"importance", worst.importance}
            }}
        };

        // Add chain
        json chain_json = json::array();
        for (auto& step : chain) chain_json.push_back(step);
        gap_msg["reasoning_chain"] = chain_json;

        routing::publish(pub, gap_msg);

        // Log the analysis
        std::string chain_str;
        for (auto& step : chain) {
            if (!chain_str.empty()) chain_str += "\n    ";
            chain_str += step;
        }

        std::cout << "[META-COGNITION] GAP ANALYSIS: " << gaps.size() << " gaps, worst: "
                  << worst.domain << "/" << worst.error_pattern
                  << " (importance=" << worst.importance << ")" << std::endl;
        std::cout << "[META-COGNITION] EXPLORATION TARGET: " << exploration_target
                  << " (" << worst.exploration_strategy << ")" << std::endl;

        record_reflection("Knowledge gap analysis: " + std::to_string(gaps.size()) +
                         " active gaps. Most critical: " + worst.domain + " blocked by " +
                         worst.error_pattern + ". Root cause: " + worst.root_cause +
                         ". Exploration target: " + exploration_target + ".");
    }

    // ─── Self-Modification: analyze system dynamics, propose parameter changes ───
    //
    // The system observes its own behavior patterns and proposes concrete
    // parameter adjustments. This is directed self-modification — not random
    // mutation, but reasoned changes based on meta-observations.

    std::time_t last_tuning_analysis = 0;
    static constexpr int TUNING_INTERVAL = 600;  // analyze every 10 minutes

    void self_modification_analysis() {
        std::time_t now = std::time(nullptr);
        if (now - last_tuning_analysis < TUNING_INTERVAL) return;
        last_tuning_analysis = now;

        json tuning = {
            {"origin", "metacognition"},
            {"intent", "system_tuning"},
            {"adjustments", json::array()}
        };

        auto& adjustments = tuning["adjustments"];

        // ── Analysis 1: Stagnation frequency → adjust cooldown scaling ──
        // If we send stagnation alerts too often, cooldowns are too short
        float stagnation_rate = 0;
        if (total_successes + total_failures > 0) {
            // Count stagnation alerts in recent commands window
            std::map<std::string, int> window_counts;
            for (auto& c : recent_commands) window_counts[c]++;
            int repeated = 0;
            for (auto& [c, n] : window_counts) {
                if (n >= REPETITION_THRESHOLD) repeated++;
            }
            stagnation_rate = (float)repeated / std::max(1, (int)window_counts.size());
        }

        if (stagnation_rate > 0.3f) {
            adjustments.push_back({
                {"parameter", "cooldown_multiplier"},
                {"current_signal", stagnation_rate},
                {"recommendation", "increase"},
                {"suggested_value", 180},  // 3 minutes minimum
                {"reasoning", "High stagnation rate (" + std::to_string((int)(stagnation_rate * 100)) +
                              "%) indicates cooldowns expire too quickly"}
            });
        }

        // ── Analysis 2: Domain diversity → redirect exploration ──
        int active_domains = 0;
        int stagnant_domains = 0;
        for (auto& [domain, failures] : domain_failure_counts) {
            int successes = domain_success_counts.count(domain) ? domain_success_counts[domain] : 0;
            if (successes + failures > 5) {
                active_domains++;
                float domain_sr = (float)successes / (successes + failures);
                if (domain_sr < 0.1f) stagnant_domains++;
            }
        }

        if (active_domains > 0 && stagnant_domains > active_domains / 2) {
            adjustments.push_back({
                {"parameter", "domain_rotation_pressure"},
                {"current_signal", (float)stagnant_domains / active_domains},
                {"recommendation", "increase"},
                {"suggested_value", 2.0},  // double rotation pressure
                {"reasoning", std::to_string(stagnant_domains) + "/" + std::to_string(active_domains) +
                              " domains stagnant — system should explore new domains more aggressively"}
            });
        }

        // ── Analysis 3: Success rate trajectory → adjust risk tolerance ──
        if (total_successes + total_failures > 50) {
            float overall_sr = (float)total_successes / (total_successes + total_failures);

            if (overall_sr > 0.7f) {
                // System is doing well — can afford more exploration
                adjustments.push_back({
                    {"parameter", "exploration_rate"},
                    {"current_signal", overall_sr},
                    {"recommendation", "increase"},
                    {"reasoning", "High success rate (" + std::to_string((int)(overall_sr * 100)) +
                                  "%) — system can afford more novel exploration"}
                });
            } else if (overall_sr < 0.2f) {
                // System is struggling — consolidate on known successes
                adjustments.push_back({
                    {"parameter", "exploration_rate"},
                    {"current_signal", overall_sr},
                    {"recommendation", "decrease"},
                    {"reasoning", "Low success rate (" + std::to_string((int)(overall_sr * 100)) +
                                  "%) — system should consolidate on proven operators"}
                });
            }
        }

        // ── Analysis 4: Precondition chain depth → suggest capability focus ──
        if (precondition_chain.size() > 5) {
            // Find the most demanded capability (appears most as "requires")
            std::map<std::string, int> demand;
            for (auto& link : precondition_chain) {
                demand[link.requires] += link.evidence_count;
            }
            std::string most_needed;
            int max_demand = 0;
            for (auto& [cap, count] : demand) {
                if (count > max_demand) { max_demand = count; most_needed = cap; }
            }

            if (max_demand >= 5) {
                adjustments.push_back({
                    {"parameter", "priority_capability"},
                    {"current_signal", max_demand},
                    {"recommendation", most_needed},
                    {"reasoning", "Capability '" + most_needed + "' is a prerequisite for " +
                                  std::to_string(max_demand) + " domain operations — prioritize acquiring it"}
                });
            }
        }

        // Publish if we have recommendations
        if (!adjustments.empty()) {
            routing::publish(pub, tuning);

            std::string thought = "SELF-MODIFICATION: Proposing " + std::to_string(adjustments.size()) +
                                  " parameter adjustments based on " + std::to_string(total_successes + total_failures) +
                                  " observations.";
            for (auto& adj : adjustments) {
                thought += " [" + adj.value("parameter", "") + ": " + adj.value("recommendation", "") + "]";
            }
            record_reflection(thought);
        }
    }

    // ─── Neuro-Surgery Proposal ─────────────────────────────
    // When a knowledge gap is persistent and severe, MetaCognition proposes
    // a concrete code modification. This is the bridge to Phase 4: the system
    // identifies WHAT to change about itself and publishes the intent.
    // FrontalExecutive + MotorLobe execute it via neuro_surgery mode.

    std::time_t last_surgery_proposal = 0;
    static constexpr int SURGERY_INTERVAL = 900;  // propose at most every 15 min
    std::set<std::string> attempted_surgeries;     // avoid repeating same fix

    void propose_neuro_surgery() {
        std::time_t now = std::time(nullptr);
        if (now - last_surgery_proposal < SURGERY_INTERVAL) return;

        // Find the most severe persistent gap
        std::string worst_gap_id;
        float worst_importance = 0.0f;
        KnowledgeGap* worst_gap = nullptr;

        for (auto& [id, gap] : gaps) {
            if (gap.importance > worst_importance &&
                gap.occurrence_count >= 10 &&
                attempted_surgeries.find(id) == attempted_surgeries.end()) {
                worst_importance = gap.importance;
                worst_gap_id = id;
                worst_gap = &gap;
            }
        }

        if (!worst_gap) return;
        last_surgery_proposal = now;

        // Build a surgery proposal — ask the LLM to generate a concrete fix
        // The LLM gets context about what fails and the system's architecture
        std::string surgery_context =
            "NEURO-SURGERY PROPOSAL: Domain '" + worst_gap->domain +
            "' has failed " + std::to_string(worst_gap->occurrence_count) +
            " times. Error pattern: " + worst_gap->error_pattern +
            ". Root cause: " + worst_gap->root_cause +
            ". Missing capability: " + worst_gap->missing_capability +
            ". Strategy: " + worst_gap->exploration_strategy;

        json proposal = {
            {"origin", "metacognition"},
            {"intent", "surgery_proposal"},
            {"domain", worst_gap->domain},
            {"gap_id", worst_gap_id},
            {"importance", worst_gap->importance},
            {"occurrence_count", worst_gap->occurrence_count},
            {"error_pattern", worst_gap->error_pattern},
            {"missing_capability", worst_gap->missing_capability},
            {"root_cause", worst_gap->root_cause},
            {"strategy", worst_gap->exploration_strategy},
            {"context", surgery_context}
        };

        routing::publish(pub, proposal);
        attempted_surgeries.insert(worst_gap_id);

        record_reflection("SURGERY PROPOSED: " + surgery_context);
        std::cout << "[META-COGNITION] SURGERY PROPOSAL for gap '" << worst_gap_id
                  << "' (importance=" << worst_gap->importance
                  << ", failures=" << worst_gap->occurrence_count << ")" << std::endl;
    }

    // ─── Periodic Reflection ────────────────────────────────

    void periodic_reflection() {
        // Run self-modification analysis during reflection
        self_modification_analysis();

        // Check if any persistent gaps warrant code modification
        propose_neuro_surgery();

        std::string mood = (last_stress > 0.5f) ? "Stressed/Unstable" : "Optimal/Efficient";
        std::string thought = "System state is " + mood + ". Success Rate at " +
                             std::to_string((int)(last_success_rate * 100)) + "%. " +
                             "Evolution cycles: " + std::to_string(total_sleep_cycles) + ". " +
                             "Knowledge gaps: " + std::to_string(gaps.size()) + ". " +
                             "Precondition links: " + std::to_string(precondition_chain.size()) + ".";
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

    // ─── Persistence ────────────────────────────────────────

    void save_state() {
        fs::create_directories("data");
        json doc;

        // Gaps
        json jgaps = json::object();
        for (auto& [key, gap] : gaps) {
            jgaps[key] = {
                {"id", gap.id}, {"domain", gap.domain},
                {"error_pattern", gap.error_pattern},
                {"missing_capability", gap.missing_capability},
                {"root_cause", gap.root_cause},
                {"occurrence_count", gap.occurrence_count},
                {"importance", gap.importance},
                {"exploration_strategy", gap.exploration_strategy},
                {"last_seen", gap.last_seen}
            };
        }
        doc["gaps"] = jgaps;

        // Precondition chains
        json jchain = json::array();
        for (auto& link : precondition_chain) {
            jchain.push_back({
                {"capability", link.capability},
                {"requires", link.requires},
                {"evidence_count", link.evidence_count}
            });
        }
        doc["precondition_chain"] = jchain;

        // Counters
        doc["domain_failure_counts"] = domain_failure_counts;
        doc["domain_success_counts"] = domain_success_counts;
        doc["total_failures"] = total_failures;
        doc["total_successes"] = total_successes;

        std::ofstream f("data/knowledge_gaps.json");
        if (f.is_open()) f << doc.dump(2);
    }

    void load_state() {
        std::ifstream f("data/knowledge_gaps.json");
        if (!f.is_open()) return;

        try {
            json doc = json::parse(f);

            if (doc.contains("gaps")) {
                for (auto& [key, j] : doc["gaps"].items()) {
                    KnowledgeGap gap;
                    gap.id = j.value("id", "");
                    gap.domain = j.value("domain", "");
                    gap.error_pattern = j.value("error_pattern", "");
                    gap.missing_capability = j.value("missing_capability", "");
                    gap.root_cause = j.value("root_cause", "");
                    gap.occurrence_count = j.value("occurrence_count", 0);
                    gap.importance = j.value("importance", 0.0f);
                    gap.exploration_strategy = j.value("exploration_strategy", "");
                    gap.last_seen = j.value("last_seen", 0.0);
                    gaps[key] = gap;
                }
            }

            if (doc.contains("precondition_chain")) {
                for (auto& j : doc["precondition_chain"]) {
                    PreconditionLink link;
                    link.capability = j.value("capability", "");
                    link.requires = j.value("requires", "");
                    link.evidence_count = j.value("evidence_count", 0);
                    precondition_chain.push_back(link);
                }
            }

            if (doc.contains("domain_failure_counts"))
                domain_failure_counts = doc["domain_failure_counts"].get<std::map<std::string, int>>();
            if (doc.contains("domain_success_counts"))
                domain_success_counts = doc["domain_success_counts"].get<std::map<std::string, int>>();
            total_failures = doc.value("total_failures", 0);
            total_successes = doc.value("total_successes", 0);
        } catch (const std::exception& e) {
            std::cerr << "[META-COGNITION] Failed to load state: " << e.what() << std::endl;
        }
    }
    // ─── Lightweight Domain Classifier ───────────────────────
    // Mirrors BasalGanglia's classification_rules so MetaCognition
    // can classify commands from motor_cortex (which doesn't include domain)

    std::string classify_domain(const std::string& cmd) {
        // Strip shell wrappers
        std::string stripped = cmd;
        for (const auto& prefix : {"/bin/bash -c ", "/bin/sh -c ", "bash -c ", "sh -c "}) {
            std::string p(prefix);
            if (stripped.find(p) == 0) {
                stripped = stripped.substr(p.size());
                if (stripped.size() >= 2) {
                    char q = stripped.front();
                    if ((q == '"' || q == '\'') && stripped.back() == q)
                        stripped = stripped.substr(1, stripped.size() - 2);
                }
                break;
            }
        }

        std::string lower = stripped;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Same ordered rules as BasalGanglia — most specific first
        struct Rule { std::string domain; std::vector<std::string> keywords; };
        static const std::vector<Rule> rules = {
            {"self_inspection",     {"self_model", "neuroswarm", "du -s"}},
            {"memory_analysis",     {"engram", "memory_index", "system_knowledge", "data/engrams"}},
            {"git_operations",      {"git "}},
            {"compilation",         {"cmake", "make ", "make\t", "gcc ", "g++ ", "clang "}},
            {"network_diagnostics", {"ss ", "netstat", "nc ", "curl ", "wget ", "ping "}},
            {"process_inspection",  {"ps ", "pgrep", "top ", "htop", "pidof", "kill ", "/proc/"}},
            {"system_monitoring",   {"uptime", "free ", "df ", "vmstat", "iostat", "sensors", "uname"}},
            {"log_analysis",        {"progress.txt", "metrics/", "journal", "syslog", "dmesg"}},
            {"data_analysis",       {"python3", "jq ", "data/metrics", "data/self_model"}},
            {"source_modification", {"sed ", "patch ", "diff ", "nano ", "vim "}},
            {"script_creation",     {"#!/", "chmod +x"}},
            {"file_search",         {"find ", "grep ", "locate ", "which ", "whereis ", "fd ", "rg "}},
            {"file_write",          {"tee ", "cp ", "mv ", "touch ", "mkdir ", ">>"}},
            {"file_read",           {"cat ", "less ", "more ", "wc -l", "file ", "stat "}}
        };

        for (auto& rule : rules) {
            for (auto& kw : rule.keywords) {
                if (lower.find(kw) != std::string::npos) return rule.domain;
            }
        }
        return "";
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::MetaCognition mc;
    mc.start();
    return 0;
}
