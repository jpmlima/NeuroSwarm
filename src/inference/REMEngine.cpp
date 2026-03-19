/**
 * @file REMEngine.cpp
 * @brief Autonomous behavioral learning engine.
 *
 * During sleep cycles (system idle), analyses engram history to extract
 * behavioral patterns — what worked, what failed, under which conditions —
 * and distills them into a system knowledge file that the FrontalExecutive
 * injects into every future prompt. This is prompt-level self-improvement:
 * the cheapest and most reliable form of self-modification for small models.
 */

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <numeric>
#include <deque>
#include <filesystem>
#include <chrono>
#include <thread>
#include <ctime>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class REMEngine {
public:
    REMEngine(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {"initiate_sleep_cycle"});

        fs::create_directories("./data");
        std::cout << "[REM] Sleep & Self-Improvement Engine online." << std::endl;
    }

    void start_circadian_loop() {
        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;
            if (j.value("intent", "") == "initiate_sleep_cycle") {
                enter_rem_sleep();
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    static constexpr const char* KNOWLEDGE_FILE = "./data/system_knowledge.md";
    static constexpr const char* ENGRAM_DIR     = "./data/engrams/";
    static constexpr int         ANALYSIS_WINDOW = 100; // last N execution events

    // -----------------------------------------------------------------------
    struct ExecutionTrace {
        std::string cid;
        std::string command;
        std::string mode;       // reality / dream / neuro_surgery
        std::string result;     // first 200 chars of proprioception
        bool        success;
        long long   ts;
    };

    // -----------------------------------------------------------------------
    void enter_rem_sleep() {
        std::cout << "[REM] Sleep cycle started. Analysing engrams..." << std::endl;

        auto traces = load_recent_traces(ANALYSIS_WINDOW);
        if (traces.empty()) {
            std::cout << "[REM] No execution traces found. Skipping." << std::endl;
            return;
        }

        std::string knowledge = synthesize_knowledge(traces);
        write_knowledge(knowledge);

        // Broadcast so a running FrontalExecutive can update itself live
        json update = {
            {"origin", "rem_engine"}, {"intent", "prompt_update"},
            {"knowledge", knowledge},
            {"learned_from", (int)traces.size()}
        };
        dispatch(update);

        json wakeup = {
            {"origin", "rem_engine"}, {"intent", "sleep_cycle_complete"},
            {"learned_memories", (int)traces.size()}
        };
        dispatch(wakeup);

        std::cout << "[REM] Sleep cycle complete. Knowledge updated from "
                  << traces.size() << " traces." << std::endl;
    }

    // -----------------------------------------------------------------------
    std::vector<ExecutionTrace> load_recent_traces(int limit) {
        std::vector<ExecutionTrace> all;

        if (!fs::exists(ENGRAM_DIR)) return all;

        // Collect execution_result events from global_stream
        fs::path stream = fs::path(ENGRAM_DIR) / "global_stream.jsonl";
        if (!fs::exists(stream)) return all;

        // We also need the corresponding command; track last inference_request per CID
        std::map<std::string, std::string> last_cmd;   // cid → command
        std::map<std::string, std::string> last_mode;  // cid → mode

        std::ifstream f(stream);
        std::string line;
        std::deque<std::string> window;
        while (std::getline(f, line)) {
            if (!line.empty()) window.push_back(line);
            if ((int)window.size() > limit * 20) window.pop_front(); // rolling window
        }

        for (const auto& l : window) {
            try {
                auto j = json::parse(l);
                std::string intent = j.value("intent", "");
                std::string cid    = j.value("cid", "");

                if (intent == "execution_request") {
                    last_cmd[cid]  = j.value("command", "");
                    last_mode[cid] = j.value("mode", "reality");
                } else if (intent == "execution_result") {
                    ExecutionTrace t;
                    t.cid     = cid;
                    t.success = (j.value("status", "") == "success");
                    t.ts      = j.value("synapse_ts", 0LL);
                    t.result  = j.value("proprioception", j.value("output", "")).substr(0, 200);
                    t.command = last_cmd.count(cid) ? last_cmd[cid] : "";
                    t.mode    = last_mode.count(cid) ? last_mode[cid] : j.value("mode", "reality");
                    if (!t.command.empty()) all.push_back(t);
                }
            } catch (...) {}
        }

        // Keep only the most recent `limit` traces
        if ((int)all.size() > limit) {
            all.erase(all.begin(), all.end() - limit);
        }
        return all;
    }

    // -----------------------------------------------------------------------
    std::string synthesize_knowledge(const std::vector<ExecutionTrace>& traces) {
        // --- aggregate stats ---
        int total   = traces.size();
        int success = std::count_if(traces.begin(), traces.end(), [](const auto& t){ return t.success; });
        float rate  = total > 0 ? (float)success / total : 0.f;

        // per-mode stats
        std::map<std::string, std::pair<int,int>> mode_stats; // mode → {success, total}
        for (const auto& t : traces) {
            mode_stats[t.mode].second++;
            if (t.success) mode_stats[t.mode].first++;
        }

        // extract command tokens for success/failure pattern analysis
        auto tokenize = [](const std::string& cmd) {
            std::vector<std::string> tokens;
            std::istringstream ss(cmd);
            std::string tok;
            while (ss >> tok) tokens.push_back(tok);
            return tokens;
        };

        std::map<std::string, int> success_vocab, fail_vocab;
        for (const auto& t : traces) {
            auto tokens = tokenize(t.command);
            for (const auto& tok : tokens) {
                if (tok.size() < 3) continue; // skip tiny tokens
                if (t.success) success_vocab[tok]++;
                else           fail_vocab[tok]++;
            }
        }

        // top-5 tokens more associated with success/failure
        auto top_tokens = [](std::map<std::string,int>& vocab, int n) {
            std::vector<std::pair<int,std::string>> v;
            for (auto& [k, cnt] : vocab) v.push_back({cnt, k});
            std::sort(v.rbegin(), v.rend());
            std::string out;
            for (int i = 0; i < std::min(n, (int)v.size()); ++i)
                out += "`" + v[i].second + "` ";
            return out;
        };

        std::string succ_tokens = top_tokens(success_vocab, 5);
        std::string fail_tokens = top_tokens(fail_vocab,    5);

        // recent failures (last 3)
        std::string recent_failures;
        int fc = 0;
        for (auto it = traces.rbegin(); it != traces.rend() && fc < 3; ++it) {
            if (!it->success) {
                recent_failures += "  - CMD: `" + it->command.substr(0, 80) + "`\n"
                                 + "    ERR: " + it->result.substr(0, 120) + "\n";
                ++fc;
            }
        }
        if (recent_failures.empty()) recent_failures = "  (none — good run)\n";

        // recent successes (last 3)
        std::string recent_successes;
        int sc = 0;
        for (auto it = traces.rbegin(); it != traces.rend() && sc < 3; ++it) {
            if (it->success) {
                recent_successes += "  - CMD: `" + it->command.substr(0, 80) + "`\n";
                ++sc;
            }
        }
        if (recent_successes.empty()) recent_successes = "  (none yet)\n";

        // --- compose knowledge block ---
        std::ostringstream kb;
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        kb << "# BEHAVIORAL KNOWLEDGE (updated " << std::ctime(&now) << ")\n";
        kb << "## Performance Summary\n";
        kb << "- Analysed " << total << " recent executions\n";
        kb << "- Overall success rate: " << (int)(rate * 100) << "%\n";
        kb << "\n## Success Rate by Mode\n";
        for (auto& [mode, s] : mode_stats) {
            float mr = s.second > 0 ? (float)s.first / s.second : 0.f;
            kb << "- `" << mode << "`: " << (int)(mr*100) << "% ("
               << s.first << "/" << s.second << ")\n";
        }
        kb << "\n## Command Vocabulary Analysis\n";
        kb << "- Tools/patterns that correlate with SUCCESS: " << (succ_tokens.empty() ? "(insufficient data)" : succ_tokens) << "\n";
        kb << "- Tools/patterns that correlate with FAILURE: " << (fail_tokens.empty() ? "(insufficient data)" : fail_tokens) << "\n";
        kb << "\n## Recent Failures (learn from these)\n" << recent_failures;
        kb << "\n## Recent Successes (replicate these patterns)\n" << recent_successes;
        kb << "\n## Behavioral Directives (derived from above)\n";

        // Derive directives from data
        if (mode_stats.count("dream") && mode_stats["dream"].second > 0) {
            float dr = (float)mode_stats["dream"].first / mode_stats["dream"].second;
            if (dr < 0.3f)
                kb << "- CAUTION: Dream sandbox has low success rate. Simplify commands before simulation.\n";
        }
        if (mode_stats.count("neuro_surgery") && mode_stats["neuro_surgery"].second > 0) {
            float nr = (float)mode_stats["neuro_surgery"].first / mode_stats["neuro_surgery"].second;
            if (nr < 0.5f)
                kb << "- CAUTION: Neuro-surgery has failed >50% of the time. Prefer single atomic file edits.\n";
        }
        if (rate > 0.7f)
            kb << "- System is performing well. Continue with current command style.\n";
        else if (rate < 0.3f)
            kb << "- System is struggling. Prefer simpler, single-step commands. Avoid complex pipelines.\n";

        return kb.str();
    }

    // -----------------------------------------------------------------------
    void write_knowledge(const std::string& knowledge) {
        std::ofstream f(KNOWLEDGE_FILE);
        f << knowledge;
        std::cout << "[REM] Knowledge file written: " << KNOWLEDGE_FILE << std::endl;
    }

    void dispatch(const json& data) {
        routing::publish(pub, data);
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[i+1];

    neuroswarm::REMEngine rem(ip);
    rem.start_circadian_loop();
    return 0;
}
