/**
 * @file Hippocampus.cpp
 * @brief Memory Consolidation and Semantic Retrieval Lobe.
 * 
 * Indexes Engram Traces into a vector space and handles memory queries
 * from the Frontal Executive.
 */

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace neuroswarm {

/**
 * @struct MemoryAtom
 * @brief A consolidated memory grain with a placeholder for a semantic vector.
 */
struct MemoryAtom {
    std::string cid;
    std::string content;
    std::vector<float> embedding; // Placeholder for high-dimensional vector
};

class Hippocampus {
public:
    Hippocampus(const std::string& bus_addr = "tcp://localhost:5555") 
        : ctx(1), bus(ctx, zmq::socket_type::dealer) {
        
        bus.set(zmq::sockopt::routing_id, "hippocampus");
        bus.connect(bus_addr);
        std::cout << "[HIPPOCAMPUS] Memory Consolidation online. Connected to Nervous Bus." << std::endl;
    }

    void start_consolidation() {
        while (true) {
            zmq::message_t msg;
            auto res = bus.recv(msg, zmq::recv_flags::none);
            if (res) {
                std::string payload(static_cast<char*>(msg.data()), msg.size());
                process_bead(payload);
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t bus;
    std::vector<MemoryAtom> long_term_cache; // Representing the RAM-based HNSW index

    void process_bead(const std::string& raw_data) {
        try {
            json stimulus = json::parse(raw_data);
            std::string intent = stimulus.value("intent", "idle");

            if (intent == "query_memory") {
                recall(stimulus);
            } else if (stimulus.contains("status") && stimulus["status"] == "completed") {
                consolidate(stimulus);
            }
        } catch (std::exception& e) {
            std::cerr << "[HIPPOCAMPUS] Consolidation Error: " << e.what() << std::endl;
        }
    }

    /**
     * @brief Consolidates an Engram into the long-term vector store.
     */
    void consolidate(const json& engram) {
        std::cout << "[HIPPOCAMPUS] Consolidating engram into long-term memory: " << engram.value("cid", "?") << std::endl;
        
        MemoryAtom atom;
        atom.cid = engram.value("cid", "");
        atom.content = engram.dump();
        // In production, we would call the ModelManager to get an embedding here
        atom.embedding = {0.1f, 0.2f, 0.3f}; // Mock vector
        
        long_term_cache.push_back(atom);
    }

    /**
     * @brief Performs semantic search to retrieve past knowledge.
     */
    void recall(const json& query_stimulus) {
        std::string query = query_stimulus.value("query", "");
        std::cout << "[HIPPOCAMPUS] Recalling memories for: " << query << std::endl;

        // Mock semantic search result
        json recall_engram = {
            {"cid", query_stimulus.value("cid", "unknown")},
            {"origin", "hippocampus"},
            {"intent", "memory_recalled"},
            {"memories", {"Previous project state found...", "User preferred C++ optimization..."}}
        };

        std::string response = recall_engram.dump();
        zmq::message_t z_msg(response.size());
        memcpy(z_msg.data(), response.c_str(), response.size());
        bus.send(z_msg, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::Hippocampus hippocampus;
    hippocampus.start_consolidation();
    return 0;
}
