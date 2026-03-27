#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <ctime>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class HippocampusLobe {
public:
    HippocampusLobe(const std::string& thalamus_ip = "localhost",
                   const std::string& storage_path = "./data/engrams") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub), base_dir(storage_path) {
        
        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {
            "search_memory", "recall_memory", "consolidate_memories",
            "embedding_result", "execution_result",
            "search_trajectory"
        });

        if (!fs::exists(base_dir)) {
            fs::create_directories(base_dir);
        }

        std::cout << "[HIPPOCAMPUS] Connected to Thalamus at " << thalamus_ip << std::endl;
    }

    void start() {
        int maintenance_tick = 0;
        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                // Periodic maintenance every ~5 minutes (6000 ticks * 50ms)
                if (++maintenance_tick % 6000 == 0) {
                    cleanup_pending_embeds();
                    compact_memory_index();
                    finalize_stale_trajectories();
                }
                continue;
            }
            try {
                process_neural_event(j);
            } catch (...) {}
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    fs::path base_dir;
    std::mutex io_mutex;

    // Pending embedding requests: embed_cid → engram waiting to be indexed
    struct PendingEmbed { json engram; long created_ts = 0; };
    std::map<std::string, PendingEmbed> pending_embeds;
    static constexpr int PENDING_EMBED_TIMEOUT_SECS = 60;  // expire after 60s
    static constexpr size_t MEMORY_INDEX_MAX_ENTRIES = 50000;  // cap indexed memories
    static constexpr double IMPORTANCE_ARCHIVE_THRESHOLD = 0.5;  // below this → archive
    static constexpr int CONSOLIDATION_KEEP_RECENT = 200;  // keep N most recent per ledger

    // Phase 7: Trajectory memory — track command sequences per goal CID
    struct TrajectoryStep {
        std::string command;
        bool success = false;
        long timestamp = 0;
    };
    struct TrajectoryRecord {
        std::string cid;
        std::string domain;
        std::vector<TrajectoryStep> steps;
        long start_ts = 0;
        long last_ts = 0;
    };
    std::map<std::string, TrajectoryRecord> trajectory_buffer;
    static constexpr int TRAJECTORY_STALE_SECS = 120;
    static constexpr int MAX_TRAJECTORIES = 2000;

    void process_neural_event(const json& event) {
        std::string intent = event.value("intent", "");
        std::string cid    = event.value("cid", "global_stream");

        if (intent == "recall_memory") {
            handle_recall(cid, event);
        }
        else if (intent == "search_memory") {
            handle_search(event);
        }
        else if (intent == "consolidate_memories") {
            // Consolidate the specific CID
            handle_consolidation(cid);
            // Also consolidate any large ledgers that have accumulated
            auto large = find_large_ledgers();
            for (const auto& lcid : large) {
                if (lcid != cid) handle_consolidation(lcid);
            }
            // Compact the memory index if oversized
            compact_memory_index();
        }
        // Resolve a pending embedding: store the indexed memory
        else if (intent == "embedding_result" && pending_embeds.count(cid)) {
            std::vector<float> emb = event.value("embedding", std::vector<float>{});
            if (!emb.empty()) {
                json indexed = pending_embeds[cid].engram;
                indexed["embedding"] = emb;
                std::lock_guard<std::mutex> lock(io_mutex);
                std::ofstream f(base_dir / "memory_index.jsonl", std::ios::app);
                f << indexed.dump() << "\n";
                std::cout << "[HIPPOCAMPUS] Memory indexed. Dim=" << emb.size() << std::endl;
            }
            pending_embeds.erase(cid);
        }
        // Index ALL execution results (success AND failure) for semantic retrieval.
        // Failures are tagged so future recall can warn "this was tried and failed".
        else if (intent == "execution_result") {
            record_engram(cid, event);
            if (cid != "global_stream") record_engram("global_stream", event);
            request_memory_embedding(cid, event);
            // Phase 7: Track trajectory steps
            track_trajectory_step(cid, event);
        }
        // Phase 7: Trajectory search
        else if (intent == "search_trajectory") {
            handle_trajectory_search(event);
        }
        else {
            record_engram(cid, event);
            if (cid != "global_stream") record_engram("global_stream", event);
        }
    }

    void record_engram(const std::string& cid, const json& data) {
        std::lock_guard<std::mutex> lock(io_mutex);
        fs::path ledger = base_dir / (cid + ".jsonl");
        
        std::ofstream file(ledger, std::ios::app);
        if (file.is_open()) {
            json engram = data;
            engram["synapse_ts"] = std::time(nullptr);
            // Ensure importance tagging for future consolidation
            if (!engram.contains("importance")) engram["importance"] = 1.0; 
            file << engram.dump() << "\n";
        }
    }

    void handle_recall(const std::string& cid, const json& request) {
        std::lock_guard<std::mutex> lock(io_mutex);
        fs::path ledger = base_dir / (cid + ".jsonl");
        int count = request.value("count", 10);
        
        std::vector<json> traces;
        if (fs::exists(ledger)) {
            std::ifstream file(ledger);
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) traces.push_back(json::parse(line));
            }
        }

        // Return the most recent traces (short-term memory)
        size_t start_idx = (traces.size() > (size_t)count) ? (traces.size() - count) : 0;
        std::vector<json> recent(traces.begin() + start_idx, traces.end());

        json memory_packet = {
            {"cid", cid},
            {"origin", "hippocampus"},
            {"intent", "memory_recalled"},
            {"history", recent}
        };

        dispatch(memory_packet);
    }

    void handle_search(const json& request) {
        std::string query = request.value("query", "");
        std::string cid   = request.value("cid", "global");

        std::cout << "[HIPPOCAMPUS] Semantic search: '" << query << "'" << std::endl;

        // 1. Request embedding for the query
        std::string search_cid = "hip_search_" + cid;
        json emb_req = {
            {"cid", search_cid}, {"origin", "hippocampus"},
            {"intent", "embedding_request"}, {"text", query}
        };
        dispatch(emb_req);

        // 2. Wait up to 10s for the embedding result
        std::vector<float> query_vec;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            auto emb_j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (!emb_j.is_null()) {
                if (emb_j.value("intent", "") == "embedding_result" &&
                    emb_j.value("cid", "") == search_cid) {
                    query_vec = emb_j.value("embedding", std::vector<float>{});
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (query_vec.empty()) {
            keyword_search(query, request);
            return;
        }

        // 3. Scan memory_index.jsonl (only indexed, embedded memories)
        struct Match { json engram; float score; };
        std::vector<Match> results;
        {
            std::lock_guard<std::mutex> lock(io_mutex);
            fs::path index_path = base_dir / "memory_index.jsonl";
            if (fs::exists(index_path)) {
                std::ifstream f(index_path);
                std::string line;
                while (std::getline(f, line)) {
                    try {
                        auto j = json::parse(line);
                        std::vector<float> emb = j.value("embedding", std::vector<float>{});
                        if (emb.empty()) continue;

                        // Dot product of normalised vectors = cosine similarity
                        float score = 0.0f;
                        size_t dim = std::min(query_vec.size(), emb.size());
                        for (size_t i = 0; i < dim; ++i) score += query_vec[i] * emb[i];
                        results.push_back({j, score});
                    } catch (...) {}
                }
            }
        }

        std::sort(results.begin(), results.end(),
                  [](const Match& a, const Match& b) { return a.score > b.score; });

        std::vector<json> final_matches;
        for (size_t i = 0; i < std::min(results.size(), (size_t)5); ++i) {
            json m = results[i].engram;
            m.erase("embedding"); // Strip embedding vector before broadcasting — reduces bus payload size
            m["similarity"] = results[i].score;
            // Ensure success tag is present for recall context
            if (!m.contains("success")) m["success"] = true;
            final_matches.push_back(m);
        }

        std::string mode = final_matches.empty() ? "keyword_fallback" : "semantic";
        if (final_matches.empty()) {
            keyword_search(query, request);
            return;
        }

        json search_result = {
            {"cid", cid}, {"origin", "hippocampus"}, {"intent", "search_result"},
            {"query", query}, {"matches", final_matches}, {"mode", mode}
        };
        dispatch(search_result);
    }

    void request_memory_embedding(const std::string& cid, const json& event) {
        std::string cmd     = event.value("command", "");
        std::string result  = event.value("proprioception", event.value("output", "")).substr(0, 300);
        std::string mode    = event.value("mode", "reality");
        bool success        = (event.value("status", "") == "success");

        // Build a descriptive text for the embedding — include outcome for recall
        std::string embed_text = (success ? "SUCCESS: " : "FAILURE: ") + cmd + " RESULT: " + result;

        json seed = {
            {"origin_cid",  cid},
            {"intent",      "memory_seed"},
            {"command",     cmd},
            {"result_summary", result},
            {"mode",        mode},
            {"success",     success},
            {"synapse_ts",  std::time(nullptr)}
        };

        std::string embed_cid = "hip_embed_" + std::to_string(std::time(nullptr)) + "_" + cid;
        pending_embeds[embed_cid] = {seed, (long)std::time(nullptr)};

        json req = {
            {"cid", embed_cid}, {"origin", "hippocampus"},
            {"intent", "embedding_request"}, {"text", embed_text}
        };
        dispatch(req);
    }

    void keyword_search(const std::string& query, const json& request) {
        std::vector<json> matches;
        for (const auto& entry : fs::directory_iterator(base_dir)) {
            if (entry.path().extension() == ".jsonl") {
                std::ifstream file(entry.path());
                std::string line;
                while (std::getline(file, line)) {
                    if (line.find(query) != std::string::npos) {
                        matches.push_back(json::parse(line));
                        if (matches.size() >= 5) break;
                    }
                }
            }
            if (matches.size() >= 5) break;
        }
        json res = {{"cid", request.value("cid", "global")}, {"origin", "hippocampus"}, {"intent", "search_result"}, {"query", query}, {"matches", matches}, {"mode", "keyword"}};
        dispatch(res);
    }

    void handle_consolidation(const std::string& cid) {
        std::lock_guard<std::mutex> lock(io_mutex);
        fs::path ledger = base_dir / (cid + ".jsonl");
        fs::path archive = base_dir / (cid + "_consolidated.jsonl");

        std::cout << "[HIPPOCAMPUS] Consolidating engrams for: " << cid << " (REM Phase)" << std::endl;

        if (!fs::exists(ledger)) {
            json resp = {{"cid", cid}, {"origin", "hippocampus"}, {"intent", "consolidation_complete"}};
            dispatch(resp);
            return;
        }

        // Load all engrams from the ledger
        std::vector<json> engrams;
        {
            std::ifstream f(ledger);
            std::string line;
            while (std::getline(f, line)) {
                if (line.empty()) continue;
                try { engrams.push_back(json::parse(line)); } catch (...) {}
            }
        }

        size_t original_count = engrams.size();
        if (original_count == 0) {
            json resp = {{"cid", cid}, {"origin", "hippocampus"}, {"intent", "consolidation_complete"}};
            dispatch(resp);
            return;
        }

        // Sort by importance (desc), then by timestamp (desc) for tie-breaking
        std::sort(engrams.begin(), engrams.end(), [](const json& a, const json& b) {
            double ia = a.value("importance", 1.0);
            double ib = b.value("importance", 1.0);
            if (ia != ib) return ia > ib;
            return a.value("synapse_ts", 0L) > b.value("synapse_ts", 0L);
        });

        // Split: keep recent + high-importance in active ledger, archive the rest
        std::vector<json> keep;
        std::vector<json> to_archive;

        for (size_t i = 0; i < engrams.size(); ++i) {
            double importance = engrams[i].value("importance", 1.0);
            bool is_success = engrams[i].value("success", true);

            // Keep: recent entries, high-importance, or successful executions
            if (i < (size_t)CONSOLIDATION_KEEP_RECENT || importance >= IMPORTANCE_ARCHIVE_THRESHOLD || is_success) {
                keep.push_back(engrams[i]);
            } else {
                to_archive.push_back(engrams[i]);
            }
        }

        // Cap the keep list if still too large (keep only the top entries)
        if (keep.size() > (size_t)CONSOLIDATION_KEEP_RECENT * 2) {
            to_archive.insert(to_archive.end(),
                keep.begin() + CONSOLIDATION_KEEP_RECENT * 2, keep.end());
            keep.resize(CONSOLIDATION_KEEP_RECENT * 2);
        }

        // Append archived engrams to consolidated file
        if (!to_archive.empty()) {
            std::ofstream af(archive, std::ios::app);
            if (af.is_open()) {
                for (const auto& e : to_archive) af << e.dump() << "\n";
            }
        }

        // Rewrite active ledger with kept engrams
        {
            std::ofstream lf(ledger, std::ios::trunc);
            if (lf.is_open()) {
                for (const auto& e : keep) lf << e.dump() << "\n";
            }
        }

        std::cout << "[HIPPOCAMPUS] Consolidated " << cid << ": "
                  << original_count << " → " << keep.size() << " active, "
                  << to_archive.size() << " archived" << std::endl;

        // Also clean up stale pending embeds
        cleanup_pending_embeds();

        json resp = {
            {"cid", cid},
            {"origin", "hippocampus"},
            {"intent", "consolidation_complete"},
            {"kept", (int)keep.size()},
            {"archived", (int)to_archive.size()}
        };
        dispatch(resp);
    }

    // Expire pending embed requests that never got a response
    void cleanup_pending_embeds() {
        long now = std::time(nullptr);
        int expired = 0;
        for (auto it = pending_embeds.begin(); it != pending_embeds.end(); ) {
            if (now - it->second.created_ts > PENDING_EMBED_TIMEOUT_SECS) {
                it = pending_embeds.erase(it);
                expired++;
            } else {
                ++it;
            }
        }
        if (expired > 0) {
            std::cout << "[HIPPOCAMPUS] Expired " << expired << " stale embedding requests" << std::endl;
        }
    }

    // Compact memory_index.jsonl when it exceeds the max entry cap.
    // Keeps highest-importance + most-recent entries; archives the rest.
    void compact_memory_index() {
        std::lock_guard<std::mutex> lock(io_mutex);
        fs::path index_path = base_dir / "memory_index.jsonl";
        if (!fs::exists(index_path)) return;

        // Count entries first (cheap check)
        size_t count = 0;
        {
            std::ifstream f(index_path);
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty()) count++;
            }
        }

        if (count <= MEMORY_INDEX_MAX_ENTRIES) return;

        std::cout << "[HIPPOCAMPUS] Memory index compaction: " << count
                  << " entries (cap=" << MEMORY_INDEX_MAX_ENTRIES << ")" << std::endl;

        // Load all entries
        std::vector<json> entries;
        entries.reserve(count);
        {
            std::ifstream f(index_path);
            std::string line;
            while (std::getline(f, line)) {
                if (line.empty()) continue;
                try { entries.push_back(json::parse(line)); } catch (...) {}
            }
        }

        // Score: importance * 0.6 + recency_score * 0.3 + success * 0.1
        long now = std::time(nullptr);
        auto score = [now](const json& e) -> double {
            double importance = e.value("importance", 1.0);
            long ts = e.value("synapse_ts", 0L);
            double age_hours = std::max(1.0, (double)(now - ts) / 3600.0);
            double recency = 1.0 / std::log2(age_hours + 1.0);  // diminishing decay
            double success_bonus = e.value("success", true) ? 0.1 : 0.0;
            return importance * 0.6 + recency * 0.3 + success_bonus;
        };

        std::sort(entries.begin(), entries.end(),
            [&score](const json& a, const json& b) { return score(a) > score(b); });

        // Keep top entries, discard the rest (they're already in per-CID archives)
        size_t keep = MEMORY_INDEX_MAX_ENTRIES;
        entries.resize(keep);

        // Rewrite atomically via temp file
        fs::path tmp = index_path;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            for (const auto& e : entries) f << e.dump() << "\n";
        }
        fs::rename(tmp, index_path);

        std::cout << "[HIPPOCAMPUS] Memory index compacted: " << count
                  << " → " << keep << " entries" << std::endl;
    }

    // Collect CIDs of largest ledgers for bulk consolidation
    std::vector<std::string> find_large_ledgers(size_t min_bytes = 100 * 1024, int max_count = 20) {
        std::vector<std::pair<std::string, uintmax_t>> ledgers;
        for (const auto& entry : fs::directory_iterator(base_dir)) {
            if (entry.path().extension() == ".jsonl" &&
                entry.path().filename().string().find("_consolidated") == std::string::npos &&
                entry.path().filename() != "memory_index.jsonl") {
                auto size = entry.file_size();
                if (size >= min_bytes) {
                    ledgers.push_back({entry.path().stem().string(), size});
                }
            }
        }
        std::sort(ledgers.begin(), ledgers.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        std::vector<std::string> cids;
        for (size_t i = 0; i < std::min(ledgers.size(), (size_t)max_count); ++i) {
            cids.push_back(ledgers[i].first);
        }
        return cids;
    }

    // ── Phase 7: Trajectory Memory ──────────────────────────────────────

    void track_trajectory_step(const std::string& cid, const json& event) {
        if (cid.empty() || cid == "global_stream") return;

        std::string cmd = event.value("command", "");
        if (cmd.empty()) return;
        bool success = (event.value("status", "") == "success");

        auto& traj = trajectory_buffer[cid];
        if (traj.steps.empty()) {
            traj.cid = cid;
            traj.domain = event.value("domain", "");
            traj.start_ts = std::time(nullptr);
        }
        traj.steps.push_back({cmd, success, std::time(nullptr)});
        traj.last_ts = std::time(nullptr);

        // Finalize stale trajectories
        finalize_stale_trajectories();
    }

    void finalize_stale_trajectories() {
        long now = std::time(nullptr);
        std::vector<std::string> stale;
        for (auto& [cid, traj] : trajectory_buffer) {
            if (now - traj.last_ts >= TRAJECTORY_STALE_SECS && traj.steps.size() >= 2) {
                stale.push_back(cid);
            } else if (now - traj.last_ts >= TRAJECTORY_STALE_SECS * 2) {
                stale.push_back(cid);  // cleanup even single-step stale ones
            }
        }

        for (auto& cid : stale) {
            auto& traj = trajectory_buffer[cid];
            if (traj.steps.size() >= 2) {
                // Build summary string
                std::string summary;
                bool any_success = false;
                for (auto& s : traj.steps) {
                    if (!summary.empty()) summary += " -> ";
                    summary += s.command;
                    if (s.success) any_success = true;
                }

                // Determine outcome from last step
                std::string outcome = traj.steps.back().success ? "success" : "failure";

                json record = {
                    {"cid", traj.cid},
                    {"domain", traj.domain},
                    {"steps", json::array()},
                    {"outcome", outcome},
                    {"summary", summary},
                    {"step_count", (int)traj.steps.size()},
                    {"ts", traj.start_ts}
                };
                for (auto& s : traj.steps) {
                    record["steps"].push_back({
                        {"cmd", s.command}, {"ok", s.success}
                    });
                }

                // Write to trajectories.jsonl
                {
                    std::lock_guard<std::mutex> lock(io_mutex);
                    std::ofstream f("./data/trajectories.jsonl", std::ios::app);
                    if (f.is_open()) f << record.dump() << "\n";
                }

                std::cout << "[HIPPOCAMPUS] Trajectory finalized: " << traj.cid
                          << " (" << traj.steps.size() << " steps, " << outcome << ")" << std::endl;
            }
            trajectory_buffer.erase(cid);
        }
    }

    void handle_trajectory_search(const json& request) {
        std::string query = request.value("query", "");
        std::string cid = request.value("cid", "global");

        // Keyword-based trajectory search (fast, no embedding needed)
        std::vector<json> matches;
        {
            std::lock_guard<std::mutex> lock(io_mutex);
            fs::path traj_path = "./data/trajectories.jsonl";
            if (fs::exists(traj_path)) {
                std::ifstream f(traj_path);
                std::string line;
                // Score by keyword overlap with query
                struct Match { json record; int score; };
                std::vector<Match> scored;

                while (std::getline(f, line)) {
                    if (line.empty()) continue;
                    try {
                        auto j = json::parse(line);
                        std::string summary = j.value("summary", "");
                        std::string domain = j.value("domain", "");
                        int score = 0;
                        // Score: domain match = 3, query substring in summary = 2, "success" outcome = 1
                        if (!domain.empty() && query.find(domain) != std::string::npos) score += 3;
                        if (!query.empty() && summary.find(query) != std::string::npos) score += 2;
                        if (j.value("outcome", "") == "success") score += 1;
                        if (score > 0) scored.push_back({j, score});
                    } catch (...) {}
                }

                std::sort(scored.begin(), scored.end(),
                    [](const Match& a, const Match& b) { return a.score > b.score; });

                for (size_t i = 0; i < std::min(scored.size(), (size_t)3); i++) {
                    matches.push_back(scored[i].record);
                }
            }
        }

        json result = {
            {"cid", cid}, {"origin", "hippocampus"},
            {"intent", "trajectory_result"},
            {"query", query}, {"trajectories", matches}
        };
        dispatch(result);

        if (!matches.empty()) {
            std::cout << "[HIPPOCAMPUS] Trajectory search: " << matches.size()
                      << " matches for '" << query << "'" << std::endl;
        }
    }

    void dispatch(const json& data) {
        routing::publish(pub, data);
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[i+1];
    }
    neuroswarm::HippocampusLobe hippocampus(ip);
    hippocampus.start();
    return 0;
}
