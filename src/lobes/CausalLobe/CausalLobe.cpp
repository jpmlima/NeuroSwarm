// CausalLobe — Learned Causal World Model
//
// Biological analogue: the cerebellum's forward model. Animals predict the
// sensory consequences of their actions before acting. A cat doesn't need to
// knock every glass off the table to know that pushing things causes falling.
//
// The CausalLobe observes action→effect pairs through temporal correlation:
// when an execution_result is followed by a visual_stimulus (filesystem change),
// a causal link is strengthened. Over time, the system learns which commands
// cause which observable state changes.
//
// Data flow:
//   execution_result → record action → correlate with subsequent visual_stimulus →
//   strengthen causal edge → causal_update broadcast
//
// Queries:
//   causal_query → predict effects → causal_prediction with confidence scores
//
// Persistence: data/causal_graph.json

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sstream>
#include <regex>
#include <set>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

// ─── Data Structures ────────────────────────────────────────────

struct CausalNode {
    std::string id;
    std::string type;              // "action" or "effect"
    std::string content;           // normalized command or effect description
    int observation_count = 0;
    double last_seen = 0.0;        // epoch seconds
    std::set<std::string> parents; // parent node IDs in the causal DAG
};

struct CausalEdge {
    std::string source_id;         // action node
    std::string target_id;         // effect node
    int co_occurrences = 0;
    int source_total = 0;          // total times source was observed
    int source_absent_effect_present = 0;  // counterfactual: effect without action
    int source_present_effect_absent = 0;  // counterfactual: action without effect
    double confidence = 0.0;       // P(effect | action) — observational
    double interventional = 0.0;   // P(effect | do(action)) — backdoor-adjusted
    double causal_lift = 0.0;      // interventional - base_rate(effect)
    double causal_strength = 0.0;  // Wilson lower bound on interventional
    double last_updated = 0.0;
};

struct RecentAction {
    std::string node_id;
    std::string command;
    int exit_code;
    bool success;
    double timestamp;              // epoch seconds
};

// Per-observation-window snapshot: which actions and effects were active
struct ObservationWindow {
    std::set<std::string> actions_present;
    std::set<std::string> effects_present;
    double timestamp = 0.0;
};

// ─── CausalLobe ─────────────────────────────────────────────────

class CausalLobe {
public:
    CausalLobe(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {
            "execution_result", "visual_stimulus",
            "causal_query", "primordial_ready"
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        load_state();

        std::cout << "[CAUSAL] World model online. " << nodes.size() << " nodes, "
                  << edges.size() << " edges loaded." << std::endl;
    }

    void start() {
        auto last_maintenance = std::chrono::steady_clock::now();

        while (true) {
            auto msg = routing::receive(sub, zmq::recv_flags::dontwait);
            if (!msg.is_null()) {
                try {
                    std::string intent = msg.value("intent", "");
                    std::string origin = msg.value("origin", "");

                    if (intent == "execution_result" && origin == "motor_cortex") {
                        handle_execution(msg);
                    }
                    else if (intent == "visual_stimulus" && origin == "visual_lobe") {
                        handle_visual_stimulus(msg);
                    }
                    else if (intent == "causal_query") {
                        handle_query(msg);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[CAUSAL] Error: " << e.what() << std::endl;
                }
            }

            // Periodic maintenance every 2 minutes
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_maintenance).count();
            if (elapsed >= 120) {
                maintain();
                last_maintenance = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    // ─── Graph State ────────────────────────────────────────────
    std::map<std::string, CausalNode> nodes;
    std::map<std::string, CausalEdge> edges;   // keyed by "src->tgt"
    std::map<std::string, double> effect_base_rates; // effect_id → EMA observation ratio
    int total_observation_windows = 0;

    // Confounder tracking: for each pair of actions that co-occur, track count
    std::map<std::string, int> action_cooccurrence; // "a1|a2" → count (sorted order)

    // Rolling observation windows for backdoor adjustment
    std::deque<ObservationWindow> observation_history;
    static constexpr int MAX_OBSERVATION_HISTORY = 500;

    // ─── Temporal Correlation ───────────────────────────────────
    std::deque<RecentAction> recent_actions;
    static constexpr double CORRELATION_WINDOW = 60.0;   // tighter window (was 90)
    static constexpr double DECAY_HALF_LIFE = 15.0;      // faster decay (was 30)
    static constexpr int MAX_NODES = 2000;
    static constexpr int MAX_RECENT = 50;
    static constexpr int MIN_EVIDENCE = 3;               // minimum co-occurrences for causal claim
    static constexpr double WILSON_Z = 1.96;             // 95% confidence interval

    int total_observations = 0;

    // ─── Helpers ────────────────────────────────────────────────

    double now_epoch() {
        return std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    std::string hash_id(const std::string& content) {
        // Simple hash for node identification
        size_t h = std::hash<std::string>{}(content);
        std::stringstream ss;
        ss << std::hex << h;
        return ss.str();
    }

    // ─── Command Normalization ──────────────────────────────────
    // Collapse variable parts so similar commands map to the same action node

    std::string normalize_command(const std::string& cmd) {
        std::string norm = cmd;

        // Strip leading env vars, sudo, nohup
        static const std::vector<std::string> prefixes = {
            "sudo ", "nohup ", "env ", "/bin/bash -c ", "/bin/sh -c "
        };
        for (const auto& p : prefixes) {
            if (norm.find(p) == 0) norm = norm.substr(p.size());
        }

        // Strip wrapping quotes
        if (norm.size() >= 2 && norm.front() == '\'' && norm.back() == '\'')
            norm = norm.substr(1, norm.size() - 2);
        if (norm.size() >= 2 && norm.front() == '"' && norm.back() == '"')
            norm = norm.substr(1, norm.size() - 2);

        // Replace absolute paths with <path>
        std::regex path_re("(/[a-zA-Z0-9_./-]{4,})");
        norm = std::regex_replace(norm, path_re, "<path>");

        // Replace quoted strings with <string>
        std::regex str_re("\"[^\"]+\"");
        norm = std::regex_replace(norm, str_re, "<string>");
        std::regex str_re2("'[^']+'");
        norm = std::regex_replace(norm, str_re2, "<string>");

        // Replace numbers > 2 digits with <N>
        std::regex num_re("\\b[0-9]{3,}\\b");
        norm = std::regex_replace(norm, num_re, "<N>");

        // Collapse whitespace
        std::regex ws_re("\\s+");
        norm = std::regex_replace(norm, ws_re, " ");

        // Trim
        size_t start = norm.find_first_not_of(" ");
        if (start == std::string::npos) return "";
        size_t end = norm.find_last_not_of(" ");
        return norm.substr(start, end - start + 1);
    }

    std::string extract_verb(const std::string& normalized) {
        auto sp = normalized.find(' ');
        return sp != std::string::npos ? normalized.substr(0, sp) : normalized;
    }

    // ─── Node Management ────────────────────────────────────────

    CausalNode& get_or_create_node(const std::string& type, const std::string& content) {
        std::string id = type.substr(0, 1) + "_" + hash_id(content);
        if (!nodes.count(id)) {
            CausalNode node;
            node.id = id;
            node.type = type;
            node.content = content;
            node.observation_count = 0;
            node.last_seen = now_epoch();
            nodes[id] = node;
        }
        auto& n = nodes[id];
        n.observation_count++;
        n.last_seen = now_epoch();
        return n;
    }

    void strengthen_edge(const std::string& src_id, const std::string& tgt_id, double weight = 1.0) {
        std::string key = src_id + "->" + tgt_id;
        if (!edges.count(key)) {
            CausalEdge edge;
            edge.source_id = src_id;
            edge.target_id = tgt_id;
            edges[key] = edge;
        }
        auto& e = edges[key];
        e.co_occurrences++;
        e.last_updated = now_epoch();

        // Record parent relationship in DAG
        if (nodes.count(tgt_id)) {
            nodes[tgt_id].parents.insert(src_id);
        }

        // Update observational confidence: P(Y|X)
        if (nodes.count(src_id)) {
            e.source_total = nodes[src_id].observation_count;
            e.confidence = (double)e.co_occurrences / std::max(1, e.source_total);
        }
    }

    // ─── Wilson Lower Bound ─────────────────────────────────────
    // Statistical confidence: given n observations with p success rate,
    // what's the lower bound of the confidence interval?
    static double wilson_lower(double p, int n) {
        if (n == 0) return 0.0;
        double z2 = WILSON_Z * WILSON_Z;
        double denom = 1.0 + z2 / n;
        double center = p + z2 / (2.0 * n);
        double spread = WILSON_Z * std::sqrt((p * (1.0 - p) + z2 / (4.0 * n)) / n);
        return std::max(0.0, (center - spread) / denom);
    }

    // ─── Do-Calculus: P(Y | do(X)) via Backdoor Adjustment ─────
    //
    // Pearl's backdoor criterion: if Z blocks all backdoor paths from X to Y,
    // then P(Y|do(X)) = Σ_z P(Y|X,Z=z) P(Z=z)
    //
    // We identify confounders as actions that co-occur with X and also
    // have edges to Y. These form the adjustment set Z.

    std::set<std::string> find_confounders(const std::string& action_id,
                                            const std::string& effect_id) {
        std::set<std::string> confounders;

        // A confounder Z for X→Y must:
        // 1. Co-occur with X (temporally correlated actions)
        // 2. Have its own edge to Y (Z→Y exists)
        // 3. Not be a descendant of X (no post-treatment bias)

        for (auto& [pair_key, count] : action_cooccurrence) {
            if (count < MIN_EVIDENCE) continue;

            // Parse the pair
            size_t sep = pair_key.find('|');
            if (sep == std::string::npos) continue;
            std::string a1 = pair_key.substr(0, sep);
            std::string a2 = pair_key.substr(sep + 1);

            std::string other;
            if (a1 == action_id) other = a2;
            else if (a2 == action_id) other = a1;
            else continue;

            // Check if 'other' also has an edge to the same effect
            std::string other_edge_key = other + "->" + effect_id;
            if (edges.count(other_edge_key) && edges[other_edge_key].co_occurrences >= MIN_EVIDENCE) {
                // Verify 'other' is not a descendant of action_id (avoid post-treatment)
                if (!is_descendant(other, action_id)) {
                    confounders.insert(other);
                }
            }
        }
        return confounders;
    }

    // BFS: is 'candidate' a descendant of 'ancestor' in the causal DAG?
    bool is_descendant(const std::string& candidate, const std::string& ancestor) {
        std::set<std::string> visited;
        std::deque<std::string> queue;
        queue.push_back(ancestor);

        while (!queue.empty()) {
            std::string current = queue.front();
            queue.pop_front();
            if (visited.count(current)) continue;
            visited.insert(current);

            // Find all children of 'current' (edges where current is source)
            for (auto& [key, edge] : edges) {
                if (edge.source_id == current) {
                    if (edge.target_id == candidate) return true;
                    if (nodes.count(edge.target_id) && nodes[edge.target_id].type == "action") {
                        queue.push_back(edge.target_id);
                    }
                }
            }
        }
        return false;
    }

    // Compute P(Y|do(X)) using observation windows + backdoor adjustment
    double compute_do_probability(const std::string& action_id,
                                   const std::string& effect_id,
                                   const std::set<std::string>& confounders) {
        if (observation_history.empty()) return 0.0;

        // If no confounders, interventional = observational
        if (confounders.empty()) {
            std::string key = action_id + "->" + effect_id;
            return edges.count(key) ? edges[key].confidence : 0.0;
        }

        // Backdoor adjustment: P(Y|do(X)) = Σ_z P(Y|X, Z=z) P(Z=z)
        // We stratify by confounder presence/absence patterns

        // Build confounder strata from observation history
        // Each stratum is a binary pattern of confounder presence
        struct Stratum {
            int x_and_y = 0;    // action+effect present
            int x_no_y = 0;     // action present, effect absent
            int total = 0;      // total windows in this stratum
        };
        std::map<std::string, Stratum> strata; // pattern → counts

        for (const auto& window : observation_history) {
            // Build confounder pattern for this window
            std::string pattern;
            for (const auto& z : confounders) {
                pattern += window.actions_present.count(z) ? "1" : "0";
            }

            auto& s = strata[pattern];
            s.total++;
            bool x_present = window.actions_present.count(action_id) > 0;
            bool y_present = window.effects_present.count(effect_id) > 0;

            if (x_present && y_present) s.x_and_y++;
            else if (x_present && !y_present) s.x_no_y++;
        }

        // Compute weighted average: Σ_z P(Y|X,Z=z) * P(Z=z)
        double p_do = 0.0;
        int total_windows = (int)observation_history.size();

        for (auto& [pattern, s] : strata) {
            int x_in_stratum = s.x_and_y + s.x_no_y;
            if (x_in_stratum < 2) continue;  // insufficient data in stratum

            double p_y_given_x_z = (double)s.x_and_y / x_in_stratum;
            double p_z = (double)s.total / total_windows;
            p_do += p_y_given_x_z * p_z;
        }

        return std::min(1.0, std::max(0.0, p_do));
    }

    // ─── Full Causal Recompute ───────────────────────────────────
    // Called during maintenance: update all edges with interventional scores

    void update_causal_scores() {
        if (total_observation_windows < 1) return;

        // Update base rates symmetrically using observation windows
        // Count how many recent windows each effect appeared in
        if (!observation_history.empty()) {
            std::map<std::string, int> effect_window_counts;
            for (const auto& w : observation_history) {
                for (const auto& eid : w.effects_present) {
                    effect_window_counts[eid]++;
                }
            }
            int n_windows = (int)observation_history.size();
            for (auto& [eid, count] : effect_window_counts) {
                effect_base_rates[eid] = (double)count / n_windows;
            }
            // Decay base rates for effects NOT seen in recent windows
            for (auto it = effect_base_rates.begin(); it != effect_base_rates.end(); ) {
                if (!effect_window_counts.count(it->first)) {
                    it->second *= 0.95;  // gradual decay
                    if (it->second < 0.001) { it = effect_base_rates.erase(it); continue; }
                }
                ++it;
            }
        }

        // Compute interventional scores for all edges
        for (auto& [key, edge] : edges) {
            // Find confounders for this X→Y edge
            auto confounders = find_confounders(edge.source_id, edge.target_id);

            // Compute P(Y|do(X)) via backdoor adjustment
            edge.interventional = compute_do_probability(
                edge.source_id, edge.target_id, confounders);

            // If no observation history yet, fall back to observational
            if (observation_history.empty()) {
                edge.interventional = edge.confidence;
            }

            // Causal lift: interventional - base_rate
            double base_rate = 0.0;
            if (effect_base_rates.count(edge.target_id)) {
                base_rate = effect_base_rates[edge.target_id];
            }
            edge.causal_lift = edge.interventional - base_rate;

            // Wilson lower bound on interventional confidence
            edge.causal_strength = (edge.co_occurrences >= MIN_EVIDENCE)
                ? wilson_lower(edge.interventional, edge.co_occurrences)
                : 0.0;

            // Track counterfactual counts from observation history
            int absent_action_present_effect = 0;
            int present_action_absent_effect = 0;
            for (const auto& w : observation_history) {
                bool x = w.actions_present.count(edge.source_id) > 0;
                bool y = w.effects_present.count(edge.target_id) > 0;
                if (!x && y) absent_action_present_effect++;
                if (x && !y) present_action_absent_effect++;
            }
            edge.source_absent_effect_present = absent_action_present_effect;
            edge.source_present_effect_absent = present_action_absent_effect;
        }
    }

    // ─── Event Handlers ─────────────────────────────────────────

    void handle_execution(const json& msg) {
        std::string mode = msg.value("mode", "reality");
        if (mode == "dream") return;  // Only learn from reality

        std::string cmd = msg.value("command", "");
        if (cmd.empty()) return;

        int exit_code = msg.value("exit_code", -1);
        bool success = msg.value("status", "") == "success";

        std::string normalized = normalize_command(cmd);
        if (normalized.empty()) return;

        // Create action node
        auto& action = get_or_create_node("action", normalized);

        // Immediate effects: exit code and success/failure
        auto& exit_effect = get_or_create_node("effect", "exit:" + std::to_string(exit_code));
        strengthen_edge(action.id, exit_effect.id);

        std::string status_str = success ? "success" : "failure";
        auto& status_effect = get_or_create_node("effect", "status:" + status_str);
        strengthen_edge(action.id, status_effect.id);

        // Output-based effects: extract key patterns from output
        std::string output = msg.value("proprioception", "");
        if (!output.empty() && output.size() < 2000) {
            auto output_effects = extract_output_effects(output);
            for (const auto& eff : output_effects) {
                auto& eff_node = get_or_create_node("effect", "output:" + eff);
                strengthen_edge(action.id, eff_node.id);
            }
        }

        // Record in sliding window for filesystem correlation
        RecentAction ra;
        ra.node_id = action.id;
        ra.command = normalized;
        ra.exit_code = exit_code;
        ra.success = success;
        ra.timestamp = now_epoch();
        recent_actions.push_back(ra);

        // Prune old actions
        double cutoff = now_epoch() - CORRELATION_WINDOW;
        while (!recent_actions.empty() && recent_actions.front().timestamp < cutoff) {
            recent_actions.pop_front();
        }
        if (recent_actions.size() > MAX_RECENT) {
            recent_actions.pop_front();
        }

        total_observations++;
    }

    std::vector<std::string> extract_output_effects(const std::string& output) {
        std::vector<std::string> effects;

        // Detect error patterns
        if (output.find("No such file") != std::string::npos) effects.push_back("error:no_such_file");
        if (output.find("Permission denied") != std::string::npos) effects.push_back("error:permission_denied");
        if (output.find("command not found") != std::string::npos) effects.push_back("error:command_not_found");
        if (output.find("Segmentation fault") != std::string::npos) effects.push_back("error:segfault");
        if (output.find("error:") != std::string::npos || output.find("Error:") != std::string::npos)
            effects.push_back("error:generic");

        // Detect success patterns
        if (output.find("Built target") != std::string::npos) effects.push_back("built_target");
        if (output.find("Compiling") != std::string::npos || output.find("Linking") != std::string::npos)
            effects.push_back("compilation");
        if (output.find("100%") != std::string::npos) effects.push_back("complete");

        // Detect output type
        int lines = std::count(output.begin(), output.end(), '\n');
        if (lines == 0 && output.size() < 80) effects.push_back("single_line_output");
        else if (lines > 20) effects.push_back("multi_line_output");

        return effects;
    }

    void handle_visual_stimulus(const json& msg) {
        std::string text = msg.value("text", "");
        if (text.empty() || recent_actions.empty()) return;

        total_observation_windows++;
        double now = now_epoch();

        // Parse individual file changes from visual stimulus
        auto changes = parse_file_changes(text);
        if (changes.empty()) return;

        // Build observation window snapshot for backdoor adjustment
        ObservationWindow window;
        window.timestamp = now;
        for (const auto& ra : recent_actions) {
            if (now - ra.timestamp <= CORRELATION_WINDOW) {
                window.actions_present.insert(ra.node_id);
            }
        }

        // Track action co-occurrence for confounder detection
        std::vector<std::string> present_actions(window.actions_present.begin(),
                                                  window.actions_present.end());
        for (size_t i = 0; i < present_actions.size(); ++i) {
            for (size_t j = i + 1; j < present_actions.size(); ++j) {
                // Sorted pair key for canonical ordering
                std::string a = present_actions[i], b = present_actions[j];
                if (a > b) std::swap(a, b);
                action_cooccurrence[a + "|" + b]++;
            }
        }

        for (const auto& change : changes) {
            auto& effect = get_or_create_node("effect", change);
            window.effects_present.insert(effect.id);

            // Correlate with recent actions using continuous decay (no binary threshold)
            for (const auto& action : recent_actions) {
                double dt = now - action.timestamp;
                if (dt > CORRELATION_WINDOW || dt < 0) continue;

                double weight = std::exp(-dt / DECAY_HALF_LIFE);
                if (weight < 0.05) continue;

                // Continuous weighting: the closer in time, the stronger the signal
                strengthen_edge(action.node_id, effect.id, weight);
            }
        }

        // Store observation window for backdoor adjustment computations
        observation_history.push_back(window);
        while (observation_history.size() > MAX_OBSERVATION_HISTORY) {
            observation_history.pop_front();
        }
    }

    std::vector<std::string> parse_file_changes(const std::string& text) {
        std::vector<std::string> changes;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            // Skip empty lines
            if (line.empty()) continue;

            // Parse "Modified: filename" or "New Stimulus: filename" patterns
            size_t colon = line.find(": ");
            if (colon != std::string::npos) {
                std::string type = line.substr(0, colon);
                std::string file = line.substr(colon + 2);

                // Normalize: strip path, keep filename
                size_t slash = file.rfind('/');
                if (slash != std::string::npos) file = file.substr(slash + 1);

                // Trim whitespace
                size_t s = file.find_first_not_of(" \t\n\r");
                size_t e = file.find_last_not_of(" \t\n\r");
                if (s != std::string::npos) file = file.substr(s, e - s + 1);

                if (!file.empty() && file.size() < 100) {
                    changes.push_back("file:" + file);
                }
            }
        }
        return changes;
    }

    // ─── Query Handler ──────────────────────────────────────────

    void handle_query(const json& msg) {
        std::string cid = msg.value("cid", "");
        std::string command = msg.value("command", "");
        if (command.empty()) return;

        std::string normalized = normalize_command(command);
        std::string action_id = "a_" + hash_id(normalized);

        json predicted_effects = json::array();
        double predicted_success = 0.5;  // default: uncertain
        double interventional_success = 0.5;
        int observations = 0;
        int confounders_detected = 0;

        if (nodes.count(action_id)) {
            observations = nodes[action_id].observation_count;

            // Collect all outgoing edges from this action
            struct EffectEntry { double sort_key; json data; };
            std::vector<EffectEntry> effects;

            for (auto& [key, edge] : edges) {
                if (edge.source_id != action_id) continue;
                if (!nodes.count(edge.target_id)) continue;

                auto& tgt = nodes[edge.target_id];
                auto confounders = find_confounders(action_id, tgt.id);

                json eff = {
                    {"effect", tgt.content},
                    {"p_observational", std::round(edge.confidence * 100) / 100.0},
                    {"p_interventional", std::round(edge.interventional * 100) / 100.0},
                    {"causal_strength", std::round(edge.causal_strength * 100) / 100.0},
                    {"causal_lift", std::round(edge.causal_lift * 100) / 100.0},
                    {"observations", edge.co_occurrences},
                    {"confounders", (int)confounders.size()}
                };

                // Use causal_strength (Wilson lower bound on interventional) for sorting
                double sort_key = edge.causal_strength;
                effects.push_back({sort_key, eff});
                confounders_detected += (int)confounders.size();

                // Extract success rate — prefer interventional over observational
                if (tgt.content == "status:success") {
                    predicted_success = edge.confidence;
                    interventional_success = edge.interventional;
                }
            }

            // Sort by causal_strength descending (statistically robust causal signal)
            std::sort(effects.begin(), effects.end(),
                      [](auto& a, auto& b) { return a.sort_key > b.sort_key; });

            for (size_t i = 0; i < effects.size() && i < 10; ++i) {
                predicted_effects.push_back(effects[i].data);
            }
        } else {
            // No exact match — try to find similar actions by verb
            std::string verb = extract_verb(normalized);
            for (auto& [id, node] : nodes) {
                if (node.type != "action") continue;
                if (extract_verb(node.content) == verb) {
                    for (auto& [key, edge] : edges) {
                        if (edge.source_id != id || !nodes.count(edge.target_id)) continue;
                        if (edge.causal_strength < 0.1 && edge.confidence < 0.2) continue;

                        auto& tgt = nodes[edge.target_id];
                        predicted_effects.push_back({
                            {"effect", tgt.content},
                            {"p_observational", std::round(edge.confidence * 50) / 100.0},
                            {"p_interventional", std::round(edge.interventional * 50) / 100.0},
                            {"approximate", true}
                        });

                        if (tgt.content == "status:success") {
                            predicted_success = edge.confidence;
                            interventional_success = edge.interventional;
                        }
                    }
                    observations = node.observation_count;
                    break;
                }
            }
        }

        json response = {
            {"origin", "causal_lobe"},
            {"intent", "causal_prediction"},
            {"cid", cid},
            {"command", command},
            {"normalized", normalized},
            {"predicted_effects", predicted_effects},
            {"predicted_success_rate", std::round(predicted_success * 100) / 100.0},
            {"interventional_success_rate", std::round(interventional_success * 100) / 100.0},
            {"observations", observations},
            {"confounders_detected", confounders_detected}
        };
        dispatch(response);
    }

    // ─── Maintenance ────────────────────────────────────────────

    void maintain() {
        update_causal_scores();
        prune();
        prune_cooccurrence();
        save_state();
        broadcast_update();

        // Count edges with genuine causal signal
        int causal_edges = 0;
        for (auto& [key, edge] : edges) {
            if (edge.causal_strength > 0.0 && edge.co_occurrences >= MIN_EVIDENCE) causal_edges++;
        }

        std::cout << "[CAUSAL] Maintenance: " << nodes.size() << " nodes, "
                  << edges.size() << " edges (" << causal_edges << " causal), "
                  << total_observations << " obs, "
                  << observation_history.size() << " windows"
                  << std::endl;
    }

    void prune() {
        // Remove weak edges: single-observation edges older than 1 hour,
        // or edges with zero causal strength and minimal evidence
        double now = now_epoch();
        std::vector<std::string> dead_edges;
        for (auto& [key, edge] : edges) {
            bool stale_single = (edge.co_occurrences < 2 && (now - edge.last_updated) > 3600);
            bool no_signal = (edge.co_occurrences >= MIN_EVIDENCE &&
                              edge.causal_strength < 0.01 && edge.causal_lift < 0.01);
            if (stale_single || no_signal) {
                dead_edges.push_back(key);
            }
        }
        for (const auto& key : dead_edges) edges.erase(key);

        // Remove orphan nodes (no edges)
        std::set<std::string> connected;
        for (auto& [key, edge] : edges) {
            connected.insert(edge.source_id);
            connected.insert(edge.target_id);
        }
        std::vector<std::string> orphans;
        for (auto& [id, node] : nodes) {
            if (!connected.count(id) && node.observation_count < 3) {
                orphans.push_back(id);
            }
        }
        for (const auto& id : orphans) nodes.erase(id);

        // Cap nodes at MAX_NODES (LRU)
        if (nodes.size() > MAX_NODES) {
            std::vector<std::pair<std::string, double>> by_time;
            for (auto& [id, node] : nodes) by_time.push_back({id, node.last_seen});
            std::sort(by_time.begin(), by_time.end(),
                      [](auto& a, auto& b) { return a.second < b.second; });

            size_t to_remove = nodes.size() - MAX_NODES + MAX_NODES / 10;
            for (size_t i = 0; i < to_remove; ++i) {
                std::string nid = by_time[i].first;
                // Remove associated edges
                std::vector<std::string> dead;
                for (auto& [key, edge] : edges) {
                    if (edge.source_id == nid || edge.target_id == nid) dead.push_back(key);
                }
                for (auto& k : dead) edges.erase(k);
                nodes.erase(nid);
            }
        }
    }

    // Prune stale action co-occurrence entries
    void prune_cooccurrence() {
        // Remove pairs where both actions no longer exist
        std::vector<std::string> dead;
        for (auto& [key, count] : action_cooccurrence) {
            size_t sep = key.find('|');
            if (sep == std::string::npos) { dead.push_back(key); continue; }
            std::string a1 = key.substr(0, sep);
            std::string a2 = key.substr(sep + 1);
            if (!nodes.count(a1) || !nodes.count(a2)) dead.push_back(key);
        }
        for (auto& k : dead) action_cooccurrence.erase(k);
    }

    void broadcast_update() {
        // Collect top causal relationships by causal_strength (Wilson-adjusted interventional)
        std::vector<std::pair<double, json>> top_links;
        int causal_count = 0;
        for (auto& [key, edge] : edges) {
            if (!nodes.count(edge.source_id) || !nodes.count(edge.target_id)) continue;
            if (edge.co_occurrences < MIN_EVIDENCE) continue;

            auto confounders = find_confounders(edge.source_id, edge.target_id);

            json link = {
                {"action", nodes[edge.source_id].content},
                {"effect", nodes[edge.target_id].content},
                {"p_obs", std::round(edge.confidence * 100) / 100.0},
                {"p_do", std::round(edge.interventional * 100) / 100.0},
                {"strength", std::round(edge.causal_strength * 100) / 100.0},
                {"lift", std::round(edge.causal_lift * 100) / 100.0},
                {"n", edge.co_occurrences},
                {"confounders", (int)confounders.size()}
            };
            top_links.push_back({edge.causal_strength, link});
            if (edge.causal_strength > 0.0) causal_count++;
        }

        std::sort(top_links.begin(), top_links.end(),
                  [](auto& a, auto& b) { return a.first > b.first; });

        json links = json::array();
        for (size_t i = 0; i < top_links.size() && i < 20; ++i) {
            links.push_back(top_links[i].second);
        }

        // Count node types
        int action_count = 0, effect_count = 0;
        for (auto& [id, node] : nodes) {
            if (node.type == "action") action_count++;
            else effect_count++;
        }

        json update = {
            {"origin", "causal_lobe"},
            {"intent", "causal_update"},
            {"action_nodes", action_count},
            {"effect_nodes", effect_count},
            {"total_edges", (int)edges.size()},
            {"causal_edges", causal_count},
            {"total_observations", total_observations},
            {"observation_windows", (int)observation_history.size()},
            {"confounder_pairs", (int)action_cooccurrence.size()},
            {"top_causal_links", links}
        };
        dispatch(update);
    }

    // ─── Persistence ────────────────────────────────────────────

    void save_state() {
        fs::create_directories("data");

        json doc;

        // Nodes
        json jnodes = json::object();
        for (auto& [id, node] : nodes) {
            json jparents = json::array();
            for (const auto& p : node.parents) jparents.push_back(p);
            jnodes[id] = {
                {"type", node.type},
                {"content", node.content},
                {"observation_count", node.observation_count},
                {"last_seen", node.last_seen},
                {"parents", jparents}
            };
        }
        doc["nodes"] = jnodes;

        // Edges
        json jedges = json::object();
        for (auto& [key, edge] : edges) {
            jedges[key] = {
                {"source_id", edge.source_id},
                {"target_id", edge.target_id},
                {"co_occurrences", edge.co_occurrences},
                {"source_total", edge.source_total},
                {"source_absent_effect_present", edge.source_absent_effect_present},
                {"source_present_effect_absent", edge.source_present_effect_absent},
                {"confidence", edge.confidence},
                {"interventional", edge.interventional},
                {"causal_lift", edge.causal_lift},
                {"causal_strength", edge.causal_strength},
                {"last_updated", edge.last_updated}
            };
        }
        doc["edges"] = jedges;

        // Base rates & co-occurrence
        doc["effect_base_rates"] = effect_base_rates;
        doc["action_cooccurrence"] = action_cooccurrence;
        doc["metadata"] = {
            {"total_observations", total_observations},
            {"total_observation_windows", total_observation_windows},
            {"last_save", now_epoch()}
        };

        // Write atomically via temp file
        fs::path tmp("data/causal_graph.json.tmp");
        std::ofstream f(tmp);
        if (f.is_open()) {
            f << doc.dump(2);
            f.close();
            fs::rename(tmp, "data/causal_graph.json");
        }

        std::cout << "[CAUSAL] State saved: " << nodes.size() << " nodes, "
                  << edges.size() << " edges" << std::endl;
    }

    void load_state() {
        std::ifstream f("data/causal_graph.json");
        if (!f.is_open()) return;

        try {
            json doc = json::parse(f);

            // Load nodes
            if (doc.contains("nodes")) {
                for (auto& [id, jn] : doc["nodes"].items()) {
                    CausalNode node;
                    node.id = id;
                    node.type = jn.value("type", "");
                    node.content = jn.value("content", "");
                    node.observation_count = jn.value("observation_count", 0);
                    node.last_seen = jn.value("last_seen", 0.0);
                    // Load parent set (new field, graceful fallback)
                    if (jn.contains("parents")) {
                        for (const auto& p : jn["parents"]) {
                            node.parents.insert(p.get<std::string>());
                        }
                    }
                    nodes[id] = node;
                }
            }

            // Load edges
            if (doc.contains("edges")) {
                for (auto& [key, je] : doc["edges"].items()) {
                    CausalEdge edge;
                    edge.source_id = je.value("source_id", "");
                    edge.target_id = je.value("target_id", "");
                    edge.co_occurrences = je.value("co_occurrences", 0);
                    edge.source_total = je.value("source_total", 0);
                    edge.source_absent_effect_present = je.value("source_absent_effect_present", 0);
                    edge.source_present_effect_absent = je.value("source_present_effect_absent", 0);
                    edge.confidence = je.value("confidence", 0.0);
                    edge.interventional = je.value("interventional", 0.0);
                    edge.causal_lift = je.value("causal_lift", 0.0);
                    edge.causal_strength = je.value("causal_strength", 0.0);
                    edge.last_updated = je.value("last_updated", 0.0);
                    edges[key] = edge;
                }
            }

            // Load base rates
            if (doc.contains("effect_base_rates")) {
                effect_base_rates = doc["effect_base_rates"].get<std::map<std::string, double>>();
            }

            // Load action co-occurrence (new field, graceful fallback)
            if (doc.contains("action_cooccurrence")) {
                action_cooccurrence = doc["action_cooccurrence"].get<std::map<std::string, int>>();
            }

            if (doc.contains("metadata")) {
                total_observations = doc["metadata"].value("total_observations", 0);
                total_observation_windows = doc["metadata"].value("total_observation_windows", 0);
            }
        } catch (const std::exception& e) {
            std::cerr << "[CAUSAL] Failed to load state: " << e.what() << std::endl;
        }
    }

    void dispatch(const json& msg) {
        routing::publish(pub, msg);
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[++i];
    }

    neuroswarm::CausalLobe lobe(ip);
    lobe.start();
    return 0;
}
