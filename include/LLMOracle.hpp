#pragma once
// Autopoiesis Phase D — LLM as last-resort code generation oracle.
//
// Biological analogue: the neocortex. Evolution (variation engine) is slow
// but requires no teacher. The neocortex is fast but expensive. Organisms
// use the neocortex when reflexes and habits fail — not for everything.
//
// The LLMOracle is called ONLY when:
//   1. The Planner identifies a gap (no operator exists)
//   2. The VariationEngine fails to fill the gap
//   3. The system has no template that matches
//
// Every successful LLM output is decomposed into reusable operators and
// templates, reducing future LLM dependency.

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <iostream>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <OperatorRegistry.hpp>

namespace neuroswarm {

class LLMOracle {
public:
    LLMOracle(zmq::socket_t& pub, zmq::socket_t& sub)
        : pub_(pub), sub_(sub) {}

    struct OracleResult {
        bool success;
        std::string command;        // the generated command
        std::string language;       // bash, python, c++
        std::string raw_response;   // full LLM output
        double latency_ms;
        int attempts;               // how many LLM calls were needed
    };

    // Ask the LLM to generate a command that achieves a postcondition.
    // Context: what we know, what we've tried, what failed.
    OracleResult generate_operator(
            const std::string& goal_postcondition,
            const std::vector<std::string>& known_operators,
            const std::vector<std::string>& failed_attempts,
            const std::string& environment_summary,
            int max_attempts = 2) {

        OracleResult result;
        result.success = false;
        result.attempts = 0;

        for (int attempt = 0; attempt < max_attempts; attempt++) {
            result.attempts++;

            std::string prompt = build_prompt(
                goal_postcondition, known_operators,
                failed_attempts, environment_summary, attempt);

            std::cout << "[ORACLE] Requesting LLM inference (attempt "
                      << (attempt + 1) << "/" << max_attempts << ")..." << std::endl;

            auto start = std::chrono::steady_clock::now();

            // Send inference request via the bus
            std::string cid = "oracle_" + std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count());

            nlohmann::json req = {
                {"cid", cid},
                {"origin", "primordial_loop"},
                {"intent", "inference_request"},
                {"adapter", "coder"},
                {"grammar", json_grammar()},
                {"text", prompt}
            };
            routing::publish(pub_, req);

            // Wait for response (with timeout)
            std::string response = wait_for_response(cid, 30000);

            auto end = std::chrono::steady_clock::now();
            result.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

            if (response.empty()) {
                std::cout << "[ORACLE] Timeout — no response from SynapticController." << std::endl;
                continue;
            }

            result.raw_response = response;

            // Parse the JSON response from the LLM
            if (parse_response(response, result)) {
                std::cout << "[ORACLE] Generated: " << result.command
                          << " (" << result.language << ", "
                          << result.latency_ms << "ms)" << std::endl;
                result.success = true;
                total_calls_++;
                total_successes_++;
                return result;
            }

            std::cout << "[ORACLE] Failed to parse response, retrying..." << std::endl;
        }

        total_calls_ += max_attempts;
        return result;
    }

    // Generate a complete script (sensor, tool) for a more complex goal.
    OracleResult generate_script(
            const std::string& description,
            const std::string& language,
            const std::string& environment_summary,
            int max_attempts = 2) {

        OracleResult result;
        result.success = false;
        result.language = language;
        result.attempts = 0;

        for (int attempt = 0; attempt < max_attempts; attempt++) {
            result.attempts++;

            std::string prompt = build_script_prompt(
                description, language, environment_summary, attempt);

            std::string cid = "oracle_script_" + std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count());

            nlohmann::json req = {
                {"cid", cid},
                {"origin", "primordial_loop"},
                {"intent", "inference_request"},
                {"adapter", "coder"},
                {"text", prompt}
            };
            routing::publish(pub_, req);

            std::string response = wait_for_response(cid, 60000);

            if (response.empty()) continue;

            result.raw_response = response;

            // Extract code block from response
            std::string code = extract_code(response, language);
            if (!code.empty()) {
                result.command = code;
                result.success = true;
                total_calls_++;
                total_successes_++;
                return result;
            }
        }

        total_calls_ += max_attempts;
        return result;
    }

    // Stats
    int total_calls() const { return total_calls_; }
    int total_successes() const { return total_successes_; }
    double dependency_ratio() const {
        // How much the system depends on the LLM (0 = independent, 1 = fully dependent)
        // Decreases as the operator registry grows
        return (total_calls_ > 0)
            ? static_cast<double>(total_calls_) / (total_calls_ + 100)
            : 0.0;
    }

private:
    zmq::socket_t& pub_;
    zmq::socket_t& sub_;
    int total_calls_ = 0;
    int total_successes_ = 0;

    std::string build_prompt(
            const std::string& goal,
            const std::vector<std::string>& known_ops,
            const std::vector<std::string>& failed,
            const std::string& env_summary,
            int attempt) {

        std::string prompt = "<|system|>\n"
            "You are a minimal bash/python command generator for an autonomous system.\n"
            "Reply ONLY with JSON: {\"command\":\"EXECUTABLE_CMD\",\"language\":\"bash\"}\n"
            "The command must be a single executable line. No explanations.\n"
            "No placeholders. No sudo. No interactive commands.\n"
            "<|end|>\n<|user|>\n";

        prompt += "ENVIRONMENT: " + env_summary + "\n";
        prompt += "GOAL: Generate a command that achieves: " + goal + "\n";

        if (!known_ops.empty()) {
            prompt += "KNOWN COMMANDS (for reference): ";
            int shown = 0;
            for (const auto& op : known_ops) {
                if (shown++ >= 5) break;
                prompt += op + "; ";
            }
            prompt += "\n";
        }

        if (!failed.empty() && attempt > 0) {
            prompt += "FAILED ATTEMPTS (do NOT repeat): ";
            for (const auto& f : failed) prompt += f + "; ";
            prompt += "\n";
        }

        prompt += "<|end|>\n<|assistant|>\n";
        return prompt;
    }

    std::string build_script_prompt(
            const std::string& description,
            const std::string& language,
            const std::string& env_summary,
            int attempt) {

        std::string prompt = "<|system|>\n"
            "You are a code generator. Write a complete, runnable " + language + " script.\n"
            "Output ONLY the code. No markdown, no explanations, no ```.\n"
            "The script must be self-contained and exit cleanly.\n"
            "<|end|>\n<|user|>\n";

        prompt += "ENVIRONMENT: " + env_summary + "\n";
        prompt += "TASK: " + description + "\n";

        if (language == "python") {
            prompt += "REQUIREMENTS: Use only standard library + zmq + json.\n"
                      "Connect to ZMQ bus at tcp://localhost:5555 for publishing.\n";
        }

        if (attempt > 0) {
            prompt += "NOTE: Previous attempt failed. Try a simpler approach.\n";
        }

        prompt += "<|end|>\n<|assistant|>\n";
        return prompt;
    }

    std::string wait_for_response(const std::string& cid, int timeout_ms) {
        auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_ms);

        while (std::chrono::steady_clock::now() < deadline) {
            auto j = routing::receive(sub_, zmq::recv_flags::dontwait);
            if (!j.is_null()) {
                if (j.value("intent", "") == "inference_result" &&
                    j.value("cid", "") == cid) {
                    return j.value("text", "");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        return "";
    }

    bool parse_response(const std::string& response, OracleResult& result) {
        // Try to parse as JSON
        try {
            // Find first { and last }
            auto start = response.find('{');
            auto end = response.rfind('}');
            if (start == std::string::npos || end == std::string::npos) return false;

            std::string json_str = response.substr(start, end - start + 1);
            auto j = nlohmann::json::parse(json_str);

            result.command = j.value("command", "");
            result.language = j.value("language", "bash");

            return !result.command.empty();
        } catch (...) {
            // Try to extract a bare command (no JSON wrapper)
            std::string trimmed = response;
            // Remove leading/trailing whitespace and quotes
            while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\n' ||
                   trimmed[0] == '"' || trimmed[0] == '\''))
                trimmed.erase(0, 1);
            while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n' ||
                   trimmed.back() == '"' || trimmed.back() == '\''))
                trimmed.pop_back();

            if (!trimmed.empty() && trimmed.size() < 200) {
                result.command = trimmed;
                result.language = "bash";
                return true;
            }
        }
        return false;
    }

    static std::string extract_code(const std::string& response, const std::string& language) {
        // Try to find code between ``` markers
        auto start = response.find("```");
        if (start != std::string::npos) {
            auto code_start = response.find('\n', start);
            if (code_start == std::string::npos) return "";
            code_start++;
            auto code_end = response.find("```", code_start);
            if (code_end == std::string::npos) code_end = response.size();
            return response.substr(code_start, code_end - code_start);
        }

        // No markers — treat entire response as code if it looks like code
        if (language == "python" && response.find("import ") != std::string::npos) {
            return response;
        }
        if (language == "bash" && (response.find("#!/") != std::string::npos ||
            response.find("echo ") != std::string::npos)) {
            return response;
        }

        // Last resort: return if short enough to be a script
        if (response.size() < 2000) return response;

        return "";
    }

    static std::string json_grammar() {
        return R"(root   ::= object
object ::= "{" ws ( pair ( "," ws pair )* )? "}"
pair   ::= string ":" ws value
value  ::= string | number | object | array | "true" | "false" | "null"
string ::= "\"" ( [^"\\\n\r] | "\\" ( ["\\/bfnrt] | "u" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] ) )* "\""
number ::= "-"? ( [0-9] | [1-9] [0-9]* ) ( "." [0-9]+ )? ( [eE] [-+]? [0-9]+ )?
array  ::= "[" ws ( value ( "," ws value )* )? "]"
ws     ::= [ \t\n\r]*)";
    }
};

} // namespace neuroswarm
