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
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <cstdlib>
#include <memory>
#include <numeric>

using json = nlohmann::json;

namespace neuroswarm {

// ──────────────────────────────────────────────────────────────────────
// BK-tree: metric tree for fuzzy string matching via Levenshtein distance
// Used for: command validation, genome dedup, fuzzy retrieval
// ──────────────────────────────────────────────────────────────────────

class BKTree {
public:
    // Levenshtein distance — O(n*m) but commands are short strings
    static int levenshtein(const std::string& a, const std::string& b) {
        int n = a.size(), m = b.size();
        if (n == 0) return m;
        if (m == 0) return n;

        // Optimisation: skip common prefix/suffix
        int prefix = 0;
        while (prefix < n && prefix < m && a[prefix] == b[prefix]) prefix++;
        int suffix = 0;
        while (suffix < (n - prefix) && suffix < (m - prefix) &&
               a[n - 1 - suffix] == b[m - 1 - suffix]) suffix++;

        int an = n - prefix - suffix;
        int bm = m - prefix - suffix;
        if (an == 0) return bm;
        if (bm == 0) return an;

        // Single-row DP
        std::vector<int> row(bm + 1);
        std::iota(row.begin(), row.end(), 0);

        for (int i = 1; i <= an; i++) {
            int prev = row[0];
            row[0] = i;
            for (int j = 1; j <= bm; j++) {
                int cost = (a[prefix + i - 1] == b[prefix + j - 1]) ? 0 : 1;
                int temp = row[j];
                row[j] = std::min({row[j] + 1, row[j - 1] + 1, prev + cost});
                prev = temp;
            }
        }
        return row[bm];
    }

    struct Entry {
        std::string cmd;
        float fitness;
        std::string domain;
    };

    void insert(const std::string& cmd, float fitness = 0.5f, const std::string& domain = "") {
        if (cmd.empty()) return;
        if (!root) {
            root = std::make_unique<Node>(Entry{cmd, fitness, domain});
            size_++;
            return;
        }
        Node* cur = root.get();
        while (true) {
            int d = levenshtein(cur->entry.cmd, cmd);
            if (d == 0) {
                // Exact match — update fitness if higher
                if (fitness > cur->entry.fitness) cur->entry.fitness = fitness;
                return;
            }
            auto it = cur->children.find(d);
            if (it == cur->children.end()) {
                cur->children[d] = std::make_unique<Node>(Entry{cmd, fitness, domain});
                size_++;
                return;
            }
            cur = it->second.get();
        }
    }

    // Find all entries within max_dist of query
    std::vector<std::pair<Entry, int>> find_within(const std::string& query, int max_dist) const {
        std::vector<std::pair<Entry, int>> results;
        if (!root) return results;
        search(root.get(), query, max_dist, results);
        return results;
    }

    // Find the k nearest entries to query
    std::vector<std::pair<Entry, int>> nearest(const std::string& query, int k) const {
        // Iterative widening: start narrow, expand until we have k results
        for (int radius = 2; radius <= 50; radius += 3) {
            auto results = find_within(query, radius);
            if ((int)results.size() >= k) {
                std::sort(results.begin(), results.end(),
                    [](const auto& a, const auto& b) { return a.second < b.second; });
                results.resize(k);
                return results;
            }
        }
        auto results = find_within(query, 50);
        std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        if ((int)results.size() > k) results.resize(k);
        return results;
    }

    // Minimum distance to any entry in the tree
    int min_distance(const std::string& query) const {
        if (!root) return 9999;
        int best = 9999;
        min_dist_search(root.get(), query, best);
        return best;
    }

    int size() const { return size_; }

    void clear() {
        root.reset();
        size_ = 0;
    }

private:
    struct Node {
        Entry entry;
        std::map<int, std::unique_ptr<Node>> children;
        Node(Entry e) : entry(std::move(e)) {}
    };

    std::unique_ptr<Node> root;
    int size_ = 0;

    void search(const Node* node, const std::string& query, int max_dist,
                std::vector<std::pair<Entry, int>>& results) const {
        int d = levenshtein(node->entry.cmd, query);
        if (d <= max_dist) {
            results.push_back({node->entry, d});
        }
        // BK-tree pruning: only visit children with keys in [d-max_dist, d+max_dist]
        int lo = d - max_dist;
        int hi = d + max_dist;
        for (auto& [key, child] : node->children) {
            if (key >= lo && key <= hi) {
                search(child.get(), query, max_dist, results);
            }
        }
    }

    void min_dist_search(const Node* node, const std::string& query, int& best) const {
        int d = levenshtein(node->entry.cmd, query);
        if (d < best) best = d;
        if (best == 0) return;  // can't do better
        for (auto& [key, child] : node->children) {
            if (key >= d - best && key <= d + best) {
                min_dist_search(child.get(), query, best);
            }
        }
    }
};

// ──────────────────────────────────────────────────────────────────────

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
            "lobe_crash", "lobe_death", "metabolic_alert",
            "genesis_result", "specialist_report", "lobe_injected",
            "lobe_terminated", "domain_resolve_result",
            "rlaif_reinforce",
            "concept_update", "concept_response",
            "validate_command",
            "exploration_target",
            "emergent_goal",
            "stagnation_alert",
            "system_tuning"
        });

        mkdir("./data", 0755);
        load_self_model();
        init_domain_commands();
        load_meta_templates();
        load_active_specialists();

        std::cout << "[BASAL_GANGLIA] Intrinsic motivation engine online. "
                  << domains.size() << " capability domains tracked."
                  << " Active specialists: " << active_specialists.size()
                  << " BK-tree: " << bk_tree.size() << " commands indexed." << std::endl;
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
                    // Phase 6: check if chronic failure should trigger neurogenesis
                    std::string cmd = j.value("command", "");
                    std::string domain = classify_command(cmd);
                    if (!domain.empty()) check_neurogenesis(domain);
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
                // Phase 6: Neurogenesis — genesis compilation result
                else if (origin == "motor_cortex" && intent == "genesis_result") {
                    std::string status = j.value("status", "");
                    std::string name = j.value("name", "");
                    if (status == "failure" && !name.empty()) {
                        // Remove from active set so retry is possible
                        active_specialists.erase(name);
                        std::cout << "[BASAL_GANGLIA] NEUROGENESIS FAILED for " << name
                                  << " — removed from active set for retry" << std::endl;
                    } else if (status == "success") {
                        std::cout << "[BASAL_GANGLIA] NEUROGENESIS COMPILED: " << name << std::endl;
                    }
                }
                // Phase 6: Specialist lobe periodic report + apoptosis check
                else if (intent == "specialist_report") {
                    std::string specialist = j.value("origin", "");
                    std::string domain = j.value("domain", "");
                    float rate = j.value("success_rate", 0.0f);
                    int handled = j.value("handled", 0);
                    std::cout << "[BASAL_GANGLIA] SPECIALIST REPORT: " << specialist
                              << " domain=" << domain << " rate=" << (int)(rate * 100)
                              << "% handled=" << handled << std::endl;

                    // Apoptosis tracking
                    if (!specialist.empty() && active_specialists.count(domain)) {
                        auto& tracker = apoptosis_trackers[domain];

                        // Track idle specialists (0 handled for consecutive reports)
                        if (handled == 0) {
                            tracker.consecutive_idle_reports++;
                        } else {
                            tracker.consecutive_idle_reports = 0;
                        }

                        // Track domain recovery (success > 70% in self-model)
                        if (self_model.count(domain)) {
                            auto& d = self_model[domain];
                            int total = d.success + d.failure;
                            if (total >= 10 && d.actual_success_rate > 0.70f) {
                                tracker.consecutive_healthy_reports++;
                            } else {
                                tracker.consecutive_healthy_reports = 0;
                            }
                        }

                        // Feedback loop: evaluate performance delta after evaluation window
                        if (tracker.evaluation_cycle >= ApoptosisTracker::EVALUATION_WINDOW) {
                            int post_total = tracker.post_injection_success + tracker.post_injection_failure;
                            float post_rate = post_total > 0
                                ? (float)tracker.post_injection_success / (float)post_total
                                : 0.0f;
                            float delta = post_rate - tracker.pre_injection_success_rate;

                            if (delta < -0.05f) {
                                // Specialist made things WORSE
                                std::cout << "[BASAL_GANGLIA] FEEDBACK: " << specialist
                                          << " DEGRADED domain '" << domain
                                          << "' (pre=" << (int)(tracker.pre_injection_success_rate * 100)
                                          << "% post=" << (int)(post_rate * 100) << "%)" << std::endl;

                                json terminate = {
                                    {"origin", "basal_ganglia"},
                                    {"intent", "lobe_terminate"},
                                    {"lobe_name", specialist},
                                    {"domain", domain},
                                    {"reason", "performance_degradation"}
                                };
                                routing::publish(pub, terminate);
                                active_specialists.erase(domain);
                                specialist_to_domain.erase(specialist);
                                apoptosis_trackers.erase(domain);
                                continue;  // skip normal apoptosis checks
                            } else if (delta < 0.05f && tracker.evaluation_cycle >= ApoptosisTracker::EVALUATION_WINDOW * 2) {
                                // No improvement after 2x window — futile
                                std::cout << "[BASAL_GANGLIA] FEEDBACK: " << specialist
                                          << " NO IMPROVEMENT for domain '" << domain
                                          << "' after " << tracker.evaluation_cycle << " cycles" << std::endl;

                                json terminate = {
                                    {"origin", "basal_ganglia"},
                                    {"intent", "lobe_terminate"},
                                    {"lobe_name", specialist},
                                    {"domain", domain},
                                    {"reason", "no_improvement"}
                                };
                                routing::publish(pub, terminate);
                                active_specialists.erase(domain);
                                specialist_to_domain.erase(specialist);
                                apoptosis_trackers.erase(domain);
                                continue;
                            } else if (delta >= 0.10f) {
                                // Significant improvement — dopamine reward
                                std::cout << "[BASAL_GANGLIA] FEEDBACK: " << specialist
                                          << " IMPROVED domain '" << domain
                                          << "' by +" << (int)(delta * 100) << "%" << std::endl;
                                emit_dopamine(domain, "neurogenesis_success", delta);

                                // Meta-template: reward the template that spawned this specialist
                                for (auto& mt : meta_population) {
                                    if (mt.specialists_spawned > mt.specialists_survived) {
                                        mt.specialists_survived++;
                                        mt.fitness = std::min(1.0f, mt.fitness + 0.1f * delta);
                                        break;
                                    }
                                }
                                // Trigger evolution after enough data
                                int total_spawned = 0;
                                for (auto& mt : meta_population) total_spawned += mt.specialists_spawned;
                                if (total_spawned > 0 && total_spawned % 5 == 0) evolve_meta_templates();
                                save_meta_templates();
                            }
                        }

                        // Lateral inhibition: if overlapping specialist has higher handled count, prune this one
                        tracker.cumulative_handled += handled;
                        tracker.report_count++;
                        if (check_lateral_inhibition(domain, specialist, tracker)) continue;

                        // Trigger apoptosis: 6 idle reports (~30 min) or 3 healthy (~15 min of >70%)
                        if (tracker.consecutive_idle_reports >= 6 ||
                            tracker.consecutive_healthy_reports >= 3) {

                            std::string reason = tracker.consecutive_idle_reports >= 6
                                ? "idle" : "domain_recovered";

                            std::cout << "[BASAL_GANGLIA] APOPTOSIS: Terminating " << specialist
                                      << " for domain " << domain
                                      << " reason=" << reason << std::endl;

                            json terminate = {
                                {"origin", "basal_ganglia"},
                                {"intent", "lobe_terminate"},
                                {"lobe_name", specialist},
                                {"domain", domain},
                                {"reason", reason}
                            };
                            routing::publish(pub, terminate);

                            active_specialists.erase(domain);
                            specialist_to_domain.erase(specialist);
                            apoptosis_trackers.erase(domain);
                        }
                    }
                }
                // Phase 6: Lobe injection confirmation — snapshot baseline for feedback loop
                else if (origin == "cerebral_matrix" && intent == "lobe_injected") {
                    std::string name = j.value("lobe_name", "");
                    std::cout << "[BASAL_GANGLIA] NEUROGENESIS COMPLETE: " << name
                              << " is now running in the matrix." << std::endl;

                    // Snapshot domain performance at injection time
                    std::string domain;
                    for (auto& [d, tag] : specialist_to_domain) {
                        if (tag == name) { domain = d; break; }
                    }
                    if (!domain.empty() && self_model.count(domain)) {
                        auto& tracker = apoptosis_trackers[domain];
                        auto& d = self_model[domain];
                        tracker.pre_injection_success_rate = d.actual_success_rate;
                        tracker.pre_injection_total = d.success + d.failure;
                        tracker.evaluation_cycle = 0;
                        tracker.post_injection_success = 0;
                        tracker.post_injection_failure = 0;
                        std::cout << "[BASAL_GANGLIA] FEEDBACK BASELINE: domain='" << domain
                                  << "' pre_rate=" << (int)(tracker.pre_injection_success_rate * 100)
                                  << "% at " << tracker.pre_injection_total << " attempts" << std::endl;
                    }
                }
                // Autopoiesis: domain resolve result from PrimordialLoop
                else if (origin == "primordial_loop" && intent == "domain_resolve_result") {
                    std::string domain = j.value("domain", "");
                    bool resolved = j.value("resolved", false);
                    std::string method = j.value("method", "");
                    int new_ops = j.value("new_operators", 0);

                    pending_resolve.erase(domain);

                    if (resolved) {
                        std::cout << "[BASAL_GANGLIA] AUTOPOIESIS RESOLVED: domain='" << domain
                                  << "' method=" << method << " new_ops=" << new_ops << std::endl;
                        // Reset domain failure tracking — give it a fresh chance
                        if (self_model.count(domain)) {
                            auto& d = self_model[domain];
                            d.consecutive_failures = 0;
                            d.cooldown_until = 0;
                        }
                    } else {
                        std::cout << "[BASAL_GANGLIA] AUTOPOIESIS FAILED for '" << domain
                                  << "' — falling back to NEUROGENESIS" << std::endl;
                        trigger_neurogenesis(domain);
                    }
                }
                // RLAIF: reinforce command genome from successful execution chains
                else if (origin == "frontal_executive" && intent == "rlaif_reinforce") {
                    std::string domain = j.value("domain", "");
                    float magnitude = j.value("magnitude", 0.0f);
                    auto cmds = j.value("commands", json::array());

                    int reinforced = 0;
                    for (auto& cmd_j : cmds) {
                        std::string cmd = cmd_j.get<std::string>();
                        // Extra fitness boost proportional to dopamine magnitude
                        auto& templates = command_genome[domain];
                        for (auto& t : templates) {
                            if (t.cmd == cmd) {
                                t.fitness = std::min(1.0f, t.fitness + 0.15f * magnitude);
                                reinforced++;
                                break;
                            }
                        }
                    }
                    if (reinforced > 0) {
                        save_genome();
                        std::cout << "[BASAL_GANGLIA] RLAIF: Reinforced " << reinforced
                                  << " templates in '" << domain << "' (dopamine="
                                  << magnitude << ")" << std::endl;
                    }
                }
                // Phase 6: Lobe termination confirmation — specialist removed
                else if (origin == "cerebral_matrix" && intent == "lobe_terminated") {
                    std::string name = j.value("lobe_name", "");
                    std::string reason = j.value("reason", "");
                    std::cout << "[BASAL_GANGLIA] APOPTOSIS COMPLETE: " << name
                              << " removed (" << reason << ")" << std::endl;
                }
                // ConceptLobe: learned representations update
                else if (origin == "concept_lobe" && intent == "concept_update") {
                    handle_concept_update(j);
                }
                else if (origin == "concept_lobe" && intent == "concept_response") {
                    handle_concept_response(j);
                }
                // Directed exploration: MetaCognition sends exploration targets
                else if (origin == "metacognition" && intent == "exploration_target") {
                    handle_exploration_target(j);
                }
                // Phase 7: Emergent goals from ConceptLobe — promote patterns to drives
                else if (origin == "concept_lobe" && intent == "emergent_goal") {
                    handle_emergent_goal(j);
                }
                // MetaCognition detected repetition — force domain rotation
                else if (origin == "metacognition" && intent == "stagnation_alert") {
                    handle_stagnation_alert(j);
                }
                // MetaCognition proposes system parameter adjustments
                else if (origin == "metacognition" && intent == "system_tuning") {
                    handle_system_tuning(j);
                }
                // BK-tree: command validation request from FrontalExecutive
                else if (intent == "validate_command") {
                    handle_validate_command(j);
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
        EMERGENT = 3,     // Phase 7: pattern-driven goals from ConceptLobe
        MASTERY = 4,      // retry domains with low success rates
        SELF_MODIFY = 5   // source modification, compilation, code generation
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
        // Requires: >70% success AND majority of domains explored (prevents
        // premature SELF_MODIFY when only a few domains are saturated)
        int total_s = 0, total_a = 0, explored_domains = 0;
        int total_domains = (int)self_model.size();
        for (auto& [d, st] : self_model) {
            total_s += st.success;
            total_a += st.success + st.failure;
            if (st.success + st.failure >= 3) explored_domains++;
        }
        float overall_rate = total_a > 0 ? (float)total_s / total_a : 0.0f;
        int required_explored = std::max(6, total_domains / 2);  // at least half, min 6
        if (overall_rate > 0.70f && explored_domains >= required_explored) return DriveLevel::SELF_MODIFY;

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
        // Phase 6: stale selection counter — how many times selected without new attempts
        int stale_selections = 0;
        // Phase 7: opportunity cost — attempts without substantive success
        int attempts_since_substantive_success = 0;
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

    // Keyword → domain classification (fallback when concept space has no match)
    struct KeywordRule {
        std::string domain;
        std::vector<std::string> keywords;
    };
    std::vector<KeywordRule> classification_rules;

    // Learned concept space — populated by ConceptLobe updates
    struct ConceptInfo {
        std::string abstraction;    // e.g., "read_content", "build_system"
        std::string pattern;        // e.g., "cat <path>"
        float success_rate = 0.0f;
        int members = 0;
        int observations = 0;
        float coherence = 0.0f;
    };
    std::map<std::string, ConceptInfo> concept_clusters;  // cluster_name → info
    // Maps concept abstractions to legacy domain names for compatibility
    std::map<std::string, std::string> concept_to_domain = {
        {"read_content", "file_read"}, {"write_output", "file_write"},
        {"copy_resource", "file_write"}, {"move_resource", "file_write"},
        {"create_resource", "file_write"}, {"create_structure", "file_write"},
        {"search_filesystem", "file_search"}, {"search_content", "file_search"},
        {"inspect_process", "process_inspection"}, {"signal_process", "process_inspection"},
        {"network_transfer", "network_diagnostics"}, {"network_inspect", "network_diagnostics"},
        {"network_probe", "network_diagnostics"}, {"remote_access", "network_diagnostics"},
        {"build_system", "compilation"}, {"compile", "compilation"},
        {"version_control", "git_operations"}, {"transform_text", "source_modification"},
        {"system_status", "system_monitoring"}, {"system_identity", "system_monitoring"},
        {"measure_content", "data_analysis"}, {"measure_resource", "data_analysis"},
        {"interpret", "script_creation"}, {"archive", "file_write"},
        {"compare_content", "source_modification"}, {"identify_type", "file_read"},
        {"verify_integrity", "file_read"}, {"modify_permissions", "file_write"}
    };
    bool concept_space_available = false;  // true once first concept_update received

    void init_domain_commands() {
        // Directed Tabula Rasa: Substantive seeds that produce real, measurable output.
        // No trivial commands (mkdir, id, whoami, echo) — those pollute the genome.
        for (const auto& domain : domains) {
            if (domain == "compilation")          domain_commands[domain] = {"make build/thalamus 2>&1 | head", "cc --version"};
            else if (domain == "file_read")       domain_commands[domain] = {"cat README.md", "head -20 CMakeLists.txt", "wc -l src/brainstem/*.cpp"};
            else if (domain == "file_write")      domain_commands[domain] = {"cp README.md data/readme_backup.md", "tee data/test_write.txt <<< 'neuroswarm test'"};
            else if (domain == "self_inspection") domain_commands[domain] = {"cat /proc/self/status | head -15", "ls -la build/"};
            else if (domain == "file_search")     domain_commands[domain] = {"find src -name '*.cpp' -type f | head -20", "grep -r 'intent' include/ --include='*.hpp' -l"};
            else if (domain == "process_inspection") domain_commands[domain] = {"ps aux --no-header | head -20", "pgrep -la neuro"};
            else if (domain == "network_diagnostics") domain_commands[domain] = {"ss -tlnp | head -20", "ss -an | grep 5555"};
            else if (domain == "git_operations")  domain_commands[domain] = {"git status --short", "git log --oneline -5"};
            else if (domain == "system_monitoring") domain_commands[domain] = {"free -h", "df -h / | tail -1"};
            else if (domain == "data_analysis")   domain_commands[domain] = {"wc -l data/operators.jsonl", "python3 -c 'import json; print(len(open(\"data/operators.jsonl\").readlines()))'"};
            else if (domain == "source_modification") domain_commands[domain] = {"head -5 src/brainstem/PrimordialLoop.cpp", "wc -l src/brainstem/*.cpp"};
            else if (domain == "script_creation") domain_commands[domain] = {"python3 -c 'print(\"hello from neuroswarm\")'", "bash -c 'for i in 1 2 3; do echo $i; done'"};
            else if (domain == "log_analysis")    domain_commands[domain] = {"tail -20 data/engrams/global_stream.jsonl", "wc -l data/engrams/*.jsonl | tail -5"};
            else if (domain == "memory_analysis") domain_commands[domain] = {"cat data/self_model.json | python3 -c \"import sys,json; sm=json.load(sys.stdin); print(len(sm),'domains')\"", "wc -l data/operators.jsonl data/concept_space.jsonl data/trajectories.jsonl"};
            else domain_commands[domain] = {"ls -la", "cat /proc/loadavg"};
        }

        // Classification rules: keyword → domain
        // Order matters: most specific first.  The FIRST keyword match wins.
        // Rules: (1) match on the primary verb/tool, not on pipe suffixes
        //        (2) "bash -c" / "sh -c" are wrappers, not domains — removed
        //        (3) pipe ops (| head, | tail, | sort) are NOT domain indicators
        classification_rules = {
            {"self_inspection",     {"self_model", "neuroswarm", "du -s"}},
            {"memory_analysis",     {"engram", "memory_index", "system_knowledge", "data/engrams"}},
            {"git_operations",      {"git "}},
            {"compilation",         {"cmake", "make ", "make\t", "gcc ", "g++ ", "clang ", "rustc ", "cargo "}},
            {"network_diagnostics", {"ss ", "netstat", "nc ", "curl ", "wget ", "ping ", "nmap ", "ip addr", "ifconfig"}},
            {"process_inspection",  {"ps ", "pgrep", "top ", "htop", "pidof", "kill ", "/proc/"}},
            {"system_monitoring",   {"uptime", "free ", "df ", "vmstat", "iostat", "sensors", "nvidia-smi", "lscpu", "uname"}},
            {"log_analysis",        {"progress.txt", "metrics/", "journal", "syslog", "dmesg"}},
            {"data_analysis",       {"python3", "jq ", "data/metrics", "data/self_model"}},
            {"source_modification", {"sed ", "patch ", "diff ", "nano ", "vim "}},
            {"script_creation",     {"#!/", "chmod +x"}},
            {"file_search",         {"find ", "grep ", "locate ", "which ", "whereis ", "fd ", "rg "}},
            {"file_write",          {"tee ", "cp ", "mv ", "touch ", "mkdir ", ">>"}},
            {"file_read",           {"cat ", "less ", "more ", "wc -l", "wc ", "file ", "stat ", "md5sum", "sha256sum", "readlink", "head ", "tail "}}
        };
    }

    std::string classify_command(const std::string& cmd) {
        // Try concept-based classification first if concept space is populated
        if (concept_space_available && !concept_clusters.empty()) {
            std::string concept_domain = classify_via_concepts(cmd);
            if (!concept_domain.empty()) return concept_domain;
        }

        // Strip shell wrappers to classify the REAL command inside
        std::string stripped = cmd;
        // Remove /bin/bash -c "..." or /bin/sh -c '...' wrappers
        for (const auto& prefix : {"/bin/bash -c ", "/bin/sh -c ", "bash -c ", "sh -c "}) {
            std::string p(prefix);
            if (stripped.find(p) == 0) {
                stripped = stripped.substr(p.size());
                // Remove surrounding quotes
                if (stripped.size() >= 2) {
                    char q = stripped.front();
                    if ((q == '"' || q == '\'') && stripped.back() == q)
                        stripped = stripped.substr(1, stripped.size() - 2);
                }
                break;
            }
        }

        // Lowercase for matching
        std::string cmd_lower = stripped;
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

    // Classify a command using the learned concept space
    // Sends an async concept_query — for now, uses cached cluster abstractions
    std::string classify_via_concepts(const std::string& cmd) {
        // Extract the verb from the command (first meaningful token)
        std::string verb;
        std::istringstream iss(cmd);
        std::string token;
        while (iss >> token) {
            if (token.find('=') != std::string::npos) continue;  // skip env vars
            if (token == "sudo" || token == "env" || token == "nice") continue;
            auto slash = token.rfind('/');
            if (slash != std::string::npos) token = token.substr(slash + 1);
            verb = token;
            break;
        }

        // Check if any concept cluster's abstraction or pattern matches this verb
        for (auto& [name, info] : concept_clusters) {
            // Match by abstraction name containing the verb
            std::string abs_lower = info.abstraction;
            std::transform(abs_lower.begin(), abs_lower.end(), abs_lower.begin(), ::tolower);

            // Match by pattern prefix (e.g., pattern "cat <path>" matches "cat")
            std::string pattern_verb;
            std::istringstream piss(info.pattern);
            piss >> pattern_verb;

            if (pattern_verb == verb || abs_lower.find(verb) != std::string::npos) {
                // Map concept abstraction to a domain
                // First check direct mapping
                if (concept_to_domain.count(info.abstraction)) {
                    return concept_to_domain[info.abstraction];
                }
                // Check if abstraction contains a known domain prefix
                for (auto& [concept, domain] : concept_to_domain) {
                    if (info.abstraction.find(concept) != std::string::npos) {
                        return domain;
                    }
                }
                // If no mapping exists, this is a genuinely new domain discovered by the concept space
                // Register it dynamically
                std::string new_domain = info.abstraction;
                if (std::find(domains.begin(), domains.end(), new_domain) == domains.end()) {
                    domains.push_back(new_domain);
                    std::cout << "[BASAL_GANGLIA] CONCEPT: New dynamic domain discovered: '"
                              << new_domain << "'" << std::endl;
                }
                return new_domain;
            }
        }
        return "";
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

        // Phase 6: Stale penalty — domain selected many times but never executed
        float stale_penalty = std::min(1.0f, d.stale_selections * 0.15f);

        // Phase 7: Opportunity cost — sigmoid penalty for fruitless attempts
        // Gentle at first (allows learning), steep after 10 attempts, saturates at 0.4
        float opportunity_cost = 0.0f;
        if (d.attempts_since_substantive_success > 3) {
            opportunity_cost = 0.4f / (1.0f + std::exp(-(d.attempts_since_substantive_success - 10.0f) / 3.0f));
        }

        // Phase 8: Habituation penalty — domains where recent commands are all repeats
        // score poorly. Measures command diversity: unique/total in recent history.
        float habituation_penalty = 0.0f;
        auto hit = domain_command_history.find(domain);
        if (hit != domain_command_history.end() && !hit->second.empty()) {
            auto& hist = hit->second;
            std::set<std::string> unique_cmds(hist.begin(), hist.end());
            float diversity = (float)unique_cmds.size() / (float)hist.size();
            // Low diversity (e.g., 1 unique out of 10 = 0.1) → penalty up to 0.5
            habituation_penalty = 0.5f * (1.0f - diversity);
        }

        float fitness = 0.20f * coverage
                      + 0.15f * trend
                      + 0.25f * pred_error
                      + 0.20f * novelty
                      - 0.10f * system_stress
                      - stale_penalty
                      - opportunity_cost
                      - habituation_penalty;

        return fitness;
    }

    // Semantic validation: reject degenerate commands that "succeed" without doing real work
    bool is_substantive_success(const std::string& cmd, const std::string& output) {
        // Uninstantiated parametric templates — {param} placeholders executed literally
        if (cmd.find('{') != std::string::npos && cmd.find('}') != std::string::npos) return false;

        // Corrupted fragments starting with hyphens
        if (cmd.find("-") == 0) return false;

        // Loop attractors — commands the system fixates on without learning
        if (cmd == "whoami" || cmd == "id" || cmd == "hostname" || cmd == "pwd"
            || cmd == "uname" || cmd == "uname -a" || cmd == "uptime" || cmd == "date") return false;

        // mkdir spam — trivial directory creation is not real work
        if (cmd.find("mkdir") == 0 && cmd.find("&&") == std::string::npos) return false;

        // Pure echo commands — exit 0 but no real work
        if (cmd.find("echo ") == 0 && cmd.find("&&") == std::string::npos
            && cmd.find("|") == std::string::npos) return false;

        // Echo-prefixed commands where only the echo part ran (rest is no-op)
        if (cmd.find("echo ") != std::string::npos && cmd.find("/dev/null") != std::string::npos)
            return false;

        // Detect shell error strings in output (false successes)
        std::vector<std::string> shell_errors = {
            "command not found", "No such file", "sh: line 1", "syntax error",
            "not a directory", "permission denied", "invalid option", "usage:"
        };
        std::string lower_out = output;
        std::transform(lower_out.begin(), lower_out.end(), lower_out.begin(), ::tolower);
        for (const auto& err : shell_errors) {
            if (lower_out.find(err) != std::string::npos) return false;
        }

        // Detect "|| echo" trick where output matches the echo'd fallback exactly
        size_t echo_pos = cmd.find("|| echo ");
        if (echo_pos != std::string::npos) {
            std::string echo_val = cmd.substr(echo_pos + 8);
            // Remove quotes if present
            echo_val.erase(std::remove(echo_val.begin(), echo_val.end(), '\''), echo_val.end());
            echo_val.erase(std::remove(echo_val.begin(), echo_val.end(), '\"'), echo_val.end());
            
            // If output is exactly the echo value (trimmed), it's a masked failure
            std::string trimmed_out = output;
            trimmed_out.erase(std::remove(trimmed_out.begin(), trimmed_out.end(), '\n'), trimmed_out.end());
            trimmed_out.erase(std::remove(trimmed_out.begin(), trimmed_out.end(), '\r'), trimmed_out.end());
            if (trimmed_out == echo_val) return false;
        }

        // Commands that produce no output are suspicious — UNLESS they're write operations
        // cp, mv, touch, tee, >, >> all succeed silently with exit 0
        if (output.size() < 3) {
            bool is_write_cmd = (cmd.find("cp ") == 0 || cmd.find("mv ") == 0 ||
                                 cmd.find("touch ") == 0 || cmd.find("tee ") == 0 ||
                                 cmd.find("cat ") != std::string::npos && cmd.find(">") != std::string::npos ||
                                 cmd.find("sed -i") != std::string::npos ||
                                 cmd.find("patch ") == 0 ||
                                 cmd.find("chmod ") == 0 || cmd.find("chown ") == 0 ||
                                 cmd.find(">>") != std::string::npos ||
                                 cmd.find("> ") != std::string::npos);
            if (!is_write_cmd) return false;
        }

        // Output is just placeholder text
        if (output.find("Running '") == 0 || output.find("Running \"") == 0)
            return false;

        return true;
    }

    void handle_execution_result(const json& j) {
        std::string cmd = j.value("command", "");
        if (cmd.empty()) {
            cmd = j.value("original_command", "");
        }
        if (cmd.empty()) return;

        std::string domain = classify_command(cmd);
        if (domain.empty()) return;

        std::string status = j.value("status", "");
        std::string output = j.value("proprioception", "");
        bool success = (status == "success");

        // Semantic validation: downgrade degenerate successes
        if (success && !is_substantive_success(cmd, output)) {
            success = false;  // treat as failure for learning purposes
        }

        auto& d = self_model[domain];
        int total_before = d.success + d.failure;

        if (success) {
            d.success++;
            d.consecutive_failures = 0;
            // Only reset opportunity cost if this is a genuinely novel success
            // Repeating the same command 30 times is not progress
            float exec_habit = habituation_factor(domain, cmd);
            if (exec_habit > 0.25f) {
                d.attempts_since_substantive_success = 0;
            } else {
                d.attempts_since_substantive_success++;  // repetition = no real progress
            }
        } else {
            d.failure++;
            d.consecutive_failures++;
            d.attempts_since_substantive_success++;  // Phase 7: no progress — accumulate cost

            // Learned helplessness: 5 consecutive failures → 3-minute cooldown
            if (d.consecutive_failures >= 5) {
                long now_ts = std::time(nullptr);
                d.cooldown_until = now_ts + 180; // 3 minutes
                std::cout << "[BASAL_GANGLIA] Learned helplessness: domain '" << domain
                          << "' entering 10-minute cooldown after " << d.consecutive_failures
                          << " consecutive failures." << std::endl;
            }
        }

        d.last_attempt_ts = std::time(nullptr);
        d.stale_selections = 0;  // Phase 6: domain is actually executing

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
        // Modulated by habituation: repeated commands produce weaker signals
        bool is_novel = (total_before == 0 && total == 1);
        bool is_surprising = (d.prediction_error > 0.3f);

        if (is_novel || is_surprising) {
            float habit = habituation_factor(domain, cmd);
            float magnitude = d.prediction_error * habit;
            emit_dopamine(domain,
                magnitude > 0.02f ? (is_novel ? "novel_capability" : "prediction_surprise") : "habituated",
                magnitude);
        }

        // Cross-domain transfer: propagate confidence to related domains
        // Use concept space affinity first, fall back to static affinity
        if (success) {
            std::set<std::string> related_domains;

            // Concept-based affinity: find domains whose concept clusters are near this one
            if (concept_space_available) {
                // Find which concept cluster this domain maps to
                for (auto& [cname, cinfo] : concept_clusters) {
                    // Check if this concept relates to the current domain
                    bool matches = false;
                    if (cinfo.abstraction == domain) matches = true;
                    for (auto& [concept, mapped] : concept_to_domain) {
                        if (mapped == domain && cinfo.abstraction.find(concept) != std::string::npos) {
                            matches = true;
                            break;
                        }
                    }
                    if (!matches) continue;

                    // All other concepts are potential transfer targets
                    for (auto& [other_name, other_info] : concept_clusters) {
                        if (other_name == cname) continue;
                        // Map concept to domain
                        std::string related;
                        for (auto& [concept, mapped] : concept_to_domain) {
                            if (other_info.abstraction.find(concept) != std::string::npos) {
                                related = mapped;
                                break;
                            }
                        }
                        if (related.empty()) related = other_info.abstraction;
                        if (self_model.count(related)) related_domains.insert(related);
                    }
                }
            }

            // Static affinity fallback
            if (related_domains.empty() && domain_affinity.count(domain)) {
                for (auto& r : domain_affinity[domain]) related_domains.insert(r);
            }

            for (auto& related : related_domains) {
                if (self_model.count(related)) {
                    auto& rd = self_model[related];
                    rd.predicted_success_rate = std::min(1.0f, rd.predicted_success_rate + 0.05f);
                }
            }
        }

        // Feedback loop: track post-injection performance for active specialists
        if (active_specialists.count(domain) && apoptosis_trackers.count(domain)) {
            auto& tracker = apoptosis_trackers[domain];
            if (success) tracker.post_injection_success++;
            else         tracker.post_injection_failure++;
            tracker.evaluation_cycle++;
        }

        // Phase 9: Staleness recovery — when a domain is completely stuck,
        // reset its command history to break habituation and allow fresh exploration.
        // Biological analogue: synaptic rescaling during sleep.
        if (d.attempts_since_substantive_success >= 50) {
            auto hit = domain_command_history.find(domain);
            if (hit != domain_command_history.end() && hit->second.size() > 5) {
                std::cout << "[BASAL_GANGLIA] Staleness recovery: resetting command history for '"
                          << domain << "' (stale=" << d.attempts_since_substantive_success
                          << ")" << std::endl;
                // Keep only last 3 commands to preserve some memory
                auto& hist = hit->second;
                if (hist.size() > 3) {
                    hist.erase(hist.begin(), hist.end() - 3);
                }
                // Halve the staleness counter — gives another 50 attempts
                d.attempts_since_substantive_success /= 2;
                // Reset cooldown so the domain can be selected again
                d.cooldown_until = 0;
                d.consecutive_failures = 0;
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

        auto& d = self_model[domain];
        if (success) {
            d.consecutive_failures = 0;

            // Habituation: compute novelty of this command in this domain
            float habit = habituation_factor(domain, cmd);

            // RLAIF: reinforce all commands from the successful execution chain
            // Only reinforce substantive commands (filter degenerate echo-only)
            if (j.contains("executed_commands")) {
                auto cmds = j["executed_commands"];
                int reinforced = 0;
                for (auto& cmd_j : cmds) {
                    std::string c = cmd_j.get<std::string>();
                    if (!is_substantive_success(c, "validated")) continue;
                    // Habituation: repeated commands get diminishing fitness boost
                    float cmd_habit = habituation_factor(domain, c);
                    if (cmd_habit > 0.1f) {
                        update_genome_fitness(domain, c, true);
                        reinforced++;
                    }
                }
                if (reinforced > 0) {
                    // Dopamine magnitude modulated by habituation
                    float dopamine = 0.3f * habit;
                    // Always emit signal (even when suppressed) so dashboard can visualize habituation
                    emit_dopamine(domain, dopamine > 0.02f ? "rlaif_chain_success" : "habituated", dopamine);
                    if (dopamine > 0.02f) {
                        std::cout << "[BASAL_GANGLIA] RLAIF: Chain reinforcement — "
                                  << reinforced << " commands in '" << domain
                                  << "' (habituation=" << habit << ")" << std::endl;
                    } else {
                        std::cout << "[BASAL_GANGLIA] HABITUATION: Suppressed dopamine for '"
                                  << domain << "' (habit=" << habit << ")" << std::endl;
                    }
                } else {
                    // No substantive commands — emit zero dopamine
                    emit_dopamine(domain, "habituated", 0.0f);
                }
            } else if (habit < 0.1f) {
                // Even without RLAIF chain, penalise repetitive domains
                d.attempts_since_substantive_success++;
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // Directed Exploration: MetaCognition → BasalGanglia priority steering
    // ──────────────────────────────────────────────────────────────────────

    // MetaCognition detected the system is stuck repeating the same command.
    // This is the system's own self-awareness kicking in — it noticed the loop.
    // Response: cooldown the stagnating domain, force rotation to something new.
    void handle_stagnation_alert(const json& j) {
        std::string domain = j.value("domain", "");
        std::string repeated_cmd = j.value("repeated_command", "");
        int count = j.value("repetition_count", 0);
        float ratio = j.value("repetition_ratio", 0.0f);

        if (domain.empty() || !self_model.count(domain)) return;

        auto& d = self_model[domain];

        // Apply cooldown proportional to repetition severity (tunable)
        int cooldown_secs = (int)(cooldown_multiplier_ * ratio);
        cooldown_secs = std::max(cooldown_secs, 60);  // minimum 60s cooldown
        d.cooldown_until = std::time(nullptr) + cooldown_secs;

        // Spike opportunity cost — this domain is not producing novelty
        d.attempts_since_substantive_success += count;

        std::cout << "[BASAL_GANGLIA] SELF-AWARENESS: MetaCognition detected stagnation in '"
                  << domain << "' ('" << repeated_cmd.substr(0, 40) << "' x" << count
                  << "). Applying " << cooldown_secs << "s cooldown." << std::endl;

        save_self_model();  // Persist the cooldown
    }

    // MetaCognition proposes parameter adjustments based on meta-observations.
    // This is directed self-modification: the system tuning its own parameters.
    void handle_system_tuning(const json& j) {
        auto adjustments = j.value("adjustments", json::array());
        if (adjustments.empty()) return;

        int applied = 0;
        for (auto& adj : adjustments) {
            std::string param = adj.value("parameter", "");
            std::string recommendation = adj.value("recommendation", "");

            if (param == "cooldown_multiplier" && recommendation == "increase") {
                float old = cooldown_multiplier_;
                float suggested = adj.value("suggested_value", 180.0f);
                cooldown_multiplier_ = std::max(cooldown_multiplier_, suggested);
                std::cout << "[BASAL_GANGLIA] SELF-MODIFY: cooldown_multiplier " << old
                          << " → " << cooldown_multiplier_
                          << " (MetaCognition: " << adj.value("reasoning", "") << ")" << std::endl;
                applied++;
            }
            else if (param == "domain_rotation_pressure" && recommendation == "increase") {
                float old = domain_rotation_pressure_;
                float suggested = adj.value("suggested_value", 2.0f);
                domain_rotation_pressure_ = suggested;
                std::cout << "[BASAL_GANGLIA] SELF-MODIFY: domain_rotation_pressure " << old
                          << " → " << domain_rotation_pressure_ << std::endl;
                applied++;
            }
            else if (param == "priority_capability") {
                priority_capability_ = recommendation;
                std::cout << "[BASAL_GANGLIA] SELF-MODIFY: priority_capability → '"
                          << priority_capability_ << "' ("
                          << adj.value("reasoning", "") << ")" << std::endl;
                applied++;
            }
        }

        if (applied > 0) {
            std::cout << "[BASAL_GANGLIA] Applied " << applied << "/" << adjustments.size()
                      << " self-modifications from MetaCognition." << std::endl;
        }
    }

    void handle_exploration_target(const json& j) {
        exploration_target_domain     = j.value("target_domain", "");
        exploration_target_capability = j.value("target_capability", "");
        exploration_target_importance = j.value("importance", 0.0f);
        exploration_strategy          = j.value("exploration_strategy", "");
        exploration_gap_error         = j.value("gap_error", "");
        exploration_root_cause        = j.value("root_cause", "");
        exploration_target_timestamp  = std::time(nullptr);

        std::cout << "[BASAL_GANGLIA] EXPLORATION TARGET received: domain='"
                  << exploration_target_domain << "' capability='"
                  << exploration_target_capability << "' importance="
                  << exploration_target_importance << " strategy='"
                  << exploration_strategy << "'" << std::endl;
    }

    bool has_active_exploration_target() const {
        if (exploration_target_domain.empty()) return false;
        long now = std::time(nullptr);
        return (now - exploration_target_timestamp) < EXPLORATION_TARGET_TTL;
    }

    // Phase 7: Emergent meta-goals — ConceptLobe promotes discovered patterns to drives
    struct EmergentGoal {
        std::string composite_name;
        std::vector<std::string> sequence;       // domain sequence (e.g., file_read → data_analysis)
        std::vector<std::string> example_commands;
        float success_rate = 0.0f;
        int observation_count = 0;
        long timestamp = 0;
    };
    std::vector<EmergentGoal> emergent_goals;
    static constexpr int MAX_EMERGENT_GOALS = 3;

    void handle_emergent_goal(const json& j) {
        std::string name = j.value("composite_name", "");
        if (name.empty()) return;

        float sr = j.value("success_rate", 0.0f);
        int obs = j.value("observation_count", 0);

        // Require minimum confidence
        if (obs < 5 || sr < 0.6f) return;

        // Check for duplicate
        for (auto& eg : emergent_goals) {
            if (eg.composite_name == name) {
                eg.observation_count = obs;
                eg.success_rate = sr;
                eg.timestamp = std::time(nullptr);
                return;
            }
        }

        EmergentGoal eg;
        eg.composite_name = name;
        eg.success_rate = sr;
        eg.observation_count = obs;
        eg.timestamp = std::time(nullptr);

        if (j.contains("sequence") && j["sequence"].is_array()) {
            for (auto& s : j["sequence"]) eg.sequence.push_back(s.get<std::string>());
        }
        if (j.contains("example_commands") && j["example_commands"].is_array()) {
            for (auto& c : j["example_commands"]) eg.example_commands.push_back(c.get<std::string>());
        }

        // Cap at MAX_EMERGENT_GOALS — evict lowest observation count
        if ((int)emergent_goals.size() >= MAX_EMERGENT_GOALS) {
            auto worst = std::min_element(emergent_goals.begin(), emergent_goals.end(),
                [](const EmergentGoal& a, const EmergentGoal& b) {
                    return a.observation_count < b.observation_count;
                });
            if (worst != emergent_goals.end() && obs > worst->observation_count) {
                std::cout << "[BASAL_GANGLIA] EMERGENT: evicting '" << worst->composite_name
                          << "' for '" << name << "'" << std::endl;
                emergent_goals.erase(worst);
            } else {
                return;  // new goal isn't better than existing ones
            }
        }

        emergent_goals.push_back(eg);

        // Promote to pending drive
        std::string seq_str;
        for (size_t i = 0; i < eg.sequence.size(); i++) {
            if (i > 0) seq_str += " → ";
            seq_str += eg.sequence[i];
        }
        pending_drives.push_back({DriveLevel::EMERGENT,
            "[EMERGENT] Execute discovered pattern '" + name + "': " + seq_str,
            "concept_lobe"});

        std::cout << "[BASAL_GANGLIA] EMERGENT GOAL registered: '" << name
                  << "' (" << seq_str << ") sr=" << (int)(sr * 100)
                  << "% obs=" << obs << std::endl;
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
                case DriveLevel::EMERGENT:    drive_name = "EMERGENT"; break;
                default:                      drive_name = "UNKNOWN"; break;
            }

            // Phase 7: EMERGENT drives carry their own sequence and commands
            if (drive.level == DriveLevel::EMERGENT) {
                // Find matching emergent goal for context
                json suggested = json::array();
                std::string domain = "self_inspection";
                for (auto& eg : emergent_goals) {
                    if (drive.description.find(eg.composite_name) != std::string::npos) {
                        for (auto& c : eg.example_commands) suggested.push_back(c);
                        if (!eg.sequence.empty()) domain = eg.sequence[0];  // start with first domain
                        break;
                    }
                }
                json goal = {
                    {"cid", cid}, {"origin", "basal_ganglia"},
                    {"intent", "intrinsic_goal"},
                    {"domain", domain}, {"fitness", 0.8f},
                    {"drive_level", "EMERGENT"},
                    {"suggested_commands", suggested},
                    {"context", drive.description}
                };
                routing::publish(pub, goal);
                std::cout << "[BASAL_GANGLIA] EMERGENT goal published: " << drive.description << std::endl;
                return;
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
                // Consider source_modification, compilation, and concept-derived equivalents
                bool is_self_modify_domain =
                    domain == "source_modification" || domain == "compilation" ||
                    domain == "build_system" || domain == "compile" ||
                    domain == "transform_text" || domain == "compare_content";
                // Also allow any concept-derived domain that maps to these
                if (!is_self_modify_domain && concept_space_available) {
                    for (auto& [concept, mapped] : concept_to_domain) {
                        if ((mapped == "compilation" || mapped == "source_modification") &&
                            domain.find(concept) != std::string::npos) {
                            is_self_modify_domain = true;
                            break;
                        }
                    }
                }
                if (!is_self_modify_domain) continue;
            }

            float f = compute_fitness(domain);

            // Phase 5: Stamina factor — scale non-survival fitness by energy
            f *= (current_stamina / 100.0f);

            // Directed exploration: boost domain targeted by MetaCognition
            // Capped to +0.3 to prevent monopoly — importance only determines direction, not magnitude
            if (has_active_exploration_target() && domain == exploration_target_domain) {
                float boost = std::min(0.3f, exploration_target_importance * 0.02f);
                f += boost;
            }

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
                // Always respond so FrontalExecutive doesn't stall waiting
                json no_goal = {
                    {"cid", cid}, {"origin", "basal_ganglia"},
                    {"intent", "intrinsic_goal_unavailable"},
                    {"reason", "all_cooldown"}
                };
                routing::publish(pub, no_goal);
                return;
            }
        }

        // Anti-repetition: force domain rotation after MAX_CONSECUTIVE_SAME_DOMAIN
        if (best_domain == last_selected_domain) {
            consecutive_same_domain++;
            if (consecutive_same_domain >= MAX_CONSECUTIVE_SAME_DOMAIN) {
                // Put this domain in short cooldown and find next best
                self_model[best_domain].cooldown_until = now_ts + 30;  // 30s cooldown
                std::cout << "[BASAL_GANGLIA] Domain rotation: '" << best_domain
                          << "' selected " << consecutive_same_domain
                          << "x consecutively. Forcing 2min cooldown." << std::endl;
                consecutive_same_domain = 0;

                // Re-pick excluding cooled-down domain
                best_domain.clear();
                best_fitness = -999.0f;
                for (auto& domain : domains) {
                    if (self_model[domain].cooldown_until > now_ts) continue;
                    float f = compute_fitness(domain) * (current_stamina / 100.0f);
                    if (f > best_fitness) { best_fitness = f; best_domain = domain; }
                }
                if (best_domain.empty()) {
                    std::cout << "[BASAL_GANGLIA] All domains in cooldown after rotation. Skipping." << std::endl;
                    json no_goal = {
                        {"cid", cid}, {"origin", "basal_ganglia"},
                        {"intent", "intrinsic_goal_unavailable"},
                        {"reason", "all_cooldown_after_rotation"}
                    };
                    routing::publish(pub, no_goal);
                    return;
                }
            }
        } else {
            consecutive_same_domain = 1;
        }
        last_selected_domain = best_domain;

        auto& d = self_model[best_domain];
        int total = d.success + d.failure;

        // Phase 6: Track stale selections — domain picked but never executed
        d.stale_selections++;

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

        // Directed exploration: enrich context with MetaCognition gap info
        bool is_directed = has_active_exploration_target() && best_domain == exploration_target_domain;
        if (is_directed) {
            context += " [DIRECTED EXPLORATION] MetaCognition identified gap: "
                    + exploration_target_capability + ". ";
            if (!exploration_gap_error.empty())
                context += "Error pattern: " + exploration_gap_error + ". ";
            if (!exploration_root_cause.empty())
                context += "Root cause: " + exploration_root_cause + ". ";
            if (!exploration_strategy.empty())
                context += "Strategy: " + exploration_strategy + ". ";
        }

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

        // Add exploration metadata if directed
        if (is_directed) {
            goal["exploration_target"] = exploration_target_capability;
            goal["exploration_strategy"] = exploration_strategy;
            goal["exploration_directed"] = true;
        }

        routing::publish(pub, goal);

        // Query concept space for transfer learning — find similar successful operations
        query_concept_transfer(cid, best_domain);

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

    // --- Concept space integration ---

    void handle_concept_update(const json& j) {
        auto clusters = j.value("clusters", json::array());
        int new_concepts = 0;

        for (auto& c : clusters) {
            std::string name = c.value("concept", "");
            if (name.empty()) continue;

            ConceptInfo info;
            info.abstraction = name;
            info.pattern = c.value("pattern", "");
            info.success_rate = c.value("success_rate", 0.0f);
            info.members = c.value("members", 0);
            info.observations = c.value("observations", 0);
            info.coherence = c.value("coherence", 0.0f);

            bool is_new = !concept_clusters.count(name);
            concept_clusters[name] = info;

            if (is_new && info.members >= 3) {
                new_concepts++;
                // Check if this concept maps to an existing domain
                std::string mapped_domain;
                for (auto& [concept, domain] : concept_to_domain) {
                    if (name.find(concept) != std::string::npos) {
                        mapped_domain = domain;
                        break;
                    }
                }

                if (mapped_domain.empty()) {
                    // Genuinely new domain discovered by concept space
                    if (std::find(domains.begin(), domains.end(), name) == domains.end()) {
                        domains.push_back(name);
                        std::cout << "[BASAL_GANGLIA] CONCEPT: Emergent domain '" << name
                                  << "' (pattern: " << info.pattern
                                  << ", members: " << info.members << ")" << std::endl;
                    }
                }
            }
        }

        if (!concept_space_available) {
            concept_space_available = true;
            std::cout << "[BASAL_GANGLIA] CONCEPT SPACE ONLINE: " << concept_clusters.size()
                      << " clusters available for classification" << std::endl;
        }

        if (new_concepts > 0) {
            std::cout << "[BASAL_GANGLIA] CONCEPT UPDATE: " << new_concepts
                      << " new concepts, " << concept_clusters.size() << " total" << std::endl;
        }
    }

    // Pending concept queries for goal enrichment
    std::map<std::string, std::string> pending_concept_queries;  // concept_cid → goal_cid

    void handle_concept_response(const json& j) {
        std::string cid = j.value("cid", "");
        auto concepts = j.value("concepts", json::array());

        // Check if this was a goal-enrichment query
        if (pending_concept_queries.count(cid)) {
            std::string goal_cid = pending_concept_queries[cid];
            pending_concept_queries.erase(cid);

            // Enrich the intrinsic goal with concept-based transfer commands
            if (!concepts.empty()) {
                json transfer_cmds = json::array();
                for (auto& c : concepts) {
                    float sim = c.value("similarity", 0.0f);
                    if (sim < 0.5f) continue;
                    auto examples = c.value("example_commands", json::array());
                    for (auto& ex : examples) {
                        transfer_cmds.push_back(ex);
                    }
                }

                if (!transfer_cmds.empty()) {
                    std::cout << "[BASAL_GANGLIA] CONCEPT TRANSFER: " << transfer_cmds.size()
                              << " commands from similar concepts for CID " << goal_cid << std::endl;
                    // Publish as supplementary suggested commands
                    json supplement = {
                        {"origin", "basal_ganglia"},
                        {"intent", "concept_transfer"},
                        {"cid", goal_cid},
                        {"transfer_commands", transfer_cmds}
                    };
                    routing::publish(pub, supplement);
                }
            }
        }
    }

    // Query concept space for commands similar to a domain goal
    void query_concept_transfer(const std::string& goal_cid, const std::string& domain) {
        if (!concept_space_available) return;

        std::string concept_cid = "bg_concept_" + goal_cid;
        pending_concept_queries[concept_cid] = goal_cid;

        json query = {
            {"origin", "basal_ganglia"}, {"intent", "concept_query"},
            {"cid", concept_cid},
            {"query", "operations for " + domain + " domain"}
        };
        routing::publish(pub, query);
    }

    // Phase 4: Command Genome — evolutionary command template system
    struct CommandTemplate {
        std::string cmd;
        float fitness = 0.5f;
        int generation = 0;
    };

    std::map<std::string, std::vector<CommandTemplate>> command_genome;
    static constexpr const char* GENOME_PATH = "./data/command_genome.json";

    // BK-tree: indexes ALL successful commands across all domains
    // for fuzzy matching, validation, and deduplication
    BKTree bk_tree;
    int bk_tree_compaction_counter = 0;
    static constexpr int COMPACTION_INTERVAL = 100;  // compact every N new commands

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

            // Insert into BK-tree if successful
            if (success) {
                bk_tree.insert(cmd, ct.fitness, domain);
            }

            // Cap at 20 templates per domain
            if (templates.size() > 20) {
                // Remove lowest fitness
                auto worst = std::min_element(templates.begin(), templates.end(),
                    [](const CommandTemplate& a, const CommandTemplate& b) { return a.fitness < b.fitness; });
                templates.erase(worst);
            }

            // Periodic genome compaction
            bk_tree_compaction_counter++;
            if (bk_tree_compaction_counter >= COMPACTION_INTERVAL) {
                bk_tree_compaction_counter = 0;
                compact_genome();
            }
        }

        save_genome();
    }

    // ──────────────────────────────────────────────────────────────────────
    // Phase 6: Autonomous Neurogenesis — self-generating specialist lobes
    // ──────────────────────────────────────────────────────────────────────
    std::set<std::string> active_specialists;
    std::set<std::string> pending_resolve;  // domains awaiting autopoiesis resolution

    // Directed exploration: MetaCognition → BasalGanglia exploration target
    std::string exploration_target_domain;
    std::string exploration_target_capability;
    std::string exploration_strategy;
    std::string exploration_gap_error;
    std::string exploration_root_cause;
    float exploration_target_importance = 0.0f;
    long exploration_target_timestamp = 0;
    static constexpr int EXPLORATION_TARGET_TTL = 300;  // 5 min expiry

    // ─── Tunable parameters (self-modified by MetaCognition) ───
    float cooldown_multiplier_ = 180.0f;    // base cooldown scaling (seconds * ratio)
    float domain_rotation_pressure_ = 1.0f; // multiplier on rotation urgency
    std::string priority_capability_;       // capability MetaCognition wants us to focus on

    // ─── Habituation: dopamine decays with command repetition ───
    // Biological analogue: sensory habituation. Repeated identical stimuli
    // produce progressively weaker neural responses. The 1st time you hear
    // a sound it's startling; the 31st time, your brain ignores it.
    // No manual cooldowns needed — the reward signal itself carries novelty.
    static constexpr int HABITUATION_WINDOW = 50;  // remember last N commands per domain
    std::map<std::string, std::vector<std::string>> domain_command_history;

    float habituation_factor(const std::string& domain, const std::string& cmd) {
        auto& history = domain_command_history[domain];

        // Count how many times this exact command appeared recently
        int repeats = 0;
        for (auto& h : history) {
            if (h == cmd) repeats++;
        }

        // Record this command
        history.push_back(cmd);
        if ((int)history.size() > HABITUATION_WINDOW) {
            history.erase(history.begin());
        }

        // Harmonic decay: 1.0 on first use, gentler than exponential
        // 1st: 1.0, 2nd: 0.5, 3rd: 0.33, 4th: 0.25, 10th: 0.1
        if (repeats == 0) return 1.0f;
        return std::max(0.05f, 1.0f / (float)(1 + repeats));
    }

    // Anti-repetition: domain rotation tracking
    std::string last_selected_domain;
    int consecutive_same_domain = 0;
    static constexpr int MAX_CONSECUTIVE_SAME_DOMAIN = 5;  // force rotation after 5
    static constexpr const char* GENESIS_DIR = "./src/lobes/genesis/";

    // Apoptosis tracking: domain → consecutive idle/healthy reports + performance delta
    struct ApoptosisTracker {
        int consecutive_idle_reports = 0;    // specialist handling 0 commands
        int consecutive_healthy_reports = 0; // domain success rate > 70%
        // Feedback loop: measure before/after specialist injection
        float pre_injection_success_rate = 0.0f;
        int   pre_injection_total = 0;
        int   evaluation_cycle = 0;          // post-injection execution count
        int   post_injection_success = 0;
        int   post_injection_failure = 0;
        static constexpr int EVALUATION_WINDOW = 15;
        // Lateral inhibition: cumulative performance tracking
        int cumulative_handled = 0;
        int report_count = 0;
    };
    std::map<std::string, ApoptosisTracker> apoptosis_trackers;

    // Map lobe_tag → domain for injection tracking
    std::map<std::string, std::string> specialist_to_domain;

    // Load existing specialists from genesis directory on startup.
    // If the compiled binary exists, re-inject it into the Matrix.
    void load_active_specialists() {
        std::string cmd = "ls " + std::string(GENESIS_DIR) + "*_specialist.cpp 2>/dev/null";
        std::array<char, 256> buf;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (pipe) {
            while (fgets(buf.data(), buf.size(), pipe.get())) result += buf.data();
        }

        // Brief delay to let pub socket connect before publishing
        std::this_thread::sleep_for(std::chrono::seconds(2));

        std::istringstream iss(result);
        std::string line;
        while (std::getline(iss, line)) {
            auto slash = line.rfind('/');
            if (slash == std::string::npos) continue;
            std::string filename = line.substr(slash + 1);
            auto suffix = filename.find("_specialist.cpp");
            if (suffix == std::string::npos) continue;
            std::string domain = filename.substr(0, suffix);

            // Build the expected lobe tag and binary path
            auto params = build_specialist_params(domain);
            std::string binary = "build/" + params.lobe_tag;

            // Check if compiled binary exists
            struct stat st;
            if (::stat(binary.c_str(), &st) == 0 && (st.st_mode & S_IXUSR)) {
                // Binary exists — re-inject into Matrix
                active_specialists.insert(domain);
                specialist_to_domain[params.lobe_tag] = domain;
                json inject = {
                    {"origin", "basal_ganglia"},
                    {"intent", "inject_lobe"},
                    {"name", params.lobe_tag},
                    {"path", binary}
                };
                routing::publish(pub, inject);
                std::cout << "[BASAL_GANGLIA] Re-injecting specialist: " << params.lobe_tag
                          << " for domain " << domain << std::endl;
            } else {
                // Source exists but binary missing — don't mark as active, allow neurogenesis
                std::cout << "[BASAL_GANGLIA] Specialist source exists for " << domain
                          << " but binary missing. Will regenerate if needed." << std::endl;
            }
        }
    }

    // Pre-validation commands per domain — specialist lobes run these before acting
    std::map<std::string, std::vector<std::string>> domain_pre_validations = {
        {"file_write",       {"test -d \"$(dirname '%CMD_TARGET%')\"", "test -w \"$(dirname '%CMD_TARGET%')\" || echo 'DIR_NOT_WRITABLE'"}},
        {"file_read",        {"test -f '%CMD_TARGET%' || echo 'FILE_NOT_FOUND'", "test -r '%CMD_TARGET%' || echo 'FILE_NOT_READABLE'"}},
        {"script_creation",  {"which bash || echo 'NO_BASH'", "test -w /tmp || echo 'TMP_NOT_WRITABLE'"}},
        {"compilation",      {"test -d build || echo 'NO_BUILD_DIR'", "which g++ || echo 'NO_COMPILER'"}},
        {"file_search",      {"test -d src/ || echo 'NO_SRC_DIR'"}},
        {"git_operations",   {"test -d .git || echo 'NOT_A_GIT_REPO'"}},
        {"network_diagnostics", {"which ss || which netstat || echo 'NO_NET_TOOLS'"}},
        {"process_inspection",  {"test -d /proc || echo 'NO_PROC_FS'"}},
        {"source_modification", {"test -d src/ || echo 'NO_SRC_DIR'", "test -w src/ || echo 'SRC_NOT_WRITABLE'"}},
        {"data_analysis",    {"test -d data/ || echo 'NO_DATA_DIR'"}},
        {"log_analysis",     {"test -f progress.txt || test -d data/metrics/ || echo 'NO_LOGS'"}},
        {"system_monitoring", {"which uptime || echo 'NO_UPTIME'"}},
        {"self_inspection",  {"test -f data/self_model.json || echo 'NO_SELF_MODEL'"}},
        {"memory_analysis",  {"test -d data/engrams/ || echo 'NO_ENGRAMS_DIR'"}}
    };

    // C++ template for specialist lobes — parameterized with %PLACEHOLDERS%
    static constexpr const char* SPECIALIST_TEMPLATE = R"CPP(
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>

using json = nlohmann::json;

// Auto-generated specialist lobe for domain: %DOMAIN%
// Created by BasalGanglia neurogenesis engine.

class %LOBE_NAME% {
public:
    %LOBE_NAME%(const std::string& pub_addr = "tcp://localhost:5555",
                const std::string& sub_addr = "tcp://localhost:5556")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect(pub_addr);
        sub.connect(sub_addr);
        routing::subscribe(sub, {"execution_request", "execution_result"});

        mkdir("./data", 0755);
        load_cache();

        std::cout << "[%LOBE_TAG%] Specialist lobe online for domain: %DOMAIN%" << std::endl;
    }

    void start() {
        auto last_report = std::chrono::steady_clock::now();

        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else {
                try {
                    std::string intent = j.value("intent", "");

                    if (intent == "execution_request") {
                        handle_request(j);
                    } else if (intent == "execution_result") {
                        handle_result(j);
                    }
                } catch (...) {}
            }

            // Publish report every 5 minutes
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_report).count();
            if (elapsed >= 5) {
                publish_report();
                last_report = now;
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    int handled = 0;
    int domain_success = 0;
    int domain_failure = 0;
    std::vector<std::string> cached_commands;
    static constexpr const char* CACHE_PATH = "./data/specialist_%DOMAIN%.json";

    // Domain keywords for filtering
    const std::vector<std::string> domain_keywords = {%DOMAIN_KEYWORDS%};

    bool is_my_domain(const std::string& cmd) {
        std::string lower = cmd;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (auto& kw : domain_keywords) {
            if (lower.find(kw) != std::string::npos) return true;
        }
        return false;
    }

    void handle_request(const json& j) {
        std::string cmd = j.value("command", "");
        if (!is_my_domain(cmd)) return;

        handled++;

        // Publish pre-validation advice
        json advice = {
            {"origin", "%LOBE_TAG%"},
            {"intent", "specialist_advice"},
            {"domain", "%DOMAIN%"},
            {"command", cmd},
            {"pre_validations", json::array({%PRE_VALIDATIONS%})},
            {"cached_alternatives", get_cached_alternatives()}
        };
        routing::publish(pub, advice);
    }

    void handle_result(const json& j) {
        std::string cmd = j.value("command", "");
        if (!is_my_domain(cmd)) return;

        std::string status = j.value("status", "");
        if (status == "success") {
            domain_success++;
            // Cache novel successful commands
            if (cmd.size() <= 200 && std::find(cached_commands.begin(), cached_commands.end(), cmd) == cached_commands.end()) {
                cached_commands.push_back(cmd);
                if (cached_commands.size() > 50) cached_commands.erase(cached_commands.begin());
                save_cache();
            }
        } else {
            domain_failure++;
        }
    }

    json get_cached_alternatives() {
        json alts = json::array();
        // Return up to 3 most recent cached successes
        int start = std::max(0, (int)cached_commands.size() - 3);
        for (int i = start; i < (int)cached_commands.size(); i++) {
            alts.push_back(cached_commands[i]);
        }
        return alts;
    }

    void publish_report() {
        int total = domain_success + domain_failure;
        float rate = total > 0 ? (float)domain_success / (float)total : 0.0f;

        json report = {
            {"origin", "%LOBE_TAG%"},
            {"intent", "specialist_report"},
            {"domain", "%DOMAIN%"},
            {"success_rate", rate},
            {"handled", handled},
            {"cached_count", (int)cached_commands.size()},
            {"total_tracked", total}
        };
        routing::publish(pub, report);

        std::cout << "[%LOBE_TAG%] Report: rate=" << (int)(rate * 100)
                  << "% handled=" << handled << " cached=" << cached_commands.size() << std::endl;
    }

    void load_cache() {
        std::ifstream f(CACHE_PATH);
        if (!f.is_open()) return;
        try {
            json doc;
            f >> doc;
            if (doc.contains("commands") && doc["commands"].is_array()) {
                for (auto& c : doc["commands"]) cached_commands.push_back(c.get<std::string>());
            }
        } catch (...) {}
    }

    void save_cache() {
        json doc = {{"commands", cached_commands}};
        std::ofstream f(CACHE_PATH);
        if (f.is_open()) f << doc.dump(2);
    }
};

int main() {
    %LOBE_NAME% lobe;
    lobe.start();
    return 0;
}
)CPP";

    // ──────────────────────────────────────────────────────────────────────
    // Meta-templates: specialist template evolution (generators of generators)
    // ──────────────────────────────────────────────────────────────────────
    struct MetaTemplate {
        int report_interval_min = 5;   // how often specialist reports
        int cache_size = 50;           // max cached commands
        int max_keywords = 10;         // keyword match limit
        float fitness = 0.5f;          // meta-fitness: how well its specialists perform
        int generation = 0;
        int specialists_spawned = 0;
        int specialists_survived = 0;  // survived past EVALUATION_WINDOW
    };

    std::vector<MetaTemplate> meta_population;
    static constexpr const char* META_TEMPLATE_PATH = "./data/meta_templates.json";

    void load_meta_templates() {
        std::ifstream f(META_TEMPLATE_PATH);
        if (!f.is_open()) {
            // Seed initial population with 3 variants
            meta_population.push_back({5, 50, 10, 0.5f, 0, 0, 0});   // default
            meta_population.push_back({3, 30, 8, 0.5f, 0, 0, 0});    // faster reports, smaller cache
            meta_population.push_back({10, 100, 15, 0.5f, 0, 0, 0}); // slower reports, larger cache
            return;
        }
        try {
            json doc;
            f >> doc;
            for (auto& t : doc) {
                MetaTemplate mt;
                mt.report_interval_min = t.value("report_interval", 5);
                mt.cache_size = t.value("cache_size", 50);
                mt.max_keywords = t.value("max_keywords", 10);
                mt.fitness = t.value("fitness", 0.5f);
                mt.generation = t.value("generation", 0);
                mt.specialists_spawned = t.value("spawned", 0);
                mt.specialists_survived = t.value("survived", 0);
                meta_population.push_back(mt);
            }
        } catch (...) {
            meta_population.push_back({5, 50, 10, 0.5f, 0, 0, 0});
        }
    }

    void save_meta_templates() {
        json doc = json::array();
        for (auto& mt : meta_population) {
            doc.push_back({
                {"report_interval", mt.report_interval_min},
                {"cache_size", mt.cache_size},
                {"max_keywords", mt.max_keywords},
                {"fitness", mt.fitness},
                {"generation", mt.generation},
                {"spawned", mt.specialists_spawned},
                {"survived", mt.specialists_survived}
            });
        }
        std::ofstream f(META_TEMPLATE_PATH);
        if (f.is_open()) f << doc.dump(2);
    }

    // Select best meta-template by fitness (roulette wheel)
    MetaTemplate& select_meta_template() {
        float total = 0;
        for (auto& mt : meta_population) total += std::max(0.01f, mt.fitness);
        float r = (float)(rand() % 10000) / 10000.0f * total;
        float accum = 0;
        for (auto& mt : meta_population) {
            accum += std::max(0.01f, mt.fitness);
            if (accum >= r) return mt;
        }
        return meta_population.back();
    }

    // Evolve meta-template population: crossover + mutation
    void evolve_meta_templates() {
        if (meta_population.size() < 2) return;

        // Sort by fitness
        std::sort(meta_population.begin(), meta_population.end(),
            [](const MetaTemplate& a, const MetaTemplate& b) { return a.fitness > b.fitness; });

        // Create offspring from top 2 via crossover
        auto& parent1 = meta_population[0];
        auto& parent2 = meta_population[1];

        MetaTemplate child;
        child.report_interval_min = (rand() % 2) ? parent1.report_interval_min : parent2.report_interval_min;
        child.cache_size = (rand() % 2) ? parent1.cache_size : parent2.cache_size;
        child.max_keywords = (rand() % 2) ? parent1.max_keywords : parent2.max_keywords;
        child.generation = std::max(parent1.generation, parent2.generation) + 1;

        // Mutation (20% chance per parameter)
        if (rand() % 5 == 0) child.report_interval_min = std::max(1, child.report_interval_min + (rand() % 5) - 2);
        if (rand() % 5 == 0) child.cache_size = std::max(10, child.cache_size + (rand() % 41) - 20);
        if (rand() % 5 == 0) child.max_keywords = std::max(3, child.max_keywords + (rand() % 5) - 2);

        // Replace weakest if population full
        if (meta_population.size() >= 6) {
            meta_population.back() = child;
        } else {
            meta_population.push_back(child);
        }

        save_meta_templates();
        std::cout << "[BASAL_GANGLIA] META-TEMPLATE EVOLVED: gen=" << child.generation
                  << " report=" << child.report_interval_min << "min"
                  << " cache=" << child.cache_size
                  << " keywords=" << child.max_keywords << std::endl;
    }

    // Build specialist parameters from domain name
    struct SpecialistParams {
        std::string domain;
        std::string lobe_name;    // e.g. "FileWriteSpecialist"
        std::string lobe_tag;     // e.g. "FILE_WRITE_SPECIALIST"
        std::string keywords;     // comma-separated quoted strings
        std::string pre_validations; // comma-separated quoted strings
    };

    SpecialistParams build_specialist_params(const std::string& domain) {
        SpecialistParams p;
        p.domain = domain;

        // Convert domain to CamelCase lobe name: "file_write" → "FileWriteSpecialist"
        std::string camel;
        bool capitalize = true;
        for (char c : domain) {
            if (c == '_') { capitalize = true; continue; }
            camel += capitalize ? (char)toupper(c) : c;
            capitalize = false;
        }
        p.lobe_name = camel + "Specialist";

        // Convert domain to UPPER_TAG: "file_write" → "FILE_WRITE_SPECIALIST"
        std::string upper = domain;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        p.lobe_tag = upper + "_SPECIALIST";

        // Extract keywords from classification_rules
        std::string kw_str;
        for (auto& rule : classification_rules) {
            if (rule.domain == domain) {
                for (auto& kw : rule.keywords) {
                    if (!kw_str.empty()) kw_str += ", ";
                    kw_str += "\"" + kw + "\"";
                }
                break;
            }
        }
        if (kw_str.empty()) kw_str = "\"" + domain + "\"";
        p.keywords = kw_str;

        // Get pre-validation commands
        std::string pv_str;
        if (domain_pre_validations.count(domain)) {
            for (auto& pv : domain_pre_validations[domain]) {
                if (!pv_str.empty()) pv_str += ", ";
                pv_str += "\"" + pv + "\"";
            }
        }
        p.pre_validations = pv_str;

        return p;
    }

    // Simple string replacement helper
    std::string str_replace(std::string str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
        return str;
    }

    // Lateral inhibition: when two specialists cover overlapping domains,
    // the one with fewer handled commands over N reports gets pruned.
    bool check_lateral_inhibition(const std::string& domain, const std::string& specialist,
                                  const ApoptosisTracker& tracker) {
        if (tracker.report_count < 3) return false;  // need enough data

        // Check if any related domain also has an active specialist
        auto affinity_it = domain_affinity.find(domain);
        if (affinity_it == domain_affinity.end()) return false;

        for (const auto& related : affinity_it->second) {
            if (!active_specialists.count(related)) continue;
            if (!apoptosis_trackers.count(related)) continue;

            auto& rival = apoptosis_trackers[related];
            if (rival.report_count < 3) continue;

            // Compare average handled per report
            float my_avg = (float)tracker.cumulative_handled / tracker.report_count;
            float rival_avg = (float)rival.cumulative_handled / rival.report_count;

            // If this specialist handles <50% of what the rival handles, prune it
            if (my_avg < rival_avg * 0.5f && rival_avg > 0) {
                std::cout << "[BASAL_GANGLIA] LATERAL INHIBITION: " << specialist
                          << " (domain=" << domain << " avg=" << (int)my_avg
                          << ") suppressed by " << related << " specialist (avg="
                          << (int)rival_avg << ")" << std::endl;

                json terminate = {
                    {"origin", "basal_ganglia"},
                    {"intent", "lobe_terminate"},
                    {"lobe_name", specialist},
                    {"domain", domain},
                    {"reason", "lateral_inhibition"}
                };
                routing::publish(pub, terminate);
                active_specialists.erase(domain);
                specialist_to_domain.erase(specialist);
                apoptosis_trackers.erase(domain);
                return true;
            }
        }
        return false;
    }

    void trigger_neurogenesis(const std::string& domain) {
        auto params = build_specialist_params(domain);

        // Substitute template
        std::string source = SPECIALIST_TEMPLATE;
        source = str_replace(source, "%DOMAIN%", params.domain);
        source = str_replace(source, "%LOBE_NAME%", params.lobe_name);
        source = str_replace(source, "%LOBE_TAG%", params.lobe_tag);
        source = str_replace(source, "%DOMAIN_KEYWORDS%", params.keywords);
        source = str_replace(source, "%PRE_VALIDATIONS%", params.pre_validations);

        // Ensure genesis directory exists
        mkdir(GENESIS_DIR, 0755);

        // Write source file
        std::string source_path = std::string(GENESIS_DIR) + domain + "_specialist.cpp";
        std::ofstream f(source_path);
        if (!f.is_open()) {
            std::cerr << "[BASAL_GANGLIA] NEUROGENESIS: Failed to write " << source_path << std::endl;
            return;
        }
        f << source;
        f.close();

        active_specialists.insert(domain);
        specialist_to_domain[params.lobe_tag] = domain;

        // Meta-template: apply selected variant's parameters to source
        auto& meta = select_meta_template();
        source = str_replace(source, "elapsed >= 5", "elapsed >= " + std::to_string(meta.report_interval_min));
        source = str_replace(source, "size() > 50", "size() > " + std::to_string(meta.cache_size));
        meta.specialists_spawned++;
        save_meta_templates();

        std::cout << "[BASAL_GANGLIA] NEUROGENESIS TRIGGERED: domain='" << domain
                  << "' → " << source_path
                  << " (meta: gen=" << meta.generation
                  << " report=" << meta.report_interval_min << "min)" << std::endl;

        // Publish genesis_request for MotorLobe to compile
        json req = {
            {"origin", "basal_ganglia"},
            {"intent", "genesis_request"},
            {"name", params.lobe_tag},
            {"source_path", source_path},
            {"output_path", "build/" + params.lobe_tag},
            {"domain", domain}
        };
        routing::publish(pub, req);

        std::cout << "[BASAL_GANGLIA] NEUROGENESIS: genesis_request published for "
                  << params.lobe_tag << std::endl;
    }

    void check_neurogenesis(const std::string& domain) {
        // Guard: already have a specialist or pending resolve for this domain
        if (active_specialists.count(domain)) return;
        if (pending_resolve.count(domain)) return;

        auto& d = self_model[domain];
        int total = d.success + d.failure;

        // Threshold: 20+ attempts, <30% success rate
        if (total < 20) return;
        if (d.actual_success_rate >= 0.30f) return;

        // Require sufficient stamina
        if (current_stamina < 50.0f) return;

        // Only trigger when SELF_MODIFY or MASTERY drive is active
        DriveLevel drive = get_current_drive();
        if (drive != DriveLevel::SELF_MODIFY && drive != DriveLevel::MASTERY) return;

        std::cout << "[BASAL_GANGLIA] AUTOPOIESIS: Domain '" << domain
                  << "' chronically failing (" << (int)(d.actual_success_rate * 100)
                  << "% over " << total << " attempts). Requesting PrimordialLoop resolution." << std::endl;

        // Collect recent failed commands for this domain
        json failed_cmds = json::array();
        for (const auto& cmd : d.example_commands) {
            failed_cmds.push_back(cmd);
        }

        // Ask PrimordialLoop to try Planner + Variation + LLM first
        std::string cid = "resolve_" + domain + "_" + std::to_string(std::time(nullptr));
        json req = {
            {"origin", "basal_ganglia"},
            {"intent", "domain_resolve_request"},
            {"cid", cid},
            {"domain", domain},
            {"failed_commands", failed_cmds},
            {"success_rate", d.actual_success_rate},
            {"total_attempts", total}
        };
        routing::publish(pub, req);

        pending_resolve.insert(domain);

        std::cout << "[BASAL_GANGLIA] AUTOPOIESIS: domain_resolve_request sent for '"
                  << domain << "' — neurogenesis deferred pending resolution." << std::endl;
    }

    // Get suggested commands: prefer genome templates, fall back to static commands
    json get_suggested_commands(const std::string& domain) {
        json cmds = json::array();

        // Try genome templates — weighted random selection (fitness × freshness)
        // Avoids always picking the same top-fitness command
        auto it = command_genome.find(domain);
        if (it != command_genome.end() && !it->second.empty()) {
            auto pool = it->second;

            // Fitness × freshness sampling — habituated commands get demoted
            // Note: we read history without modifying it (no push_back)
            auto& hist = domain_command_history[domain];
            float total_fit = 0.0f;
            for (auto& t : pool) {
                int repeats = 0;
                for (auto& h : hist) { if (h == t.cmd) repeats++; }
                float freshness = (repeats == 0) ? 1.0f : 1.0f / (1.0f + repeats);
                total_fit += std::max(0.05f, t.fitness * freshness);
            }

            std::set<int> picked;
            int picks = std::min((int)pool.size(), 4);
            for (int p = 0; p < picks && picked.size() < pool.size(); p++) {
                float r = (float)(rand() % 1000) / 1000.0f * total_fit;
                float acc = 0.0f;
                for (int i = 0; i < (int)pool.size(); i++) {
                    int reps = 0;
                    for (auto& h : hist) { if (h == pool[i].cmd) reps++; }
                    float fresh = (reps == 0) ? 1.0f : 1.0f / (1.0f + reps);
                    acc += std::max(0.05f, pool[i].fitness * fresh);
                    if (acc >= r && picked.find(i) == picked.end()) {
                        picked.insert(i);
                        cmds.push_back(pool[i].cmd);
                        break;
                    }
                }
            }
        }

        // Fall back to static domain commands if genome is empty
        if (cmds.empty() && domain_commands.count(domain)) {
            for (auto& c : domain_commands[domain]) cmds.push_back(c);
        }

        // BK-tree fuzzy retrieval: if genome gave few results, supplement with similar commands
        if (cmds.size() < 3 && !domain_commands[domain].empty()) {
            // Use first static command as anchor for fuzzy search
            auto fuzzy = bk_tree.nearest(domain_commands[domain][0], 3);
            for (auto& [entry, dist] : fuzzy) {
                if (dist <= 8 && entry.fitness >= 0.5f) {
                    // Avoid duplicates
                    bool dup = false;
                    for (auto& c : cmds) if (c == entry.cmd) { dup = true; break; }
                    if (!dup) cmds.push_back(entry.cmd);
                }
            }
        }

        // For concept-derived dynamic domains: pull example commands from concept clusters
        if (cmds.empty() && concept_space_available) {
            for (auto& [cname, cinfo] : concept_clusters) {
                if (cinfo.abstraction == domain ||
                    cinfo.abstraction.find(domain) != std::string::npos ||
                    domain.find(cinfo.abstraction) != std::string::npos) {
                    // Use the self_model example_commands which were populated from executions
                    if (self_model.count(domain) && !self_model[domain].example_commands.empty()) {
                        for (auto& c : self_model[domain].example_commands) {
                            cmds.push_back(c);
                        }
                    }
                    break;
                }
            }
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
                // Accept dynamic domains from concept space — don't skip unknown domains
                if (self_model.find(domain) == self_model.end()) {
                    // Register as dynamic domain
                    if (std::find(domains.begin(), domains.end(), domain) == domains.end()) {
                        domains.push_back(domain);
                    }
                    self_model[domain] = DomainState{};
                }
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
                d.attempts_since_substantive_success = data.value("attempts_since_substantive_success", 0);

                if (data.contains("example_commands") && data["example_commands"].is_array()) {
                    for (auto& cmd : data["example_commands"]) {
                        d.example_commands.push_back(cmd.get<std::string>());
                    }
                }
            }

            // Phase 9: Cap stale counters on load — prevents domains from being
            // permanently stuck from previous sessions. Max staleness = 25,
            // giving each domain a fair chance to be selected and recover.
            int capped = 0;
            for (auto& [dom, ds] : self_model) {
                if (ds.attempts_since_substantive_success > 25) {
                    ds.attempts_since_substantive_success = 25;
                    ds.cooldown_until = 0;
                    ds.consecutive_failures = std::min(ds.consecutive_failures, 2);
                    capped++;
                }
            }
            if (capped > 0) {
                std::cout << "[BASAL_GANGLIA] Capped staleness for " << capped
                          << " domains (max=25 on load)" << std::endl;
            }

            std::cout << "[BASAL_GANGLIA] Loaded self-model from " << self_model_path << std::endl;
        } catch (...) {
            std::cout << "[BASAL_GANGLIA] Could not parse self-model, using defaults." << std::endl;
        }

        // Phase 4: Load command genome
        load_genome();

        // Build BK-tree from genome (successful commands = fitness > 0.5)
        build_bk_tree();
    }

    // ──────────────────────────────────────────────────────────────────────
    // BK-tree operations: build, validate, compact, fuzzy retrieve
    // ──────────────────────────────────────────────────────────────────────

    void build_bk_tree() {
        bk_tree.clear();
        int inserted = 0;
        for (auto& [domain, templates] : command_genome) {
            for (auto& t : templates) {
                if (t.fitness >= 0.4f) {  // only index commands with reasonable fitness
                    bk_tree.insert(t.cmd, t.fitness, domain);
                    inserted++;
                }
            }
        }
        // Also index static domain_commands as known-good baselines
        for (auto& [domain, cmds] : domain_commands) {
            for (auto& cmd : cmds) {
                bk_tree.insert(cmd, 0.7f, domain);
                inserted++;
            }
        }
        std::cout << "[BASAL_GANGLIA] BK-tree built: " << bk_tree.size()
                  << " unique commands indexed (" << inserted << " attempted)" << std::endl;
    }

    // Validate a command against the BK-tree
    // Returns: {valid, distance, nearest_cmd, reason}
    struct ValidationResult {
        bool valid = false;
        int distance = 9999;
        std::string nearest_cmd;
        std::string reason;
    };

    ValidationResult validate_command_bk(const std::string& cmd) {
        ValidationResult result;

        if (cmd.empty()) {
            result.reason = "empty_command";
            return result;
        }

        // Hardcoded rejection patterns (always invalid regardless of distance)
        static const std::vector<std::string> blacklist = {
            "/path/to/", "REAL_BASH_CMD", "your_", "example_",
            "<file>", "<path>", "<command>", "INSERT_", "TODO_",
            "placeholder", "${VARIABLE}"
        };
        for (auto& pat : blacklist) {
            if (cmd.find(pat) != std::string::npos) {
                result.reason = "blacklisted_pattern:" + pat;
                return result;
            }
        }

        // If tree is empty (cold start), allow everything
        if (bk_tree.size() == 0) {
            result.valid = true;
            result.distance = 0;
            result.reason = "cold_start_allow";
            return result;
        }

        // Find minimum distance to any known command
        int min_dist = bk_tree.min_distance(cmd);
        result.distance = min_dist;

        // Adaptive threshold: max(4, 20% of command length)
        // Short commands (ls, ps) need near-exact; long commands allow more variance
        int threshold = std::max(4, (int)(cmd.size() * 0.20));

        // Cap at 15 — anything beyond that is definitely alien
        threshold = std::min(threshold, 15);

        if (min_dist <= threshold) {
            result.valid = true;
            result.reason = "within_threshold";
        } else {
            result.reason = "too_distant:" + std::to_string(min_dist) + ">" + std::to_string(threshold);
        }

        // Get nearest for context
        auto nearest = bk_tree.nearest(cmd, 1);
        if (!nearest.empty()) {
            result.nearest_cmd = nearest[0].first.cmd;
        }

        return result;
    }

    // Handle validate_command requests from FrontalExecutive
    void handle_validate_command(const json& j) {
        std::string cmd = j.value("command", "");
        std::string cid = j.value("cid", "");

        auto result = validate_command_bk(cmd);

        json response = {
            {"origin", "basal_ganglia"},
            {"intent", "command_validated"},
            {"cid", cid},
            {"command", cmd},
            {"valid", result.valid},
            {"distance", result.distance},
            {"nearest_cmd", result.nearest_cmd},
            {"reason", result.reason}
        };

        // If invalid, suggest nearest alternative
        if (!result.valid && !result.nearest_cmd.empty()) {
            response["suggestion"] = result.nearest_cmd;
        }

        // Also provide top-3 nearest successful commands as alternatives
        auto alternatives = bk_tree.nearest(cmd, 3);
        json alts = json::array();
        for (auto& [entry, dist] : alternatives) {
            if (entry.fitness >= 0.5f) {
                alts.push_back({{"cmd", entry.cmd}, {"fitness", entry.fitness},
                                {"distance", dist}, {"domain", entry.domain}});
            }
        }
        response["alternatives"] = alts;

        routing::publish(pub, response);

        if (!result.valid) {
            std::cout << "[BASAL_GANGLIA] BK-VALIDATION REJECTED: dist=" << result.distance
                      << " reason=" << result.reason
                      << " cmd='" << cmd.substr(0, 60) << "'"
                      << " nearest='" << result.nearest_cmd.substr(0, 60) << "'" << std::endl;
        }
    }

    // Genome compaction: cluster near-duplicate commands, keep only the fittest
    void compact_genome() {
        int total_removed = 0;

        for (auto& [domain, templates] : command_genome) {
            if (templates.size() < 5) continue;

            // Sort by fitness descending
            std::sort(templates.begin(), templates.end(),
                [](const CommandTemplate& a, const CommandTemplate& b) {
                    return a.fitness > b.fitness;
                });

            std::vector<CommandTemplate> compacted;
            std::vector<bool> merged(templates.size(), false);

            for (size_t i = 0; i < templates.size(); i++) {
                if (merged[i]) continue;
                compacted.push_back(templates[i]);

                // Absorb any near-duplicates (distance <= 3)
                for (size_t j = i + 1; j < templates.size(); j++) {
                    if (merged[j]) continue;
                    int dist = BKTree::levenshtein(templates[i].cmd, templates[j].cmd);
                    if (dist <= 3) {
                        merged[j] = true;
                        total_removed++;
                    }
                }
            }

            templates = std::move(compacted);
        }

        if (total_removed > 0) {
            save_genome();
            build_bk_tree();  // rebuild after compaction
            std::cout << "[BASAL_GANGLIA] GENOME COMPACTION: removed " << total_removed
                      << " near-duplicate templates" << std::endl;
        }
    }

    // Fuzzy genome retrieval: find commands similar to a query, across all domains
    json fuzzy_retrieve(const std::string& query, int k = 5) {
        auto results = bk_tree.nearest(query, k);
        json arr = json::array();
        for (auto& [entry, dist] : results) {
            arr.push_back({{"cmd", entry.cmd}, {"fitness", entry.fitness},
                           {"distance", dist}, {"domain", entry.domain}});
        }
        return arr;
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
                {"cooldown_until",        d.cooldown_until},
                {"attempts_since_substantive_success", d.attempts_since_substantive_success}
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
