/**
 * @file EngramLogger.cpp
 * @brief Biomimetic state-persistence engine.
 * 
 * Records and retrieves atomic Engram Traces into the Cerebral Matrix ledger.
 * Enables Synaptic Replay for crash recovery and Iterative Resonance.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class EngramLogger {
public:
    EngramLogger(const std::string& matrix_path) 
        : base_dir(fs::path(matrix_path) / ".matrix" / "engrams") {
        
        if (!fs::exists(base_dir)) {
            fs::create_directories(base_dir);
        }
    }

    /**
     * @brief Records a new Engram Trace (Synaptic Firing).
     */
    bool record(const std::string& stream_id, const json& trace_data) {
        std::lock_guard<std::mutex> lock(sync_mutex);
        fs::path ledger_path = base_dir / (stream_id + ".jsonl");
        
        std::ofstream ledger(ledger_path, std::ios::app);
        if (!ledger.is_open()) return false;

        json engram = trace_data;
        engram["synapse_ts"] = std::time(nullptr);
        
        ledger << engram.dump() << "
";
        return true;
    }

    /**
     * @brief Retrieves the most recent Engram for Synaptic Replay.
     * Essential for restoring state after a disconnection or failure.
     */
    json retrieve_last_trace(const std::string& stream_id) {
        std::lock_guard<std::mutex> lock(sync_mutex);
        fs::path ledger_path = base_dir / (stream_id + ".jsonl");
        
        if (!fs::exists(ledger_path)) return json::object();

        std::ifstream ledger(ledger_path);
        std::string line, last_line;
        while (std::getline(ledger, line)) {
            if (!line.empty()) last_line = line;
        }

        if (last_line.empty()) return json::object();
        
        try {
            return json::parse(last_line);
        } catch (...) {
            return json::object();
        }
    }

private:
    fs::path base_dir;
    std::mutex sync_mutex;
};

} // namespace neuroswarm
