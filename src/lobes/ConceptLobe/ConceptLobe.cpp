// ConceptLobe — Learned Internal Representations
//
// Replaces hardcoded domain classification with emergent concept clusters.
// Each execution is embedded into a 768-dim vector space (Nomic embeddings).
// Similar operations cluster together. The system discovers its own abstractions.
//
// Data flow:
//   execution_result → embed command+output → insert into concept space →
//   online clustering → concept_update broadcast
//
// Queries:
//   concept_query → find nearest concepts → concept_response with
//   abstracted operation, similar commands, transfer predictions
//
// Persistence: data/concept_space.jsonl (append-only nodes)
//              data/concept_clusters.json (cluster state)

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <thread>
#include <mutex>
#include <filesystem>
#include <sstream>
#include <regex>

using json = nlohmann::json;
namespace fs = std::filesystem;

// A single point in concept space — one observed action and its outcome
struct ConceptNode {
    std::string id;
    std::string command;
    std::string output_summary;       // first 200 chars of output
    bool success;
    std::string cluster_id;           // which cluster this belongs to
    std::vector<float> embedding;     // 768-dim
    std::chrono::steady_clock::time_point created_at;
    int observation_count = 1;        // how many times this exact command was seen
};

// An emergent concept cluster — discovered from embedding similarity
struct ConceptCluster {
    std::string id;
    std::vector<float> centroid;       // mean embedding of members
    std::vector<std::string> members;  // ConceptNode ids
    std::string abstraction;           // inferred abstract operation name
    std::string pattern;               // inferred command pattern (e.g., "cat <path>")
    float coherence = 0.0f;            // mean intra-cluster similarity
    float success_rate = 0.0f;
    int total_observations = 0;
    std::vector<std::string> parameterised_slots;  // variable parts across members
};

// A meta-concept: a compositional abstraction over clusters
// Level 1 = base clusters (read_content, search_content)
// Level 2+ = compositions (information_retrieval = read_content + search_content)
struct MetaConcept {
    std::string id;
    int level = 2;                              // 2 = first meta-level, 3+ = higher
    std::vector<std::string> children;          // cluster or meta-concept ids
    std::string abstraction;                    // inferred composite name
    std::vector<float> centroid;                // mean of child centroids
    float coherence = 0.0f;
    int co_occurrence_count = 0;                // times children appeared together in a goal
    double last_updated = 0.0;
};

// Tracks which clusters activated during a single goal
struct GoalSequence {
    std::string cid;
    std::vector<std::string> cluster_ids;       // clusters activated, in order
    std::vector<bool> success_flags;            // success/failure per activation
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point last_activity;
};

// A semantic role within a composite operation
struct SemanticRole {
    std::string cluster_id;
    std::string role;           // "precondition", "action", "verification", "cleanup"
    int position;               // index in the sequence
};

// A composite operation: a meaningful sequence of clusters
// e.g., "backup" = [search_filesystem(precondition) → read_content(action) → copy_resource(action)]
struct CompositeOperation {
    std::string id;
    std::string name;                       // inferred composite name
    std::vector<SemanticRole> roles;        // ordered semantic roles
    std::string sequence_key;               // canonical cluster sequence hash
    int observation_count = 0;
    float success_rate = 0.0f;
    double last_seen = 0.0;
};

class ConceptLobe {
public:
    ConceptLobe(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub),
          data_dir("./data/") {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");

        routing::subscribe(sub, {
            "execution_result", "concept_query", "embedding_result",
            "primordial_ready", "operators_synced"
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        load_state();

        std::cout << "[CONCEPT] ConceptLobe online. "
                  << nodes.size() << " nodes, "
                  << clusters.size() << " clusters loaded." << std::endl;
    }

    void start() {
        auto last_maintenance = std::chrono::steady_clock::now();

        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);

            if (!j.is_null()) {
                try {
                    std::string intent = j.value("intent", "");

                    if (intent == "execution_result") {
                        handle_execution_result(j);
                    } else if (intent == "concept_query") {
                        handle_concept_query(j);
                    } else if (intent == "embedding_result") {
                        handle_embedding_result(j);
                    } else if (intent == "primordial_ready" || intent == "operators_synced") {
                        // Could trigger re-clustering of new operators
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[CONCEPT] Error: " << e.what() << std::endl;
                }
            }

            // Periodic maintenance: merge/split clusters, persist state
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::minutes>(now - last_maintenance).count() >= 2) {
                maintain_clusters();
                save_state();
                publish_concept_update();
                last_maintenance = now;
            }

            if (j.is_null()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    std::string data_dir;

    std::map<std::string, ConceptNode> nodes;
    std::map<std::string, ConceptCluster> clusters;

    // Pending embedding requests: cid → partial ConceptNode
    struct PendingEmbed {
        ConceptNode node;
        std::string goal_cid;          // original goal cid for sequence tracking
        std::chrono::steady_clock::time_point requested_at;
    };
    std::map<std::string, PendingEmbed> pending_embeds;

    static constexpr float CLUSTER_THRESHOLD = 0.72f;  // cosine similarity to join a cluster
    static constexpr float MERGE_THRESHOLD = 0.80f;    // clusters closer than this get merged
    static constexpr float SPLIT_THRESHOLD = 0.55f;    // clusters less coherent than this get split
    static constexpr size_t MAX_NODES = 5000;
    static constexpr size_t MIN_CLUSTER_SIZE = 3;

    // ─── Abstraction Hierarchy ──────────────────────────────────
    std::map<std::string, MetaConcept> meta_concepts;
    std::map<std::string, GoalSequence> active_goals;  // cid → sequence
    // Co-occurrence matrix: "clusterA|clusterB" → count (A < B lexicographically)
    std::map<std::string, int> co_occurrence;
    int total_completed_goals = 0;

    static constexpr int MIN_COOCCURRENCE = 4;         // times two clusters must co-occur
    static constexpr float MIN_COOCCURRENCE_RATIO = 0.15f; // fraction of goals containing the pair
    static constexpr int GOAL_TIMEOUT_SECS = 120;      // goal considered complete after inactivity
    static constexpr int MAX_META_LEVEL = 4;            // max hierarchy depth

    // ─── Semantic Compositionality ──────────────────────────────
    std::map<std::string, CompositeOperation> composite_ops;
    // sequence_key → observation count (for detecting recurring sequences)
    std::map<std::string, int> sequence_counts;
    // sequence_key → ordered cluster ids
    std::map<std::string, std::vector<std::string>> sequence_patterns;
    // sequence_key → success counts
    std::map<std::string, int> sequence_successes;

    static constexpr int MIN_SEQUENCE_OBS = 3;         // min times a sequence must recur

    // --- Execution result handling ---

    void handle_execution_result(const json& data) {
        std::string cmd = data.value("command", "");
        std::string output = data.value("output", "");
        std::string cid = data.value("cid", "");
        bool success = data.value("status", "") == "success";
        std::string mode = data.value("mode", "reality");

        // Only learn from reality executions
        if (mode != "reality" || cmd.empty()) return;

        // Check if we already have this exact command
        std::string cmd_hash = hash_command(cmd);
        if (nodes.count(cmd_hash)) {
            nodes[cmd_hash].observation_count++;
            if (success != nodes[cmd_hash].success) {
                nodes[cmd_hash].success = success;
            }
            // Still track goal sequence even for known commands
            if (!cid.empty() && !nodes[cmd_hash].cluster_id.empty()) {
                record_goal_activation(cid, nodes[cmd_hash].cluster_id, success);
            }
            return;
        }

        // Build the text to embed: command + truncated output + success/failure context
        std::string embed_text = build_embed_text(cmd, output, success);

        // Create pending node
        ConceptNode node;
        node.id = cmd_hash;
        node.command = cmd;
        node.output_summary = output.substr(0, 200);
        node.success = success;
        node.created_at = std::chrono::steady_clock::now();

        // Request embedding
        std::string embed_cid = "concept_embed_" + cmd_hash;
        json req = {
            {"cid", embed_cid}, {"origin", "concept_lobe"},
            {"intent", "embedding_request"}, {"text", embed_text}
        };

        pending_embeds[embed_cid] = {node, cid, std::chrono::steady_clock::now()};
        routing::publish(pub, req);
    }

    void handle_embedding_result(const json& data) {
        std::string cid = data.value("cid", "");

        // Only process our own embedding requests
        if (cid.find("concept_embed_") != 0 && cid.find("concept_query_") != 0) return;

        auto vec = data.value("embedding", std::vector<float>{});
        if (vec.empty()) return;

        // Normalise the vector (L2)
        normalise(vec);

        if (cid.find("concept_embed_") == 0) {
            // This is a node embedding response
            auto it = pending_embeds.find(cid);
            if (it == pending_embeds.end()) return;

            ConceptNode node = it->second.node;
            std::string goal_cid = it->second.goal_cid;
            node.embedding = vec;
            pending_embeds.erase(it);

            // Insert into concept space and cluster
            insert_node(node);

            // Track goal sequence for abstraction hierarchy
            if (!goal_cid.empty() && !node.cluster_id.empty()) {
                record_goal_activation(goal_cid, node.cluster_id);
            }

        } else if (cid.find("concept_query_") == 0) {
            // This is a query embedding response — handled in pending_queries
            handle_query_embedding(cid, vec);
        }
    }

    // --- Core concept space operations ---

    void insert_node(ConceptNode& node) {
        // Find nearest cluster
        std::string best_cluster;
        float best_sim = -1.0f;

        for (auto& [cid, cluster] : clusters) {
            if (cluster.centroid.empty()) continue;
            float sim = cosine_similarity(node.embedding, cluster.centroid);
            if (sim > best_sim) {
                best_sim = sim;
                best_cluster = cid;
            }
        }

        if (best_sim >= CLUSTER_THRESHOLD && !best_cluster.empty()) {
            // Join existing cluster
            node.cluster_id = best_cluster;
            clusters[best_cluster].members.push_back(node.id);
            update_centroid(clusters[best_cluster], node.embedding);
            update_cluster_stats(clusters[best_cluster]);

            std::cout << "[CONCEPT] '" << truncate(node.command, 40)
                      << "' → cluster '" << clusters[best_cluster].abstraction
                      << "' (sim=" << best_sim << ")" << std::endl;
        } else {
            // Create new cluster
            std::string cluster_id = "c_" + std::to_string(clusters.size()) + "_" + node.id.substr(0, 8);
            ConceptCluster cluster;
            cluster.id = cluster_id;
            cluster.centroid = node.embedding;
            cluster.members.push_back(node.id);
            cluster.abstraction = infer_abstraction({node.command});
            cluster.pattern = extract_pattern({node.command});
            cluster.coherence = 1.0f;
            cluster.success_rate = node.success ? 1.0f : 0.0f;
            cluster.total_observations = node.observation_count;

            node.cluster_id = cluster_id;
            clusters[cluster_id] = cluster;

            std::cout << "[CONCEPT] New concept: '" << cluster.abstraction
                      << "' from '" << truncate(node.command, 40) << "'" << std::endl;
        }

        nodes[node.id] = node;

        // Enforce capacity
        if (nodes.size() > MAX_NODES) {
            prune_oldest_nodes();
        }
    }

    // --- Concept queries ---

    struct PendingQuery {
        std::string original_cid;
        std::string query_text;
        std::chrono::steady_clock::time_point requested_at;
    };
    std::map<std::string, PendingQuery> pending_queries;

    void handle_concept_query(const json& data) {
        std::string query = data.value("query", "");
        std::string cid = data.value("cid", "");

        if (query.empty()) return;

        // Embed the query
        std::string embed_cid = "concept_query_" + cid;
        json req = {
            {"cid", embed_cid}, {"origin", "concept_lobe"},
            {"intent", "embedding_request"}, {"text", query}
        };

        pending_queries[embed_cid] = {cid, query, std::chrono::steady_clock::now()};
        routing::publish(pub, req);
    }

    void handle_query_embedding(const std::string& embed_cid, const std::vector<float>& query_vec) {
        auto it = pending_queries.find(embed_cid);
        if (it == pending_queries.end()) return;

        std::string original_cid = it->second.original_cid;
        pending_queries.erase(it);

        // Find top-k nearest clusters
        struct ClusterMatch { std::string id; float sim; };
        std::vector<ClusterMatch> matches;

        for (auto& [cid, cluster] : clusters) {
            if (cluster.centroid.empty()) continue;
            float sim = cosine_similarity(query_vec, cluster.centroid);
            matches.push_back({cid, sim});
        }

        std::sort(matches.begin(), matches.end(),
                  [](auto& a, auto& b) { return a.sim > b.sim; });

        // Build response with top 5 concepts
        json concepts = json::array();
        for (size_t i = 0; i < std::min((size_t)5, matches.size()); ++i) {
            auto& cluster = clusters[matches[i].id];
            json c = {
                {"concept", cluster.abstraction},
                {"pattern", cluster.pattern},
                {"similarity", matches[i].sim},
                {"success_rate", cluster.success_rate},
                {"observations", cluster.total_observations},
                {"member_count", cluster.members.size()},
                {"example_commands", get_example_commands(cluster, 3)}
            };
            if (!cluster.parameterised_slots.empty()) {
                c["parameters"] = cluster.parameterised_slots;
            }
            concepts.push_back(c);
        }

        // Find meta-concepts that contain any of the matched clusters
        json related_hierarchy = json::array();
        for (size_t i = 0; i < std::min((size_t)5, matches.size()); ++i) {
            for (auto& [mid, mc] : meta_concepts) {
                auto& ch = mc.children;
                if (std::find(ch.begin(), ch.end(), matches[i].id) != ch.end()) {
                    related_hierarchy.push_back({
                        {"meta_concept", mc.abstraction},
                        {"level", mc.level},
                        {"children_count", mc.children.size()}
                    });
                }
            }
        }

        json resp = {
            {"cid", original_cid}, {"origin", "concept_lobe"},
            {"intent", "concept_response"}, {"concepts", concepts},
            {"total_clusters", clusters.size()}, {"total_nodes", nodes.size()},
            {"hierarchy", related_hierarchy}
        };
        routing::publish(pub, resp);
    }

    // --- Cluster maintenance ---

    void maintain_clusters() {
        // Prune expired pending requests
        auto now = std::chrono::steady_clock::now();
        for (auto it = pending_embeds.begin(); it != pending_embeds.end(); ) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.requested_at).count();
            if (age > 30) it = pending_embeds.erase(it); else ++it;
        }
        for (auto it = pending_queries.begin(); it != pending_queries.end(); ) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.requested_at).count();
            if (age > 30) it = pending_queries.erase(it); else ++it;
        }

        // Recalculate coherence for all clusters
        for (auto& [cid, cluster] : clusters) {
            recalculate_coherence(cluster);
            update_cluster_stats(cluster);
        }

        // Merge similar clusters
        merge_similar_clusters();

        // Split incoherent clusters
        split_incoherent_clusters();

        // Remove empty clusters
        for (auto it = clusters.begin(); it != clusters.end(); ) {
            if (it->second.members.empty()) {
                it = clusters.erase(it);
            } else {
                ++it;
            }
        }

        // Finalize stale goals, compose abstractions, discover composites
        finalize_stale_goals();
        compose_abstractions();
        discover_composites();

        // Re-infer abstractions for clusters that grew
        for (auto& [cid, cluster] : clusters) {
            std::vector<std::string> cmds;
            for (auto& nid : cluster.members) {
                if (nodes.count(nid)) cmds.push_back(nodes[nid].command);
            }
            if (cmds.size() >= MIN_CLUSTER_SIZE) {
                cluster.abstraction = infer_abstraction(cmds);
                cluster.pattern = extract_pattern(cmds);
                cluster.parameterised_slots = find_parameters(cmds);
            }
        }

        std::cout << "[CONCEPT] Maintenance: " << nodes.size() << " nodes, "
                  << clusters.size() << " clusters, "
                  << meta_concepts.size() << " meta-concepts, "
                  << total_completed_goals << " goals tracked" << std::endl;
    }

    void merge_similar_clusters() {
        std::vector<std::pair<std::string, std::string>> to_merge;

        std::vector<std::string> cids;
        for (auto& [cid, _] : clusters) cids.push_back(cid);

        for (size_t i = 0; i < cids.size(); ++i) {
            for (size_t j = i + 1; j < cids.size(); ++j) {
                auto& a = clusters[cids[i]];
                auto& b = clusters[cids[j]];
                if (a.centroid.empty() || b.centroid.empty()) continue;

                float sim = cosine_similarity(a.centroid, b.centroid);
                if (sim >= MERGE_THRESHOLD) {
                    to_merge.push_back({cids[i], cids[j]});
                }
            }
        }

        for (auto& [keep_id, merge_id] : to_merge) {
            if (!clusters.count(keep_id) || !clusters.count(merge_id)) continue;

            auto& keep = clusters[keep_id];
            auto& merge = clusters[merge_id];

            // Move all members to the surviving cluster
            for (auto& nid : merge.members) {
                keep.members.push_back(nid);
                if (nodes.count(nid)) nodes[nid].cluster_id = keep_id;
            }

            // Recalculate centroid
            recalculate_centroid(keep);

            std::cout << "[CONCEPT] Merged '" << merge.abstraction
                      << "' into '" << keep.abstraction << "'" << std::endl;

            clusters.erase(merge_id);
        }
    }

    void split_incoherent_clusters() {
        std::vector<std::string> to_split;

        for (auto& [cid, cluster] : clusters) {
            if (cluster.members.size() >= MIN_CLUSTER_SIZE * 2 &&
                cluster.coherence < SPLIT_THRESHOLD) {
                to_split.push_back(cid);
            }
        }

        for (auto& cid : to_split) {
            if (!clusters.count(cid)) continue;
            auto& cluster = clusters[cid];

            // Simple 2-means split: pick two furthest members as seeds
            if (cluster.members.size() < 4) continue;

            // Find the member most distant from centroid
            std::string furthest;
            float max_dist = -1.0f;
            for (auto& nid : cluster.members) {
                if (!nodes.count(nid) || nodes[nid].embedding.empty()) continue;
                float sim = cosine_similarity(nodes[nid].embedding, cluster.centroid);
                float dist = 1.0f - sim;
                if (dist > max_dist) { max_dist = dist; furthest = nid; }
            }

            if (furthest.empty()) continue;

            // Create new cluster with the far-away members
            std::string new_id = "c_" + std::to_string(clusters.size()) + "_split";
            ConceptCluster new_cluster;
            new_cluster.id = new_id;
            new_cluster.centroid = nodes[furthest].embedding;

            // Assign each member to whichever centroid is closer
            std::vector<std::string> keep_members, move_members;
            for (auto& nid : cluster.members) {
                if (!nodes.count(nid) || nodes[nid].embedding.empty()) {
                    keep_members.push_back(nid);
                    continue;
                }
                float sim_old = cosine_similarity(nodes[nid].embedding, cluster.centroid);
                float sim_new = cosine_similarity(nodes[nid].embedding, new_cluster.centroid);
                if (sim_new > sim_old) {
                    move_members.push_back(nid);
                    nodes[nid].cluster_id = new_id;
                } else {
                    keep_members.push_back(nid);
                }
            }

            if (move_members.size() >= MIN_CLUSTER_SIZE && keep_members.size() >= MIN_CLUSTER_SIZE) {
                cluster.members = keep_members;
                new_cluster.members = move_members;
                recalculate_centroid(cluster);
                recalculate_centroid(new_cluster);

                // Infer abstractions for both
                std::vector<std::string> cmds;
                for (auto& nid : new_cluster.members) {
                    if (nodes.count(nid)) cmds.push_back(nodes[nid].command);
                }
                new_cluster.abstraction = infer_abstraction(cmds);
                new_cluster.pattern = extract_pattern(cmds);

                std::cout << "[CONCEPT] Split: '" << cluster.abstraction
                          << "' → new concept '" << new_cluster.abstraction << "'" << std::endl;

                clusters[new_id] = new_cluster;
            }
        }
    }

    // --- Abstraction inference ---

    // Infer what abstract operation a set of commands represents
    std::string infer_abstraction(const std::vector<std::string>& commands) {
        if (commands.empty()) return "unknown";
        if (commands.size() == 1) return extract_verb(commands[0]);

        // Find the common leading token(s)
        std::map<std::string, int> verb_counts;
        for (auto& cmd : commands) {
            std::string verb = extract_verb(cmd);
            verb_counts[verb]++;
        }

        // Find dominant verb
        std::string dominant_verb;
        int max_count = 0;
        for (auto& [verb, count] : verb_counts) {
            if (count > max_count) { max_count = count; dominant_verb = verb; }
        }

        // Map common verbs to abstract operations
        static const std::map<std::string, std::string> verb_abstractions = {
            {"cat", "read_content"}, {"head", "read_content"}, {"tail", "read_content"},
            {"less", "read_content"}, {"more", "read_content"},
            {"echo", "write_output"}, {"tee", "write_output"}, {"printf", "write_output"},
            {"cp", "copy_resource"}, {"mv", "move_resource"}, {"ln", "link_resource"},
            {"rm", "remove_resource"}, {"unlink", "remove_resource"},
            {"mkdir", "create_structure"}, {"touch", "create_resource"},
            {"find", "search_filesystem"}, {"grep", "search_content"},
            {"rg", "search_content"}, {"fd", "search_filesystem"},
            {"locate", "search_filesystem"},
            {"ps", "inspect_process"}, {"pgrep", "inspect_process"},
            {"top", "inspect_process"}, {"kill", "signal_process"},
            {"git", "version_control"}, {"svn", "version_control"},
            {"cmake", "build_system"}, {"make", "build_system"},
            {"gcc", "compile"}, {"g++", "compile"}, {"clang", "compile"},
            {"python3", "interpret"}, {"python", "interpret"}, {"node", "interpret"},
            {"curl", "network_transfer"}, {"wget", "network_transfer"},
            {"ssh", "remote_access"}, {"scp", "remote_transfer"},
            {"ss", "network_inspect"}, {"netstat", "network_inspect"},
            {"ping", "network_probe"}, {"nc", "network_probe"},
            {"sed", "transform_text"}, {"awk", "transform_text"},
            {"sort", "transform_text"}, {"uniq", "transform_text"},
            {"wc", "measure_content"}, {"stat", "measure_resource"},
            {"du", "measure_resource"}, {"df", "measure_filesystem"},
            {"uptime", "system_status"}, {"free", "system_status"},
            {"uname", "system_identity"}, {"hostname", "system_identity"},
            {"chmod", "modify_permissions"}, {"chown", "modify_ownership"},
            {"diff", "compare_content"}, {"cmp", "compare_content"},
            {"tar", "archive"}, {"zip", "archive"}, {"gzip", "compress"},
            {"file", "identify_type"}, {"md5sum", "verify_integrity"},
            {"sha256sum", "verify_integrity"}
        };

        if (verb_abstractions.count(dominant_verb)) {
            std::string base = verb_abstractions.at(dominant_verb);
            // If there's a common target type, append it
            std::string target = find_common_target_type(commands);
            if (!target.empty()) return base + ":" + target;
            return base;
        }

        // Fallback: use the dominant verb with a count suffix
        float ratio = (float)max_count / commands.size();
        if (ratio >= 0.6f) return dominant_verb + "_operation";

        return "mixed_operation";
    }

    std::string extract_verb(const std::string& cmd) {
        // Handle pipes — use first command's verb
        std::string first_cmd = cmd;
        auto pipe_pos = cmd.find('|');
        if (pipe_pos != std::string::npos) first_cmd = cmd.substr(0, pipe_pos);

        // Handle env vars, sudo, etc.
        std::istringstream iss(first_cmd);
        std::string token;
        while (iss >> token) {
            // Skip env assignments (X=Y) and prefixes
            if (token.find('=') != std::string::npos && token.find('=') > 0) continue;
            if (token == "sudo" || token == "env" || token == "nice" || token == "nohup") continue;
            // Extract basename from path
            auto slash = token.rfind('/');
            if (slash != std::string::npos) token = token.substr(slash + 1);
            return token;
        }
        return "unknown";
    }

    std::string find_common_target_type(const std::vector<std::string>& commands) {
        std::map<std::string, int> types;
        for (auto& cmd : commands) {
            if (cmd.find(".cpp") != std::string::npos || cmd.find(".hpp") != std::string::npos)
                types["source"]++;
            else if (cmd.find(".json") != std::string::npos || cmd.find(".jsonl") != std::string::npos)
                types["data"]++;
            else if (cmd.find(".sh") != std::string::npos || cmd.find("bash") != std::string::npos)
                types["script"]++;
            else if (cmd.find("/proc/") != std::string::npos || cmd.find("pid") != std::string::npos)
                types["process"]++;
            else if (cmd.find("src/") != std::string::npos)
                types["source"]++;
            else if (cmd.find("data/") != std::string::npos)
                types["data"]++;
            else if (cmd.find("build/") != std::string::npos)
                types["build"]++;
        }

        std::string best;
        int best_count = 0;
        for (auto& [type, count] : types) {
            if (count > best_count) { best_count = count; best = type; }
        }
        if (best_count >= (int)(commands.size() * 0.5)) return best;
        return "";
    }

    // Extract a parameterised pattern from a set of commands
    // e.g., ["cat /etc/hostname", "cat /etc/os-release"] → "cat <path>"
    std::string extract_pattern(const std::vector<std::string>& commands) {
        if (commands.empty()) return "";
        if (commands.size() == 1) return commands[0];

        // Tokenise all commands
        std::vector<std::vector<std::string>> tokenised;
        for (auto& cmd : commands) {
            std::istringstream iss(cmd);
            std::vector<std::string> tokens;
            std::string t;
            while (iss >> t) tokens.push_back(t);
            tokenised.push_back(tokens);
        }

        // Find the min token count
        size_t min_len = SIZE_MAX;
        for (auto& t : tokenised) min_len = std::min(min_len, t.size());
        if (min_len == 0) return commands[0];

        // Build pattern: keep tokens that are identical across all commands,
        // replace varying tokens with <param>
        std::vector<std::string> pattern;
        for (size_t i = 0; i < min_len; ++i) {
            std::set<std::string> unique;
            for (auto& t : tokenised) unique.insert(t[i]);

            if (unique.size() == 1) {
                pattern.push_back(*unique.begin());
            } else {
                // Infer parameter type
                bool all_paths = true;
                for (auto& v : unique) {
                    if (v.find('/') == std::string::npos && v.find('.') == std::string::npos) {
                        all_paths = false;
                        break;
                    }
                }
                pattern.push_back(all_paths ? "<path>" : "<arg>");
            }
        }

        std::string result;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (i > 0) result += " ";
            result += pattern[i];
        }
        return result;
    }

    // Find which parts of commands vary (the "parameters")
    std::vector<std::string> find_parameters(const std::vector<std::string>& commands) {
        std::vector<std::string> params;
        if (commands.size() < 2) return params;

        std::vector<std::vector<std::string>> tokenised;
        for (auto& cmd : commands) {
            std::istringstream iss(cmd);
            std::vector<std::string> tokens;
            std::string t;
            while (iss >> t) tokens.push_back(t);
            tokenised.push_back(tokens);
        }

        size_t min_len = SIZE_MAX;
        for (auto& t : tokenised) min_len = std::min(min_len, t.size());

        for (size_t i = 0; i < min_len; ++i) {
            std::set<std::string> unique;
            for (auto& t : tokenised) unique.insert(t[i]);
            if (unique.size() > 1) {
                // Collect all observed values for this position
                for (auto& v : unique) {
                    if (std::find(params.begin(), params.end(), v) == params.end()) {
                        params.push_back(v);
                    }
                }
            }
        }
        return params;
    }

    // --- Vector operations ---

    float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.size() != b.size() || a.empty()) return 0.0f;
        float dot = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) dot += a[i] * b[i];
        return dot;  // Vectors are already L2-normalised
    }

    void normalise(std::vector<float>& v) {
        float norm = 0.0f;
        for (float x : v) norm += x * x;
        norm = std::sqrt(norm);
        if (norm > 1e-8f) {
            for (float& x : v) x /= norm;
        }
    }

    void update_centroid(ConceptCluster& cluster, const std::vector<float>& new_vec) {
        if (cluster.centroid.empty()) {
            cluster.centroid = new_vec;
            return;
        }
        // Incremental mean update
        size_t n = cluster.members.size();
        for (size_t i = 0; i < cluster.centroid.size(); ++i) {
            cluster.centroid[i] = (cluster.centroid[i] * (n - 1) + new_vec[i]) / n;
        }
        normalise(cluster.centroid);
    }

    void recalculate_centroid(ConceptCluster& cluster) {
        if (cluster.members.empty()) return;

        size_t dim = 0;
        for (auto& nid : cluster.members) {
            if (nodes.count(nid) && !nodes[nid].embedding.empty()) {
                dim = nodes[nid].embedding.size();
                break;
            }
        }
        if (dim == 0) return;

        cluster.centroid.assign(dim, 0.0f);
        int count = 0;
        for (auto& nid : cluster.members) {
            if (!nodes.count(nid) || nodes[nid].embedding.empty()) continue;
            for (size_t i = 0; i < dim; ++i) {
                cluster.centroid[i] += nodes[nid].embedding[i];
            }
            count++;
        }
        if (count > 0) {
            for (size_t i = 0; i < dim; ++i) cluster.centroid[i] /= count;
            normalise(cluster.centroid);
        }
    }

    void recalculate_coherence(ConceptCluster& cluster) {
        if (cluster.members.size() < 2 || cluster.centroid.empty()) {
            cluster.coherence = 1.0f;
            return;
        }

        float total_sim = 0.0f;
        int count = 0;
        for (auto& nid : cluster.members) {
            if (!nodes.count(nid) || nodes[nid].embedding.empty()) continue;
            total_sim += cosine_similarity(nodes[nid].embedding, cluster.centroid);
            count++;
        }
        cluster.coherence = count > 0 ? total_sim / count : 0.0f;
    }

    void update_cluster_stats(ConceptCluster& cluster) {
        int successes = 0, total = 0;
        int observations = 0;
        for (auto& nid : cluster.members) {
            if (!nodes.count(nid)) continue;
            if (nodes[nid].success) successes++;
            total++;
            observations += nodes[nid].observation_count;
        }
        cluster.success_rate = total > 0 ? (float)successes / total : 0.0f;
        cluster.total_observations = observations;
    }

    // --- Publish updates ---

    void publish_concept_update() {
        json cluster_summary = json::array();
        for (auto& [cid, cluster] : clusters) {
            cluster_summary.push_back({
                {"concept", cluster.abstraction},
                {"pattern", cluster.pattern},
                {"coherence", cluster.coherence},
                {"success_rate", cluster.success_rate},
                {"members", cluster.members.size()},
                {"observations", cluster.total_observations}
            });
        }

        // Meta-concept hierarchy
        json hierarchy = json::array();
        for (auto& [mid, mc] : meta_concepts) {
            json children_info = json::array();
            for (auto& cid : mc.children) {
                if (clusters.count(cid)) {
                    children_info.push_back(clusters[cid].abstraction);
                } else if (meta_concepts.count(cid)) {
                    children_info.push_back(meta_concepts[cid].abstraction);
                }
            }
            hierarchy.push_back({
                {"id", mc.id},
                {"level", mc.level},
                {"abstraction", mc.abstraction},
                {"children", children_info},
                {"co_occurrences", mc.co_occurrence_count}
            });
        }

        // Composite operations
        json composites = json::array();
        for (auto& [key, op] : composite_ops) {
            json roles_j = json::array();
            for (auto& r : op.roles) {
                std::string abs = clusters.count(r.cluster_id) ? clusters[r.cluster_id].abstraction : "?";
                roles_j.push_back({{"concept", abs}, {"role", r.role}});
            }
            composites.push_back({
                {"name", op.name}, {"roles", roles_j},
                {"observations", op.observation_count},
                {"success_rate", op.success_rate}
            });
        }

        json update = {
            {"origin", "concept_lobe"}, {"intent", "concept_update"},
            {"cid", "concept_maint_" + std::to_string(std::time(nullptr))},
            {"total_nodes", nodes.size()},
            {"total_clusters", clusters.size()},
            {"total_meta_concepts", meta_concepts.size()},
            {"total_composites", composite_ops.size()},
            {"clusters", cluster_summary},
            {"hierarchy", hierarchy},
            {"composites", composites}
        };
        routing::publish(pub, update);
    }

    // --- Persistence ---

    void save_state() {
        // Save nodes
        std::ofstream nf(data_dir + "concept_space.jsonl", std::ios::trunc);
        if (nf.is_open()) {
            for (auto& [id, node] : nodes) {
                json j = {
                    {"id", node.id}, {"command", node.command},
                    {"output_summary", node.output_summary},
                    {"success", node.success}, {"cluster_id", node.cluster_id},
                    {"observation_count", node.observation_count},
                    {"embedding", node.embedding}
                };
                nf << j.dump() << "\n";
            }
        }

        // Save clusters
        json cluster_state = json::object();
        for (auto& [cid, cluster] : clusters) {
            cluster_state[cid] = {
                {"id", cluster.id}, {"centroid", cluster.centroid},
                {"members", cluster.members}, {"abstraction", cluster.abstraction},
                {"pattern", cluster.pattern}, {"coherence", cluster.coherence},
                {"success_rate", cluster.success_rate},
                {"total_observations", cluster.total_observations},
                {"parameterised_slots", cluster.parameterised_slots}
            };
        }

        std::ofstream cf(data_dir + "concept_clusters.json");
        if (cf.is_open()) cf << cluster_state.dump(2);

        // Save hierarchy
        json hier_state;
        for (auto& [mid, mc] : meta_concepts) {
            hier_state["meta_concepts"][mid] = {
                {"id", mc.id}, {"level", mc.level},
                {"children", mc.children}, {"abstraction", mc.abstraction},
                {"centroid", mc.centroid}, {"coherence", mc.coherence},
                {"co_occurrence_count", mc.co_occurrence_count},
                {"last_updated", mc.last_updated}
            };
        }
        hier_state["co_occurrence"] = co_occurrence;
        hier_state["total_completed_goals"] = total_completed_goals;
        hier_state["sequence_counts"] = sequence_counts;
        hier_state["sequence_successes"] = sequence_successes;

        // Save sequence patterns
        json seq_pats = json::object();
        for (auto& [key, pat] : sequence_patterns) seq_pats[key] = pat;
        hier_state["sequence_patterns"] = seq_pats;

        // Save composite operations
        json comps = json::object();
        for (auto& [key, op] : composite_ops) {
            json roles = json::array();
            for (auto& r : op.roles) {
                roles.push_back({{"cluster_id", r.cluster_id}, {"role", r.role}, {"position", r.position}});
            }
            comps[key] = {
                {"id", op.id}, {"name", op.name}, {"roles", roles},
                {"sequence_key", op.sequence_key}, {"observation_count", op.observation_count},
                {"success_rate", op.success_rate}, {"last_seen", op.last_seen}
            };
        }
        hier_state["composite_ops"] = comps;

        std::ofstream hf(data_dir + "concept_hierarchy.json");
        if (hf.is_open()) hf << hier_state.dump(2);

        std::cout << "[CONCEPT] State saved: " << nodes.size() << " nodes, "
                  << clusters.size() << " clusters, "
                  << meta_concepts.size() << " meta-concepts, "
                  << composite_ops.size() << " composites" << std::endl;
    }

    void load_state() {
        // Load nodes
        std::ifstream nf(data_dir + "concept_space.jsonl");
        if (nf.is_open()) {
            std::string line;
            while (std::getline(nf, line)) {
                try {
                    auto j = json::parse(line);
                    ConceptNode node;
                    node.id = j.value("id", "");
                    node.command = j.value("command", "");
                    node.output_summary = j.value("output_summary", "");
                    node.success = j.value("success", false);
                    node.cluster_id = j.value("cluster_id", "");
                    node.observation_count = j.value("observation_count", 1);
                    node.embedding = j.value("embedding", std::vector<float>{});
                    node.created_at = std::chrono::steady_clock::now();
                    if (!node.id.empty()) nodes[node.id] = node;
                } catch (...) {}
            }
        }

        // Load clusters
        std::ifstream cf(data_dir + "concept_clusters.json");
        if (cf.is_open()) {
            try {
                json cluster_state;
                cf >> cluster_state;
                for (auto& [cid, j] : cluster_state.items()) {
                    ConceptCluster cluster;
                    cluster.id = j.value("id", cid);
                    cluster.centroid = j.value("centroid", std::vector<float>{});
                    cluster.members = j.value("members", std::vector<std::string>{});
                    cluster.abstraction = j.value("abstraction", "");
                    cluster.pattern = j.value("pattern", "");
                    cluster.coherence = j.value("coherence", 0.0f);
                    cluster.success_rate = j.value("success_rate", 0.0f);
                    cluster.total_observations = j.value("total_observations", 0);
                    cluster.parameterised_slots = j.value("parameterised_slots", std::vector<std::string>{});

                    // Validate: remove member references to nodes that don't exist
                    std::vector<std::string> valid_members;
                    for (auto& nid : cluster.members) {
                        if (nodes.count(nid)) valid_members.push_back(nid);
                    }
                    cluster.members = valid_members;

                    if (!cluster.members.empty()) {
                        clusters[cid] = cluster;
                    }
                }
            } catch (...) {}
        }

        // Load hierarchy
        std::ifstream hf(data_dir + "concept_hierarchy.json");
        if (hf.is_open()) {
            try {
                json hier_state;
                hf >> hier_state;

                if (hier_state.contains("meta_concepts")) {
                    for (auto& [mid, j] : hier_state["meta_concepts"].items()) {
                        MetaConcept mc;
                        mc.id = j.value("id", mid);
                        mc.level = j.value("level", 2);
                        mc.children = j.value("children", std::vector<std::string>{});
                        mc.abstraction = j.value("abstraction", "");
                        mc.centroid = j.value("centroid", std::vector<float>{});
                        mc.coherence = j.value("coherence", 0.0f);
                        mc.co_occurrence_count = j.value("co_occurrence_count", 0);
                        mc.last_updated = j.value("last_updated", 0.0);
                        meta_concepts[mid] = mc;
                    }
                }

                if (hier_state.contains("co_occurrence")) {
                    co_occurrence = hier_state["co_occurrence"].get<std::map<std::string, int>>();
                }

                total_completed_goals = hier_state.value("total_completed_goals", 0);

                // Load sequence data
                if (hier_state.contains("sequence_counts"))
                    sequence_counts = hier_state["sequence_counts"].get<std::map<std::string, int>>();
                if (hier_state.contains("sequence_successes"))
                    sequence_successes = hier_state["sequence_successes"].get<std::map<std::string, int>>();
                if (hier_state.contains("sequence_patterns")) {
                    for (auto& [key, val] : hier_state["sequence_patterns"].items())
                        sequence_patterns[key] = val.get<std::vector<std::string>>();
                }

                // Load composite operations
                if (hier_state.contains("composite_ops")) {
                    for (auto& [key, j] : hier_state["composite_ops"].items()) {
                        CompositeOperation op;
                        op.id = j.value("id", "");
                        op.name = j.value("name", "");
                        op.sequence_key = j.value("sequence_key", key);
                        op.observation_count = j.value("observation_count", 0);
                        op.success_rate = j.value("success_rate", 0.0f);
                        op.last_seen = j.value("last_seen", 0.0);
                        if (j.contains("roles")) {
                            for (auto& rj : j["roles"]) {
                                SemanticRole r;
                                r.cluster_id = rj.value("cluster_id", "");
                                r.role = rj.value("role", "action");
                                r.position = rj.value("position", 0);
                                op.roles.push_back(r);
                            }
                        }
                        composite_ops[key] = op;
                    }
                }
            } catch (...) {}
        }
    }

    // --- Helpers ---

    std::string build_embed_text(const std::string& cmd, const std::string& output, bool success) {
        // Build a rich text representation for embedding
        // Include the command, a summary of the output, and the outcome
        std::string text = "command: " + cmd;
        if (!output.empty()) {
            std::string out_trunc = output.substr(0, 150);
            // Remove newlines for cleaner embedding
            std::replace(out_trunc.begin(), out_trunc.end(), '\n', ' ');
            text += " | output: " + out_trunc;
        }
        text += success ? " | result: success" : " | result: failure";
        return text;
    }

    std::string hash_command(const std::string& cmd) {
        // Simple hash for deduplication
        std::hash<std::string> hasher;
        return "n_" + std::to_string(hasher(cmd));
    }

    std::string truncate(const std::string& s, size_t max_len) {
        if (s.size() <= max_len) return s;
        return s.substr(0, max_len - 3) + "...";
    }

    json get_example_commands(const ConceptCluster& cluster, int n) {
        json examples = json::array();
        int count = 0;
        for (auto& nid : cluster.members) {
            if (count >= n) break;
            if (nodes.count(nid)) {
                examples.push_back(nodes[nid].command);
                count++;
            }
        }
        return examples;
    }

    void prune_oldest_nodes() {
        // Remove the oldest nodes that have lowest observation counts
        // Keep nodes that are cluster centroids or high-observation
        std::vector<std::pair<std::string, int>> candidates;
        for (auto& [id, node] : nodes) {
            candidates.push_back({id, node.observation_count});
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](auto& a, auto& b) { return a.second < b.second; });

        size_t to_remove = nodes.size() - MAX_NODES + MAX_NODES / 10;
        for (size_t i = 0; i < to_remove && i < candidates.size(); ++i) {
            std::string nid = candidates[i].first;
            // Remove from its cluster
            if (nodes.count(nid) && clusters.count(nodes[nid].cluster_id)) {
                auto& members = clusters[nodes[nid].cluster_id].members;
                members.erase(std::remove(members.begin(), members.end(), nid), members.end());
            }
            nodes.erase(nid);
        }
    }

    // ─── Abstraction Hierarchy ──────────────────────────────────

    void record_goal_activation(const std::string& cid, const std::string& cluster_id,
                                bool success = true) {
        auto now = std::chrono::steady_clock::now();
        auto& goal = active_goals[cid];
        if (goal.cid.empty()) {
            goal.cid = cid;
            goal.started_at = now;
        }
        goal.last_activity = now;

        // Record in order (allow duplicates for sequence learning, but not consecutive)
        if (goal.cluster_ids.empty() || goal.cluster_ids.back() != cluster_id) {
            goal.cluster_ids.push_back(cluster_id);
            goal.success_flags.push_back(success);
        }
    }

    void finalize_stale_goals() {
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> stale;

        for (auto& [cid, goal] : active_goals) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - goal.last_activity).count();
            if (age > GOAL_TIMEOUT_SECS) {
                stale.push_back(cid);
            }
        }

        for (auto& cid : stale) {
            auto& goal = active_goals[cid];
            if (goal.cluster_ids.size() >= 2) {
                // Record pairwise co-occurrences
                auto& ids = goal.cluster_ids;
                for (size_t i = 0; i < ids.size(); ++i) {
                    for (size_t j = i + 1; j < ids.size(); ++j) {
                        std::string a = ids[i], b = ids[j];
                        if (a > b) std::swap(a, b);
                        co_occurrence[a + "|" + b]++;
                    }
                }

                // Record ordered sequences for semantic compositionality
                // Extract subsequences of length 2-4
                for (size_t len = 2; len <= std::min((size_t)4, ids.size()); ++len) {
                    for (size_t start = 0; start + len <= ids.size(); ++start) {
                        // Build sequence key (ordered)
                        std::string seq_key;
                        std::vector<std::string> seq;
                        bool all_success = true;
                        for (size_t k = start; k < start + len; ++k) {
                            if (k > start) seq_key += "→";
                            seq_key += ids[k];
                            seq.push_back(ids[k]);
                            if (k < goal.success_flags.size() && !goal.success_flags[k])
                                all_success = false;
                        }
                        sequence_counts[seq_key]++;
                        sequence_patterns[seq_key] = seq;
                        if (all_success) sequence_successes[seq_key]++;
                    }
                }

                total_completed_goals++;
            }
            active_goals.erase(cid);
        }

        // Cap active_goals to prevent memory leak from orphan cids
        if (active_goals.size() > 200) {
            // Remove oldest
            std::string oldest_cid;
            auto oldest_time = std::chrono::steady_clock::time_point::max();
            for (auto& [cid, goal] : active_goals) {
                if (goal.last_activity < oldest_time) {
                    oldest_time = goal.last_activity;
                    oldest_cid = cid;
                }
            }
            if (!oldest_cid.empty()) active_goals.erase(oldest_cid);
        }
    }

    void compose_abstractions() {
        if (total_completed_goals < 5) return;  // need enough data

        // Find co-occurring cluster pairs that exceed threshold
        std::vector<std::pair<std::string, std::string>> candidates;
        for (auto& [key, count] : co_occurrence) {
            if (count < MIN_COOCCURRENCE) continue;
            float ratio = (float)count / total_completed_goals;
            if (ratio < MIN_COOCCURRENCE_RATIO) continue;

            // Parse "clusterA|clusterB"
            auto sep = key.find('|');
            if (sep == std::string::npos) continue;
            std::string a = key.substr(0, sep);
            std::string b = key.substr(sep + 1);

            // Both clusters must still exist
            if (!clusters.count(a) || !clusters.count(b)) continue;

            // Check if this pair is already captured in a meta-concept
            bool already_composed = false;
            for (auto& [mid, mc] : meta_concepts) {
                auto& ch = mc.children;
                if (std::find(ch.begin(), ch.end(), a) != ch.end() &&
                    std::find(ch.begin(), ch.end(), b) != ch.end()) {
                    // Update co-occurrence count
                    mc.co_occurrence_count = count;
                    already_composed = true;
                    break;
                }
            }
            if (!already_composed) {
                candidates.push_back({a, b});
            }
        }

        // Create new meta-concepts from strong co-occurrences
        for (auto& [a, b] : candidates) {
            // Try to absorb into an existing meta-concept that already contains one of them
            bool absorbed = false;
            for (auto& [mid, mc] : meta_concepts) {
                if (mc.level != 2) continue;
                auto& ch = mc.children;
                bool has_a = std::find(ch.begin(), ch.end(), a) != ch.end();
                bool has_b = std::find(ch.begin(), ch.end(), b) != ch.end();

                if (has_a && !has_b) {
                    // Check co-occurrence of b with all existing children
                    bool strong_with_all = true;
                    for (auto& c : ch) {
                        std::string k1 = (b < c) ? b + "|" + c : c + "|" + b;
                        if (co_occurrence[k1] < MIN_COOCCURRENCE) {
                            strong_with_all = false;
                            break;
                        }
                    }
                    if (strong_with_all && ch.size() < 6) {
                        ch.push_back(b);
                        update_meta_centroid(mc);
                        mc.abstraction = infer_meta_abstraction(mc);
                        absorbed = true;
                        std::cout << "[CONCEPT] Absorbed '" << clusters[b].abstraction
                                  << "' into meta-concept '" << mc.abstraction << "'" << std::endl;
                        break;
                    }
                } else if (has_b && !has_a) {
                    bool strong_with_all = true;
                    for (auto& c : ch) {
                        std::string k1 = (a < c) ? a + "|" + c : c + "|" + a;
                        if (co_occurrence[k1] < MIN_COOCCURRENCE) {
                            strong_with_all = false;
                            break;
                        }
                    }
                    if (strong_with_all && ch.size() < 6) {
                        ch.push_back(a);
                        update_meta_centroid(mc);
                        mc.abstraction = infer_meta_abstraction(mc);
                        absorbed = true;
                        std::cout << "[CONCEPT] Absorbed '" << clusters[a].abstraction
                                  << "' into meta-concept '" << mc.abstraction << "'" << std::endl;
                        break;
                    }
                }
            }

            if (!absorbed) {
                // Create new Level 2 meta-concept
                MetaConcept mc;
                mc.id = "m2_" + std::to_string(meta_concepts.size());
                mc.level = 2;
                mc.children = {a, b};

                std::string co_key = (a < b) ? a + "|" + b : b + "|" + a;
                mc.co_occurrence_count = co_occurrence[co_key];

                update_meta_centroid(mc);
                mc.abstraction = infer_meta_abstraction(mc);
                mc.last_updated = epoch_secs();

                meta_concepts[mc.id] = mc;

                std::cout << "[CONCEPT] HIERARCHY: Level 2 meta-concept '"
                          << mc.abstraction << "' = {'"
                          << clusters[a].abstraction << "' + '"
                          << clusters[b].abstraction << "'} (co-occurred "
                          << mc.co_occurrence_count << "/" << total_completed_goals
                          << " goals)" << std::endl;
            }
        }

        // Level 3+: compose meta-concepts that themselves co-occur
        compose_higher_levels();
    }

    void compose_higher_levels() {
        // Check if any Level 2 meta-concepts share children that co-occur frequently
        // If two meta-concepts have overlapping or adjacent clusters, consider merging into Level 3
        if (meta_concepts.size() < 2) return;

        std::vector<std::string> l2_ids;
        for (auto& [mid, mc] : meta_concepts) {
            if (mc.level == 2 && mc.children.size() >= 2) l2_ids.push_back(mid);
        }

        for (size_t i = 0; i < l2_ids.size(); ++i) {
            for (size_t j = i + 1; j < l2_ids.size(); ++j) {
                auto& mc_a = meta_concepts[l2_ids[i]];
                auto& mc_b = meta_concepts[l2_ids[j]];

                // Check: do all children of mc_a co-occur with all children of mc_b?
                bool all_strong = true;
                for (auto& ca : mc_a.children) {
                    for (auto& cb : mc_b.children) {
                        if (ca == cb) continue;  // shared child
                        std::string k = (ca < cb) ? ca + "|" + cb : cb + "|" + ca;
                        if (co_occurrence[k] < MIN_COOCCURRENCE) {
                            all_strong = false;
                            break;
                        }
                    }
                    if (!all_strong) break;
                }

                if (!all_strong) continue;

                // Check not already captured at Level 3
                bool exists = false;
                for (auto& [mid, mc] : meta_concepts) {
                    if (mc.level >= 3) {
                        auto& ch = mc.children;
                        if (std::find(ch.begin(), ch.end(), l2_ids[i]) != ch.end() &&
                            std::find(ch.begin(), ch.end(), l2_ids[j]) != ch.end()) {
                            exists = true;
                            break;
                        }
                    }
                }
                if (exists) continue;

                // Create Level 3
                MetaConcept mc3;
                mc3.id = "m3_" + std::to_string(meta_concepts.size());
                mc3.level = 3;
                mc3.children = {l2_ids[i], l2_ids[j]};
                mc3.abstraction = mc_a.abstraction + "+" + mc_b.abstraction;
                mc3.last_updated = epoch_secs();

                // Centroid: mean of the two meta-concept centroids
                if (!mc_a.centroid.empty() && !mc_b.centroid.empty()) {
                    mc3.centroid.resize(mc_a.centroid.size(), 0.0f);
                    for (size_t k = 0; k < mc3.centroid.size(); ++k) {
                        mc3.centroid[k] = (mc_a.centroid[k] + mc_b.centroid[k]) / 2.0f;
                    }
                    normalise(mc3.centroid);
                }

                meta_concepts[mc3.id] = mc3;

                std::cout << "[CONCEPT] HIERARCHY: Level 3 meta-concept '"
                          << mc3.abstraction << "' = {'"
                          << mc_a.abstraction << "' + '"
                          << mc_b.abstraction << "'}" << std::endl;
            }
        }
    }

    std::string infer_meta_abstraction(const MetaConcept& mc) {
        // Compose a name from children's abstractions
        // e.g., {read_content, search_content} → "information_retrieval"
        //        {compile, build_system} → "compilation_pipeline"
        std::set<std::string> child_abstractions;
        for (auto& cid : mc.children) {
            if (clusters.count(cid))
                child_abstractions.insert(clusters[cid].abstraction);
        }

        // Check for known semantic compositions
        static const std::vector<std::pair<std::set<std::string>, std::string>> known_compositions = {
            {{"read_content", "search_content"}, "information_retrieval"},
            {{"read_content", "search_filesystem"}, "information_retrieval"},
            {{"search_content", "search_filesystem"}, "search_operations"},
            {{"compile", "build_system"}, "compilation_pipeline"},
            {{"read_content", "transform_text"}, "text_processing"},
            {{"search_content", "transform_text"}, "text_analysis"},
            {{"copy_resource", "remove_resource"}, "resource_management"},
            {{"create_resource", "remove_resource"}, "resource_lifecycle"},
            {{"create_structure", "create_resource"}, "scaffolding"},
            {{"network_transfer", "network_probe"}, "network_operations"},
            {{"remote_access", "remote_transfer"}, "remote_operations"},
            {{"inspect_process", "signal_process"}, "process_management"},
            {{"version_control", "compile"}, "development_workflow"},
            {{"version_control", "build_system"}, "development_workflow"},
            {{"read_content", "write_output"}, "content_transform"},
            {{"modify_permissions", "modify_ownership"}, "access_control"},
            {{"measure_content", "measure_resource"}, "measurement"},
            {{"archive", "compress"}, "packaging"},
            {{"compare_content", "search_content"}, "content_analysis"},
            {{"identify_type", "verify_integrity"}, "content_verification"},
        };

        // Check if child abstractions match a known composition
        for (auto& [pattern, name] : known_compositions) {
            if (std::includes(child_abstractions.begin(), child_abstractions.end(),
                             pattern.begin(), pattern.end())) {
                return name;
            }
        }

        // Fallback: find common semantic root
        // Extract shared suffixes/prefixes
        std::vector<std::string> abs_list(child_abstractions.begin(), child_abstractions.end());
        if (abs_list.size() >= 2) {
            // Check for shared suffix (e.g., _content, _resource, _process)
            std::string common_suffix = find_common_suffix(abs_list);
            if (common_suffix.size() > 3) {
                return common_suffix.substr(1) + "_operations";  // strip leading _
            }

            // Check for shared prefix
            std::string common_prefix = find_common_prefix(abs_list);
            if (common_prefix.size() > 3) {
                return common_prefix + "_composite";
            }
        }

        // Last resort: join with +
        std::string result;
        for (auto& a : child_abstractions) {
            if (!result.empty()) result += "+";
            result += a;
        }
        return result;
    }

    std::string find_common_suffix(const std::vector<std::string>& strings) {
        if (strings.empty()) return "";
        std::string ref = strings[0];
        for (int len = (int)ref.size(); len > 0; --len) {
            std::string suffix = ref.substr(ref.size() - len);
            bool all_match = true;
            for (size_t i = 1; i < strings.size(); ++i) {
                if (strings[i].size() < (size_t)len ||
                    strings[i].substr(strings[i].size() - len) != suffix) {
                    all_match = false;
                    break;
                }
            }
            if (all_match && suffix.find('_') != std::string::npos) return suffix;
        }
        return "";
    }

    std::string find_common_prefix(const std::vector<std::string>& strings) {
        if (strings.empty()) return "";
        std::string ref = strings[0];
        for (int len = (int)ref.size(); len > 0; --len) {
            std::string prefix = ref.substr(0, len);
            bool all_match = true;
            for (size_t i = 1; i < strings.size(); ++i) {
                if (strings[i].size() < (size_t)len ||
                    strings[i].substr(0, len) != prefix) {
                    all_match = false;
                    break;
                }
            }
            if (all_match && prefix.find('_') != std::string::npos) return prefix;
        }
        return "";
    }

    void update_meta_centroid(MetaConcept& mc) {
        std::vector<std::vector<float>*> centroids;
        for (auto& cid : mc.children) {
            if (clusters.count(cid) && !clusters[cid].centroid.empty()) {
                centroids.push_back(&clusters[cid].centroid);
            }
        }
        if (centroids.empty()) return;

        size_t dim = centroids[0]->size();
        mc.centroid.assign(dim, 0.0f);
        for (auto* c : centroids) {
            for (size_t i = 0; i < dim; ++i) mc.centroid[i] += (*c)[i];
        }
        for (size_t i = 0; i < dim; ++i) mc.centroid[i] /= centroids.size();
        normalise(mc.centroid);
    }

    // ─── Semantic Compositionality ────────────────────────────────

    void discover_composites() {
        for (auto& [seq_key, count] : sequence_counts) {
            if (count < MIN_SEQUENCE_OBS) continue;
            if (composite_ops.count(seq_key)) {
                // Update existing
                auto& op = composite_ops[seq_key];
                op.observation_count = count;
                op.success_rate = sequence_successes.count(seq_key)
                    ? (float)sequence_successes[seq_key] / count : 0.0f;
                op.last_seen = epoch_secs();
                continue;
            }

            // New composite — verify all clusters still exist
            auto& pattern = sequence_patterns[seq_key];
            bool all_valid = true;
            for (auto& cid : pattern) {
                if (!clusters.count(cid)) { all_valid = false; break; }
            }
            if (!all_valid) continue;

            // Infer semantic roles from position and cluster type
            CompositeOperation op;
            op.id = "comp_" + std::to_string(composite_ops.size());
            op.sequence_key = seq_key;
            op.observation_count = count;
            op.success_rate = sequence_successes.count(seq_key)
                ? (float)sequence_successes[seq_key] / count : 0.0f;
            op.last_seen = epoch_secs();

            for (size_t i = 0; i < pattern.size(); ++i) {
                SemanticRole role;
                role.cluster_id = pattern[i];
                role.position = (int)i;
                role.role = infer_semantic_role(pattern[i], i, pattern.size());
                op.roles.push_back(role);
            }

            op.name = infer_composite_name(op);

            composite_ops[seq_key] = op;

            // Log discovery
            std::string parts;
            for (auto& r : op.roles) {
                if (!parts.empty()) parts += " → ";
                parts += clusters[r.cluster_id].abstraction + "(" + r.role + ")";
            }
            std::cout << "[CONCEPT] COMPOSITE: '" << op.name << "' = ["
                      << parts << "] (seen " << count << "x)" << std::endl;
        }

        // Prune stale sequences (not seen recently, low count)
        std::vector<std::string> stale_seqs;
        for (auto& [key, count] : sequence_counts) {
            if (count < 2) stale_seqs.push_back(key);
        }
        if (stale_seqs.size() > 500) {
            for (auto& k : stale_seqs) {
                sequence_counts.erase(k);
                sequence_patterns.erase(k);
                sequence_successes.erase(k);
            }
        }
    }

    std::string infer_semantic_role(const std::string& cluster_id, size_t pos, size_t total) {
        if (!clusters.count(cluster_id)) return "action";
        auto& abs = clusters[cluster_id].abstraction;

        // Role by abstraction type
        static const std::map<std::string, std::string> role_map = {
            {"search_filesystem", "precondition"}, {"search_content", "precondition"},
            {"inspect_process", "precondition"},   {"system_status", "precondition"},
            {"measure_content", "precondition"},   {"measure_resource", "precondition"},
            {"identify_type", "precondition"},     {"network_inspect", "precondition"},
            {"read_content", "action"},            {"write_output", "action"},
            {"copy_resource", "action"},           {"move_resource", "action"},
            {"remove_resource", "action"},         {"create_resource", "action"},
            {"create_structure", "action"},        {"compile", "action"},
            {"build_system", "action"},            {"transform_text", "action"},
            {"network_transfer", "action"},        {"archive", "action"},
            {"compress", "action"},                {"modify_permissions", "action"},
            {"modify_ownership", "action"},        {"version_control", "action"},
            {"compare_content", "verification"},   {"verify_integrity", "verification"},
        };

        if (role_map.count(abs)) return role_map.at(abs);

        // Role by position
        if (pos == 0 && total > 2) return "precondition";
        if (pos == total - 1 && total > 2) return "verification";
        return "action";
    }

    std::string infer_composite_name(const CompositeOperation& op) {
        // Collect child abstractions in order
        std::vector<std::string> abs_list;
        std::set<std::string> role_set;
        for (auto& r : op.roles) {
            if (clusters.count(r.cluster_id))
                abs_list.push_back(clusters[r.cluster_id].abstraction);
            role_set.insert(r.role);
        }

        if (abs_list.empty()) return "unknown_composite";

        // Check known semantic compositions (ordered patterns)
        static const std::vector<std::pair<std::vector<std::string>, std::string>> known = {
            {{"search_filesystem", "read_content"}, "locate_and_read"},
            {{"read_content", "copy_resource"}, "backup"},
            {{"search_content", "read_content"}, "find_and_examine"},
            {{"read_content", "transform_text"}, "extract_transform"},
            {{"read_content", "write_output"}, "content_relay"},
            {{"search_filesystem", "remove_resource"}, "find_and_clean"},
            {{"create_structure", "create_resource"}, "scaffold"},
            {{"build_system", "compile"}, "full_build"},
            {{"compile", "inspect_process"}, "build_and_verify"},
            {{"version_control", "compile"}, "commit_and_build"},
            {{"read_content", "compare_content"}, "diff_check"},
            {{"copy_resource", "modify_permissions"}, "deploy"},
            {{"search_content", "transform_text"}, "search_and_transform"},
            {{"network_transfer", "verify_integrity"}, "secure_download"},
            {{"read_content", "read_content"}, "multi_read"},
            {{"search_filesystem", "read_content", "transform_text"}, "find_read_transform"},
            {{"read_content", "transform_text", "write_output"}, "etl_pipeline"},
            {{"version_control", "build_system", "compile"}, "dev_cycle"},
            {{"search_filesystem", "copy_resource"}, "find_and_copy"},
            {{"inspect_process", "signal_process"}, "manage_process"},
        };

        for (auto& [pattern, name] : known) {
            if (pattern.size() != abs_list.size()) continue;
            bool match = true;
            for (size_t i = 0; i < pattern.size(); ++i) {
                if (pattern[i] != abs_list[i]) { match = false; break; }
            }
            if (match) return name;
        }

        // Fallback: combine roles into name
        bool has_precondition = role_set.count("precondition") > 0;
        bool has_verification = role_set.count("verification") > 0;

        // Use the primary action's abstraction as base
        std::string primary;
        for (auto& r : op.roles) {
            if (r.role == "action" && clusters.count(r.cluster_id)) {
                primary = clusters[r.cluster_id].abstraction;
                break;
            }
        }
        if (primary.empty()) primary = abs_list[0];

        if (has_precondition && has_verification)
            return "guarded_" + primary;
        else if (has_precondition)
            return "prepared_" + primary;
        else if (has_verification)
            return "verified_" + primary;

        // Join with underscores
        std::string result;
        for (auto& a : abs_list) {
            if (!result.empty()) result += "_then_";
            result += a;
        }
        return result;
    }

    double epoch_secs() {
        return std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    void dispatch(const json& msg) {
        routing::publish(pub, msg);
    }
};

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[++i];
    }

    ConceptLobe lobe(ip);
    lobe.start();
    return 0;
}
