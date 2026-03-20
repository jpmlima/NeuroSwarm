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
#include <set>
#include <algorithm>
#include <numeric>
#include <deque>
#include <filesystem>
#include <chrono>
#include <thread>
#include <ctime>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <functional>

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

    static constexpr const char* KNOWLEDGE_FILE  = "./data/system_knowledge.md";
    static constexpr const char* ENGRAM_DIR      = "./data/engrams/";
    static constexpr const char* TRAINING_DIR    = "./data/training/";
    static constexpr const char* LOCKFILE        = "./data/training/.training_active";
    static constexpr const char* FINETUNE_BIN    = "./external/llama.cpp/build/bin/llama-finetune";
    static constexpr const char* BASE_MODEL      = "./models/qwen2.5-1.5b-instruct-q4_k_m.gguf";
    static constexpr const char* FINETUNED_DIR   = "./models/finetuned/";
    static constexpr const char* LORA_DIR        = "./models/lora/";
    static constexpr int         ANALYSIS_WINDOW = 100; // last N execution events
    static constexpr int         MIN_NEW_TRACES  = 20;  // minimum new successes before export
    static constexpr const char* GENOME_PATH     = "./data/command_genome.json";

    int last_export_count = 0; // successful traces at last export
    pid_t training_pid    = 0; // PID of active llama-finetune subprocess

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

        // Step 1: Check if a previous fine-tuning run completed
        check_training_status();

        auto traces = load_recent_traces(ANALYSIS_WINDOW);
        if (traces.empty()) {
            std::cout << "[REM] No execution traces found. Skipping." << std::endl;
            return;
        }

        // Step 2: Synthesize prompt-level knowledge (existing)
        std::string knowledge = synthesize_knowledge(traces);
        write_knowledge(knowledge);

        // Broadcast so a running FrontalExecutive can update itself live
        json update = {
            {"origin", "rem_engine"}, {"intent", "prompt_update"},
            {"knowledge", knowledge},
            {"learned_from", (int)traces.size()}
        };
        dispatch(update);

        // Step 2b: Phase 4 — Genome crossover & pruning
        evolve_genome(traces);

        // Step 3: Export training data if enough new successes
        int successes = std::count_if(traces.begin(), traces.end(),
                                      [](const auto& t){ return t.success && t.mode == "reality"; });
        if (successes >= last_export_count + MIN_NEW_TRACES) {
            std::string training_file = export_training_data(traces);
            if (!training_file.empty()) {
                last_export_count = successes;
                // Step 4: Trigger fine-tune if not already running
                trigger_finetune(training_file);
            }
        }

        json wakeup = {
            {"origin", "rem_engine"}, {"intent", "sleep_cycle_complete"},
            {"learned_memories", (int)traces.size()}
        };
        dispatch(wakeup);

        std::cout << "[REM] Sleep cycle complete. Knowledge updated from "
                  << traces.size() << " traces." << std::endl;
    }

    // -----------------------------------------------------------------------
    // Export successful reality-mode traces as chat-template training data
    std::string export_training_data(const std::vector<ExecutionTrace>& traces) {
        fs::create_directories(TRAINING_DIR);

        // Recover GOAL text from global_stream by correlating CIDs
        std::map<std::string, std::string> cid_to_goal;
        fs::path stream = fs::path(ENGRAM_DIR) / "global_stream.jsonl";
        if (fs::exists(stream)) {
            std::ifstream f(stream);
            std::string line;
            while (std::getline(f, line)) {
                try {
                    auto j = json::parse(line);
                    if (j.value("intent", "") == "inference_request") {
                        std::string cid  = j.value("cid", "");
                        std::string text = j.value("text", "");
                        // Extract GOAL from prompt text
                        auto pos = text.find("GOAL:");
                        if (pos != std::string::npos) {
                            std::string goal = text.substr(pos + 5);
                            // Trim to first newline
                            auto nl = goal.find('\n');
                            if (nl != std::string::npos) goal = goal.substr(0, nl);
                            // Trim whitespace
                            while (!goal.empty() && goal.front() == ' ') goal.erase(goal.begin());
                            if (!goal.empty()) cid_to_goal[cid] = goal;
                        }
                    }
                } catch (...) {}
            }
        }

        // Deduplicate by command hash
        std::set<std::string> seen_cmds;
        std::ostringstream out;
        int count = 0;

        for (const auto& t : traces) {
            if (!t.success || t.mode != "reality") continue;
            if (t.command.empty()) continue;
            if (seen_cmds.count(t.command)) continue;
            seen_cmds.insert(t.command);

            std::string goal = cid_to_goal.count(t.cid) ? cid_to_goal[t.cid] : t.command;

            // Escape command for JSON embedding
            std::string escaped_cmd = t.command;
            for (size_t p = 0; (p = escaped_cmd.find('"', p)) != std::string::npos; p += 2)
                escaped_cmd.insert(p, "\\");

            // JSONL format: one training sample per line
            json sample = {{"text",
                "<|system|>\nYou are a bash executor. Reply ONLY with JSON: "
                "{\"thought\":\"brief\",\"command\":\"REAL_BASH_CMD\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\n"
                "Rules: command MUST be executable bash. No placeholders.\n<|end|>\n"
                "<|user|>\nGOAL: " + goal + "\n<|end|>\n"
                "<|assistant|>\n{\"thought\":\"execute\",\"command\":\"" + escaped_cmd + "\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\n<|end|>"}};
            out << sample.dump() << "\n";
            ++count;
        }

        if (count == 0) {
            std::cout << "[REM] No new unique successful traces to export." << std::endl;
            return "";
        }

        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        std::string filename = std::string(TRAINING_DIR) + "rem_training_" + std::to_string(epoch) + ".jsonl";

        std::ofstream f(filename);
        f << out.str();
        std::cout << "[REM] Exported " << count << " training pairs to " << filename << std::endl;
        return filename;
    }

    // -----------------------------------------------------------------------
    // Spawn llama-finetune as a CPU-only subprocess
    void trigger_finetune(const std::string& training_file) {
        // Check lockfile — don't run two fine-tunes concurrently
        if (fs::exists(LOCKFILE)) {
            std::cout << "[REM] Fine-tuning already in progress (lockfile exists). Skipping." << std::endl;
            return;
        }
        if (!fs::exists(FINETUNE_BIN)) {
            std::cout << "[REM] llama-finetune binary not found at " << FINETUNE_BIN << ". Skipping." << std::endl;
            return;
        }
        if (!fs::exists(BASE_MODEL)) {
            std::cout << "[REM] Base model not found at " << BASE_MODEL << ". Skipping." << std::endl;
            return;
        }

        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Try LoRA fine-tuning first (lighter, hot-loadable)
        bool use_lora = true;
        std::string output_path;

        if (use_lora) {
            fs::create_directories(LORA_DIR);
            output_path = std::string(LORA_DIR) + "rem_lora_" + std::to_string(epoch) + ".gguf";
        } else {
            fs::create_directories(FINETUNED_DIR);
            output_path = std::string(FINETUNED_DIR) + "qwen2.5-rem-" + std::to_string(epoch) + ".gguf";
        }

        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "[REM] fork() failed for llama-finetune." << std::endl;
            return;
        }

        if (pid == 0) {
            // Child process — exec llama-finetune
            if (use_lora) {
                execl(FINETUNE_BIN, "llama-finetune",
                      "-m", BASE_MODEL,
                      "-f", training_file.c_str(),
                      "--lora-out", output_path.c_str(),
                      "-ngl", "0",
                      "-c", "512",
                      "-b", "4",
                      "-ub", "4",
                      "--epochs", "2",
                      "--learning-rate", "1e-5",
                      (char*)nullptr);
            } else {
                execl(FINETUNE_BIN, "llama-finetune",
                      "-m", BASE_MODEL,
                      "-f", training_file.c_str(),
                      "-o", output_path.c_str(),
                      "-ngl", "0",
                      "-c", "512",
                      "-b", "4",
                      "-ub", "4",
                      "--epochs", "2",
                      "--learning-rate", "1e-5",
                      (char*)nullptr);
            }
            // exec failed
            _exit(1);
        }

        // Parent — record PID, type, and output path in lockfile
        training_pid = pid;
        std::ofstream lock(LOCKFILE);
        lock << pid << "\n" << output_path << "\n" << (use_lora ? "lora" : "model");
        std::cout << "[REM] Fine-tuning started (PID " << pid << ", "
                  << (use_lora ? "LoRA" : "full model") << "). Output: " << output_path << std::endl;
    }

    // -----------------------------------------------------------------------
    // Non-blocking check on a previously spawned fine-tuning subprocess
    void check_training_status() {
        if (training_pid <= 0) {
            // Try to recover PID from lockfile (e.g. after restart)
            if (fs::exists(LOCKFILE)) {
                std::ifstream lock(LOCKFILE);
                std::string pid_str, model_path;
                std::getline(lock, pid_str);
                std::getline(lock, model_path);
                try { training_pid = std::stoi(pid_str); } catch (...) { training_pid = 0; }
                if (training_pid <= 0) {
                    fs::remove(LOCKFILE);
                    return;
                }
            } else {
                return;
            }
        }

        int status = 0;
        pid_t result = waitpid(training_pid, &status, WNOHANG);

        if (result == 0) {
            // Still running
            return;
        }

        // Read output model path and type from lockfile
        std::string output_model, training_type = "model";
        if (fs::exists(LOCKFILE)) {
            std::ifstream lock(LOCKFILE);
            std::string pid_str;
            std::getline(lock, pid_str);
            std::getline(lock, output_model);
            std::getline(lock, training_type);
            if (training_type.empty()) training_type = "model";
        }

        if (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            std::cout << "[REM] Fine-tuning complete! Output: " << output_model
                      << " (type: " << training_type << ")" << std::endl;

            json tc = {
                {"origin", "rem_engine"},
                {"intent", "training_complete"},
                {"model_path", output_model},
                {"type", training_type},
                {"adapter_name", "rem_latest"}
            };
            dispatch(tc);
        } else {
            std::cerr << "[REM] Fine-tuning failed or was killed (PID " << training_pid << ")." << std::endl;
        }

        training_pid = 0;
        fs::remove(LOCKFILE);
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
    // Phase 4: Command Genome — Crossover & Pruning during REM sleep
    void evolve_genome(const std::vector<ExecutionTrace>& traces) {
        if (!fs::exists(GENOME_PATH)) {
            std::cout << "[REM] No command genome found. Skipping evolution." << std::endl;
            return;
        }

        json genome;
        {
            std::ifstream f(GENOME_PATH);
            try { f >> genome; } catch (...) { return; }
        }

        bool modified = false;

        for (auto& [domain, templates] : genome.items()) {
            if (!templates.is_array() || templates.size() < 2) continue;

            // Prune: remove templates with fitness < 0.1 and generation > 20
            std::vector<json> survivors;
            for (auto& t : templates) {
                float fit = t.value("fitness", 0.5f);
                int gen = t.value("generation", 0);
                if (fit < 0.1f && gen > 20) {
                    std::cout << "[REM] Pruning dead template in '" << domain
                              << "': gen=" << gen << " fitness=" << fit << std::endl;
                    modified = true;
                    continue;
                }
                survivors.push_back(t);
            }

            // Crossover: combine pipe stages from two high-fitness templates
            if (survivors.size() >= 2) {
                // Sort by fitness descending
                std::sort(survivors.begin(), survivors.end(),
                    [](const json& a, const json& b) {
                        return a.value("fitness", 0.0f) > b.value("fitness", 0.0f);
                    });

                std::string cmd1 = survivors[0].value("cmd", "");
                std::string cmd2 = survivors[1].value("cmd", "");

                // Simple crossover: if both have pipes, swap last pipe stage
                auto pipe1 = cmd1.rfind(" | ");
                auto pipe2 = cmd2.rfind(" | ");

                if (pipe1 != std::string::npos && pipe2 != std::string::npos) {
                    std::string offspring = cmd1.substr(0, pipe1) + cmd2.substr(pipe2);
                    // Check offspring doesn't already exist
                    bool exists = false;
                    for (auto& s : survivors) {
                        if (s.value("cmd", "") == offspring) { exists = true; break; }
                    }
                    if (!exists && offspring.size() <= 200) {
                        int max_gen = 0;
                        for (auto& s : survivors) max_gen = std::max(max_gen, s.value("generation", 0));

                        json child = {
                            {"cmd", offspring},
                            {"fitness", (survivors[0].value("fitness", 0.5f) + survivors[1].value("fitness", 0.5f)) / 2.0f},
                            {"generation", max_gen + 1}
                        };
                        survivors.push_back(child);
                        modified = true;
                        std::cout << "[REM] Genome crossover in '" << domain << "': " << offspring << std::endl;
                    }
                }
            }

            // Cap at 20 templates per domain
            while (survivors.size() > 20) {
                survivors.pop_back(); // remove lowest fitness (already sorted)
                modified = true;
            }

            templates = survivors;
        }

        if (modified) {
            std::ofstream f(GENOME_PATH);
            if (f.is_open()) {
                f << genome.dump(2);
                std::cout << "[REM] Command genome evolved and saved." << std::endl;
            }
        }
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
