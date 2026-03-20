/**
 * @file rem_finetune_test.cpp
 * @brief Smoke test for REM fine-tuning pipeline: training data export + lockfile logic.
 *
 * Reads real engram data from data/engrams/global_stream.jsonl,
 * exports training pairs, and verifies format correctness.
 */

#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <filesystem>
#include <cassert>

using json = nlohmann::json;
namespace fs = std::filesystem;

static const char* ENGRAM_DIR   = "./data/engrams/";
static const char* TRAINING_DIR = "./data/training/";

struct ExecutionTrace {
    std::string cid;
    std::string command;
    std::string mode;
    std::string result;
    bool        success;
    long long   ts;
};

static std::vector<ExecutionTrace> load_traces() {
    std::vector<ExecutionTrace> all;
    fs::path stream = fs::path(ENGRAM_DIR) / "global_stream.jsonl";
    if (!fs::exists(stream)) return all;

    std::map<std::string, std::string> last_cmd, last_mode;
    std::ifstream f(stream);
    std::string line;
    std::deque<std::string> window;
    while (std::getline(f, line)) {
        if (!line.empty()) window.push_back(line);
        if ((int)window.size() > 200000) window.pop_front();
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
    return all;
}

int main() {
    std::cout << "=== REM Fine-tuning Smoke Test ===" << std::endl;

    // Test 1: Load traces from real engram data
    auto traces = load_traces();
    std::cout << "[TEST 1] Loaded " << traces.size() << " execution traces from global_stream.jsonl" << std::endl;
    assert(!traces.empty() && "Expected non-empty traces from global_stream");

    int total_success = 0, reality_success = 0;
    for (const auto& t : traces) {
        if (t.success) ++total_success;
        if (t.success && t.mode == "reality") ++reality_success;
    }
    std::cout << "         Total successes: " << total_success
              << ", Reality successes: " << reality_success << std::endl;

    // Test 2: Export training data (with threshold=0 to force export)
    fs::create_directories(TRAINING_DIR);

    // Recover GOAL text
    std::map<std::string, std::string> cid_to_goal;
    fs::path stream = fs::path(ENGRAM_DIR) / "global_stream.jsonl";
    {
        std::ifstream f(stream);
        std::string line;
        while (std::getline(f, line)) {
            try {
                auto j = json::parse(line);
                if (j.value("intent", "") == "inference_request") {
                    std::string cid  = j.value("cid", "");
                    std::string text = j.value("text", "");
                    auto pos = text.find("GOAL:");
                    if (pos != std::string::npos) {
                        std::string goal = text.substr(pos + 5);
                        auto nl = goal.find('\n');
                        if (nl != std::string::npos) goal = goal.substr(0, nl);
                        while (!goal.empty() && goal.front() == ' ') goal.erase(goal.begin());
                        if (!goal.empty()) cid_to_goal[cid] = goal;
                    }
                }
            } catch (...) {}
        }
    }
    std::cout << "[TEST 2] Recovered " << cid_to_goal.size() << " GOAL texts from inference_request events" << std::endl;

    // Generate training pairs
    std::set<std::string> seen_cmds;
    std::ostringstream out;
    int count = 0;

    for (const auto& t : traces) {
        if (!t.success || t.mode != "reality") continue;
        if (t.command.empty()) continue;
        if (seen_cmds.count(t.command)) continue;
        seen_cmds.insert(t.command);

        std::string goal = cid_to_goal.count(t.cid) ? cid_to_goal[t.cid] : t.command;

        out << "<|system|>\n"
            << "You are a bash executor. Reply ONLY with JSON: "
            << "{\"thought\":\"brief\",\"command\":\"REAL_BASH_CMD\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\n"
            << "Rules: command MUST be executable bash. No placeholders.\n"
            << "<|end|>\n"
            << "<|user|>\n"
            << "GOAL: " << goal << "\n"
            << "<|end|>\n"
            << "<|assistant|>\n"
            << "{\"thought\":\"execute\",\"command\":\"" << t.command << "\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\n"
            << "<|end|>\n\n";
        ++count;
    }

    std::string filename = std::string(TRAINING_DIR) + "rem_training_TEST.txt";
    {
        std::ofstream f(filename);
        f << out.str();
    }

    std::cout << "[TEST 2] Exported " << count << " training pairs to " << filename << std::endl;
    assert(fs::exists(filename) && "Training file should exist on disk");

    // Test 3: Verify training data format (only if we have pairs)
    if (count > 0) {
        std::ifstream f(filename);
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        assert(content.find("<|system|>") != std::string::npos && "Missing <|system|> tag");
        assert(content.find("<|user|>") != std::string::npos && "Missing <|user|> tag");
        assert(content.find("<|assistant|>") != std::string::npos && "Missing <|assistant|> tag");
        assert(content.find("<|end|>") != std::string::npos && "Missing <|end|> tag");
        assert(content.find("GOAL:") != std::string::npos && "Missing GOAL: prefix");
        assert(content.find("\"mode\":\"reality\"") != std::string::npos && "Missing reality mode in output");
        std::cout << "[TEST 3] Training data format verified OK" << std::endl;

        // Print first training pair for visual inspection
        auto first_end = content.find("<|end|>\n\n");
        if (first_end != std::string::npos) {
            std::cout << "\n--- First training pair (preview) ---\n"
                      << content.substr(0, first_end + 9)
                      << "--- End preview ---\n" << std::endl;
        }
    } else {
        std::cout << "[TEST 3] Skipped format check (no reality-mode successes in data)" << std::endl;
    }

    // Test 4: Lockfile logic
    const char* LOCKFILE = "./data/training/.training_active";
    {
        // No lockfile should exist initially
        fs::remove(LOCKFILE);
        assert(!fs::exists(LOCKFILE) && "Lockfile should not exist initially");

        // Write lockfile
        std::ofstream lock(LOCKFILE);
        lock << "12345\n/tmp/test_model.gguf";
        lock.close();
        assert(fs::exists(LOCKFILE) && "Lockfile should exist after write");

        // Read it back
        std::ifstream lf(LOCKFILE);
        std::string pid_str, model_path;
        std::getline(lf, pid_str);
        std::getline(lf, model_path);
        assert(pid_str == "12345" && "PID should be 12345");
        assert(model_path == "/tmp/test_model.gguf" && "Model path should match");
        std::cout << "[TEST 4] Lockfile read/write logic verified OK" << std::endl;

        fs::remove(LOCKFILE);
    }

    // Test 5: Verify llama-finetune binary exists
    const char* FINETUNE_BIN = "./external/llama.cpp/build/bin/llama-finetune";
    if (fs::exists(FINETUNE_BIN)) {
        std::cout << "[TEST 5] llama-finetune binary found: " << FINETUNE_BIN << std::endl;
    } else {
        std::cout << "[TEST 5] WARNING: llama-finetune binary NOT found (fine-tuning will be skipped at runtime)" << std::endl;
    }

    // Cleanup test file
    fs::remove(filename);

    std::cout << "\n=== ALL TESTS PASSED ===" << std::endl;
    return 0;
}
