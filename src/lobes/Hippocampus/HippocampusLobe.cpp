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
            "embedding_result", "execution_result"
        });

        if (!fs::exists(base_dir)) {
            fs::create_directories(base_dir);
        }

        std::cout << "[HIPPOCAMPUS] Connected to Thalamus at " << thalamus_ip << std::endl;
    }

    void start() {
        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;
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
    struct PendingEmbed { json engram; };
    std::map<std::string, PendingEmbed> pending_embeds;

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
            handle_consolidation(cid);
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
        pending_embeds[embed_cid] = {seed};

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
        
        // In a real biological scenario, this would involve summarizing with an LLM.
        // For now, we move old engrams to an archive to keep the active ledger fast.
        if (fs::exists(ledger)) {
            fs::rename(ledger, archive);
            std::ofstream new_ledger(ledger); // Reset active memory
        }

        json resp = {
            {"cid", cid},
            {"origin", "hippocampus"},
            {"intent", "consolidation_complete"}
        };
        dispatch(resp);
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
