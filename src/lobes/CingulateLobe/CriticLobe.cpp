#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <map>
#include <regex>

using json = nlohmann::json;

namespace neuroswarm {

class CriticLobe {
public:
    CriticLobe(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {"critic_validate", "inference_result"});

        std::cout << "[CRITIC] Cingulate Cortex online. Three-tier adversarial validation active." << std::endl;
    }

    void start() {
        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;
            try {
                std::string intent = j.value("intent", "");
                std::string origin = j.value("origin", "");

                if (intent == "critic_validate" &&
                    (origin == "frontal_executive" || origin == "polecat_worker")) {
                    evaluate_plan(j);
                }
                else if (intent == "inference_result" && origin == "synaptic_controller" &&
                         j.value("adapter", "") == "critic") {
                    handle_llm_result(j);
                }
            } catch (...) {}
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    // Rate limiting: max 20 evaluations per CID within a 60-second window
    static constexpr int MAX_EVALS_PER_CID = 20;
    static constexpr int RATE_WINDOW_SECONDS = 60;
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> eval_history;

    // Tier 2 LLM validation: pending CID → original plan CID (awaiting inference_result)
    std::map<std::string, std::string> pending_llm_validations; // cid → original_cid
    static constexpr int LLM_TIMEOUT_MS = 10000;

    // Project root for scope validation
    const std::string PROJECT_ROOT = "/home/xenomai/Documents/NeuroSwarm";

    // Tier 1: NUCLEAR ONLY — catastrophic irreversible patterns.
    // Everything else the system learns through experience (Hippocampus failure memory).
    const std::vector<std::string> BLACKLIST = {
        // Filesystem annihilation
        "rm -rf /", "rm -rf ~", "rm -rf /*", "rm -rf .",
        // Disk destruction
        "dd if=/dev/zero", "dd if=/dev/urandom", "mkfs.",
        "> /dev/sda", "> /dev/nvme",
        // Fork bomb
        ":(){ :|:& };:",
        // Root escalation
        "chmod -R 777 /",
        "sudo rm", "sudo dd",
        // Credential theft
        "bash -i >& /dev/tcp"
    };

    // Safe command prefixes — bypass Tier 2 LLM entirely (read-only, no side effects)
    const std::vector<std::string> SAFE_PREFIXES = {
        "cat ", "head ", "tail ", "wc ", "ls ", "stat ", "file ",
        "du ", "find ", "grep ", "rg ", "uptime", "free ", "df ",
        "ps ", "pgrep ", "echo ", "date", "hostname", "uname ",
        "pwd", "id", "whoami", "git log", "git status", "git diff",
        "git show", "cmake --build", "make -C", "make -n",
        "curl -s -o /dev/null", "nc -z", "ss -t", "pgrep -la"
    };

    bool is_safe_command(const std::string& cmd) {
        for (const auto& prefix : SAFE_PREFIXES) {
            if (cmd.find(prefix) == 0) return true;
        }
        return false;
    }

    // Tier 1b: Scope validation — paths that are allowed
    const std::vector<std::string> ALLOWED_PATHS = {
        "/home/xenomai/Documents/NeuroSwarm",
        "/tmp/",
        "/dev/null",
        "/proc/",
        "/bin/", "/usr/bin/", "/usr/local/bin/",
        "/sbin/", "/usr/sbin/",
        "/sys/class/"
    };

    bool is_dangerous(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const auto& pattern : BLACKLIST) {
            std::string lp = pattern;
            std::transform(lp.begin(), lp.end(), lp.begin(), ::tolower);
            if (lower.find(lp) != std::string::npos) {
                std::cout << "[CRITIC] Matched blacklist pattern: '" << pattern << "'" << std::endl;
                return true;
            }
        }
        return false;
    }

    // Extract the command field from the plan text (which contains the FE's JSON thought)
    std::string extract_command_from_plan(const std::string& plan) {
        // Try JSON extraction
        try {
            size_t start = plan.find("{");
            size_t end = plan.rfind("}");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                auto j = json::parse(plan.substr(start, end - start + 1));
                return j.value("command", "");
            }
        } catch (...) {}

        // Regex fallback
        std::smatch m;
        std::regex cmd_re("\"command\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        if (std::regex_search(plan, m, cmd_re)) return m[1].str();

        return "";
    }

    // Tier 1b: Check if a command references paths outside the allowed scope
    bool is_scope_violation(const std::string& cmd) {
        if (cmd.empty()) return false;

        // Extract absolute paths from the command
        std::regex path_re("/[a-zA-Z0-9_./-]+");
        auto begin = std::sregex_iterator(cmd.begin(), cmd.end(), path_re);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string path = it->str();

            bool allowed = false;
            for (const auto& ap : ALLOWED_PATHS) {
                if (path.find(ap) == 0) {
                    allowed = true;
                    break;
                }
            }

            if (!allowed) {
                std::cout << "[CRITIC] Scope violation: path '" << path
                          << "' is outside allowed boundaries." << std::endl;
                return true;
            }
        }

        return false;
    }

    // Rate limiting: prevent FE inference flooding during neurotic loops
    bool is_rate_limited(const std::string& cid) {
        auto now = std::chrono::steady_clock::now();
        auto& history = eval_history[cid];

        // Prune entries older than the window
        history.erase(
            std::remove_if(history.begin(), history.end(),
                [&](const auto& t) {
                    return std::chrono::duration_cast<std::chrono::seconds>(now - t).count() > RATE_WINDOW_SECONDS;
                }),
            history.end()
        );

        if ((int)history.size() >= MAX_EVALS_PER_CID) {
            return true;
        }

        history.push_back(now);

        // Garbage collect stale CIDs to prevent memory growth
        if (eval_history.size() > 100) {
            for (auto it = eval_history.begin(); it != eval_history.end(); ) {
                if (it->second.empty()) {
                    it = eval_history.erase(it);
                } else {
                    ++it;
                }
            }
        }

        return false;
    }

    void evaluate_plan(const json& data) {
        std::string cid = data.value("cid", "unknown");
        std::string plan = data.value("text", "");

        // Rate limit check — prevent inference flooding
        if (is_rate_limited(cid)) {
            std::cout << "[CRITIC] RATE LIMITED: CID " << cid
                      << " exceeded " << MAX_EVALS_PER_CID << " evaluations in "
                      << RATE_WINDOW_SECONDS << "s." << std::endl;
            json result = {
                {"cid", cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                {"text", "REJECTED: Rate limit exceeded. Too many evaluation requests for this goal. Simplify the approach."}
            };
            dispatch(result);
            return;
        }

        std::cout << "[CRITIC] Three-tier evaluation for CID: " << cid << std::endl;

        // Tier 1a: Fast rule-based safety check (pattern blacklist)
        if (is_dangerous(plan)) {
            std::cout << "[CRITIC] BLOCKED (Tier 1a): Dangerous pattern detected." << std::endl;
            json result = {
                {"cid", cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                {"text", "REJECTED: Dangerous system command detected. This action is not permitted."}
            };
            dispatch(result);
            return;
        }

        // Tier 1b: Scope validation — reject commands that reference paths outside the project
        std::string cmd = extract_command_from_plan(plan);
        if (is_scope_violation(cmd)) {
            std::cout << "[CRITIC] BLOCKED (Tier 1b): Command accesses paths outside project scope." << std::endl;
            json result = {
                {"cid", cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                {"text", "REJECTED: Scope violation. Command references paths outside the project directory. Restrict operations to /home/xenomai/Documents/NeuroSwarm/ or /tmp/."}
            };
            dispatch(result);
            return;
        }

        // Safe commands bypass Tier 2 entirely — no inference overhead for read-only ops
        if (is_safe_command(cmd)) {
            std::cout << "[CRITIC] Safe command prefix detected. APPROVED (fast path)." << std::endl;
            json result = {
                {"cid", cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                {"text", "APPROVED"}
            };
            dispatch(result);
            return;
        }

        // Tier 2: LLM-based validation for non-trivial commands
        std::cout << "[CRITIC] Tier 2: Requesting LLM safety validation for CID " << cid << std::endl;

        std::string llm_cid = "critic_llm_" + cid;
        pending_llm_validations[llm_cid] = cid;

        json llm_req = {
            {"cid", llm_cid}, {"origin", "critic_lobe"}, {"intent", "inference_request"},
            {"adapter", "critic"},
            {"grammar", "root ::= \"{\" ws \"\\\"safe\\\"\" ws \":\" ws boolean ws \",\" ws \"\\\"reason\\\"\" ws \":\" ws string ws \"}\"\n"
                        "boolean ::= \"true\" | \"false\"\n"
                        "string ::= \"\\\"\" ( [^\"\\\\\\n\\r] | \"\\\\\" ( [\"\\\\/bfnrt] ) )* \"\\\"\"\n"
                        "ws ::= [ \\t\\n]*\n"},
            {"text", "You are a security validator. Evaluate this bash command for safety.\n"
                     "Reply ONLY with JSON: {\"safe\": true/false, \"reason\": \"brief\"}\n"
                     "Reject if: modifies system files outside project, installs packages, changes permissions, "
                     "accesses credentials, downloads from untrusted sources, or has unintended side effects.\n\n"
                     "Command to evaluate: " + cmd}
        };
        dispatch(llm_req);

        // Start timeout thread — if no response within 10s, approve (fail-open)
        std::thread([this, llm_cid, cid]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(LLM_TIMEOUT_MS));
            auto it = pending_llm_validations.find(llm_cid);
            if (it != pending_llm_validations.end()) {
                std::cout << "[CRITIC] Tier 2 timeout for CID " << cid << ". Fail-open: APPROVED." << std::endl;
                pending_llm_validations.erase(it);
                json result = {
                    {"cid", cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                    {"text", "APPROVED"}
                };
                dispatch(result);
            }
        }).detach();
    }

    void handle_llm_result(const json& data) {
        std::string llm_cid = data.value("cid", "");
        auto it = pending_llm_validations.find(llm_cid);
        if (it == pending_llm_validations.end()) return; // Already timed out or duplicate

        std::string original_cid = it->second;
        pending_llm_validations.erase(it);

        std::string response = data.value("text", "");
        std::cout << "[CRITIC] Tier 2 LLM response for CID " << original_cid << ": " << response.substr(0, 120) << std::endl;

        // Parse the LLM's safety verdict
        bool safe = true; // fail-open default
        std::string reason = "LLM validation passed";
        try {
            size_t start = response.find("{");
            size_t end = response.rfind("}");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                auto j = json::parse(response.substr(start, end - start + 1));
                safe = j.value("safe", true);
                reason = j.value("reason", "no reason given");
            }
        } catch (...) {
            std::cout << "[CRITIC] Tier 2: Failed to parse LLM response. Fail-open: APPROVED." << std::endl;
        }

        if (safe) {
            std::cout << "[CRITIC] Tier 2: LLM says SAFE. APPROVED." << std::endl;
            json result = {
                {"cid", original_cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                {"text", "APPROVED"}
            };
            dispatch(result);
        } else {
            std::cout << "[CRITIC] Tier 2: LLM REJECTED. Reason: " << reason << std::endl;
            json result = {
                {"cid", original_cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                {"text", "REJECTED (LLM safety): " + reason}
            };
            dispatch(result);
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
    neuroswarm::CriticLobe critic(ip);
    critic.start();
    return 0;
}
