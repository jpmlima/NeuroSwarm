#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <ctime>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class HippocampusLobe {
public:
    HippocampusLobe(const std::string& pub_addr = "tcp://localhost:5555", 
                   const std::string& sub_addr = "tcp://localhost:5556",
                   const std::string& storage_path = "./data/engrams") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub), base_dir(storage_path) {
        
        pub.connect(pub_addr);
        sub.connect(sub_addr);
        sub.set(zmq::sockopt::subscribe, ""); 

        if (!fs::exists(base_dir)) {
            fs::create_directories(base_dir);
        }

        std::cout << "[HIPPOCAMPUS] Memory system active." << std::endl;
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    if (raw.empty() || raw[0] != '{') continue;
                    auto j = json::parse(raw);
                    process_neural_event(j);
                } catch (...) {}
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    fs::path base_dir;
    std::mutex io_mutex;

    void process_neural_event(const json& event) {
        std::string intent = event.value("intent", "");
        std::string cid = event.value("cid", "global_stream");

        if (intent == "recall_memory") {
            handle_recall(cid, event);
        } 
        else if (intent == "search_memory") {
            handle_search(event);
        }
        else if (intent == "consolidate_memories") {
            handle_consolidation(cid);
        }
        else {
            // Standard record of experience
            record_engram(cid, event);
            // Also mirror to global stream for cross-pollination
            if (cid != "global_stream") {
                record_engram("global_stream", event);
            }
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
        std::lock_guard<std::mutex> lock(io_mutex);
        std::string query = request.value("query", "");
        std::vector<json> matches;

        std::cout << "[HIPPOCAMPUS] Global Search: '" << query << "'" << std::endl;

        // Scan all engrams for keywords (Simple semantic search)
        for (const auto& entry : fs::directory_iterator(base_dir)) {
            if (entry.path().extension() == ".jsonl") {
                std::ifstream file(entry.path());
                std::string line;
                while (std::getline(file, line)) {
                    if (line.find(query) != std::string::npos) {
                        matches.push_back(json::parse(line));
                        if (matches.size() >= 5) break; // Limit results
                    }
                }
            }
            if (matches.size() >= 5) break;
        }

        json search_result = {
            {"cid", request.value("cid", "global")},
            {"origin", "hippocampus"},
            {"intent", "search_result"},
            {"query", query},
            {"matches", matches}
        };
        dispatch(search_result);
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
        std::string payload = data.dump();
        zmq::message_t msg(payload.size());
        memcpy(msg.data(), payload.c_str(), payload.size());
        pub.send(msg, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::HippocampusLobe hippocampus;
    hippocampus.start();
    return 0;
}
