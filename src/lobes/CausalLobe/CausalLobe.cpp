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
};

struct CausalEdge {
    std::string source_id;         // action node
    std::string target_id;         // effect node
    int co_occurrences = 0;
    int source_total = 0;          // total times source was observed
    double confidence = 0.0;       // co_occurrences / source_total
    double causal_lift = 0.0;      // confidence - base_rate(effect)
    double last_updated = 0.0;
};

struct RecentAction {
    std::string node_id;
    std::string command;
    int exit_code;
    bool success;
    double timestamp;              // epoch seconds
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
    std::map<std::string, double> effect_base_rates; // effect_id -> observation ratio
    int total_observation_windows = 0;

    // ─── Temporal Correlation ───────────────────────────────────
    std::deque<RecentAction> recent_actions;
    static constexpr double CORRELATION_WINDOW = 90.0;  // seconds
    static constexpr double DECAY_HALF_LIFE = 30.0;     // seconds for exponential decay
    static constexpr int MAX_NODES = 2000;
    static constexpr int MAX_RECENT = 50;

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
        e.co_occurrences += (int)weight;
        e.last_updated = now_epoch();

        // Update confidence from source total
        if (nodes.count(src_id)) {
            e.source_total = nodes[src_id].observation_count;
            e.confidence = (double)e.co_occurrences / std::max(1, e.source_total);
        }
    }

    void update_causal_lifts() {
        // Compute base rates: P(effect) = times_effect_seen / total_windows
        if (total_observation_windows < 1) return;
        for (auto& [key, edge] : edges) {
            double base_rate = 0.0;
            if (effect_base_rates.count(edge.target_id)) {
                base_rate = effect_base_rates[edge.target_id];
            }
            edge.causal_lift = edge.confidence - base_rate;
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

        for (const auto& change : changes) {
            auto& effect = get_or_create_node("effect", change);

            // Update base rate for this effect
            effect_base_rates[effect.id] =
                (effect_base_rates[effect.id] * (total_observation_windows - 1) + 1.0)
                / total_observation_windows;

            // Correlate with all recent actions (exponential decay by time distance)
            for (const auto& action : recent_actions) {
                double dt = now - action.timestamp;
                if (dt > CORRELATION_WINDOW) continue;

                double weight = std::exp(-dt / DECAY_HALF_LIFE);
                if (weight < 0.05) continue;  // negligible

                strengthen_edge(action.node_id, effect.id, weight > 0.5 ? 1.0 : 0.0);
            }
        }

        // Also update base rates for effects NOT seen (they remain at previous rate)
        // This happens naturally since we only increment on observation.
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
        int observations = 0;

        if (nodes.count(action_id)) {
            observations = nodes[action_id].observation_count;

            // Collect all outgoing edges from this action
            std::vector<std::pair<double, json>> effects;
            for (auto& [key, edge] : edges) {
                if (edge.source_id != action_id) continue;
                if (!nodes.count(edge.target_id)) continue;

                auto& tgt = nodes[edge.target_id];
                json eff = {
                    {"effect", tgt.content},
                    {"confidence", std::round(edge.confidence * 100) / 100.0},
                    {"causal_lift", std::round(edge.causal_lift * 100) / 100.0},
                    {"observations", edge.co_occurrences}
                };
                effects.push_back({edge.confidence, eff});

                // Extract success rate from status edges
                if (tgt.content == "status:success") {
                    predicted_success = edge.confidence;
                }
            }

            // Sort by confidence descending
            std::sort(effects.begin(), effects.end(),
                      [](auto& a, auto& b) { return a.first > b.first; });

            for (size_t i = 0; i < effects.size() && i < 10; ++i) {
                predicted_effects.push_back(effects[i].second);
            }
        } else {
            // No exact match — try to find similar actions by verb
            std::string verb = extract_verb(normalized);
            for (auto& [id, node] : nodes) {
                if (node.type != "action") continue;
                if (extract_verb(node.content) == verb) {
                    // Found a verb match — use its edges as approximate prediction
                    for (auto& [key, edge] : edges) {
                        if (edge.source_id != id || !nodes.count(edge.target_id)) continue;
                        if (edge.confidence < 0.2) continue;

                        auto& tgt = nodes[edge.target_id];
                        predicted_effects.push_back({
                            {"effect", tgt.content},
                            {"confidence", std::round(edge.confidence * 50) / 100.0},  // halved for approximation
                            {"approximate", true}
                        });

                        if (tgt.content == "status:success") {
                            predicted_success = edge.confidence;
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
            {"observations", observations}
        };
        dispatch(response);
    }

    // ─── Maintenance ────────────────────────────────────────────

    void maintain() {
        update_causal_lifts();
        prune();
        save_state();
        broadcast_update();

        std::cout << "[CAUSAL] Maintenance: " << nodes.size() << " nodes, "
                  << edges.size() << " edges, " << total_observations << " observations"
                  << std::endl;
    }

    void prune() {
        // Remove weak edges (noise)
        std::vector<std::string> dead_edges;
        for (auto& [key, edge] : edges) {
            if (edge.co_occurrences < 2 && edge.confidence < 0.05) {
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

    void broadcast_update() {
        // Collect top causal relationships by lift
        std::vector<std::pair<double, json>> top_links;
        for (auto& [key, edge] : edges) {
            if (!nodes.count(edge.source_id) || !nodes.count(edge.target_id)) continue;
            if (edge.co_occurrences < 2) continue;

            json link = {
                {"action", nodes[edge.source_id].content},
                {"effect", nodes[edge.target_id].content},
                {"confidence", std::round(edge.confidence * 100) / 100.0},
                {"causal_lift", std::round(edge.causal_lift * 100) / 100.0},
                {"observations", edge.co_occurrences}
            };
            top_links.push_back({edge.causal_lift, link});
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
            {"total_observations", total_observations},
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
            jnodes[id] = {
                {"type", node.type},
                {"content", node.content},
                {"observation_count", node.observation_count},
                {"last_seen", node.last_seen}
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
                {"confidence", edge.confidence},
                {"causal_lift", edge.causal_lift},
                {"last_updated", edge.last_updated}
            };
        }
        doc["edges"] = jedges;

        // Base rates
        doc["effect_base_rates"] = effect_base_rates;
        doc["metadata"] = {
            {"total_observations", total_observations},
            {"total_observation_windows", total_observation_windows},
            {"last_save", now_epoch()}
        };

        std::ofstream f("data/causal_graph.json");
        if (f.is_open()) f << doc.dump(2);

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
                    edge.confidence = je.value("confidence", 0.0);
                    edge.causal_lift = je.value("causal_lift", 0.0);
                    edge.last_updated = je.value("last_updated", 0.0);
                    edges[key] = edge;
                }
            }

            // Load base rates
            if (doc.contains("effect_base_rates")) {
                effect_base_rates = doc["effect_base_rates"].get<std::map<std::string, double>>();
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
