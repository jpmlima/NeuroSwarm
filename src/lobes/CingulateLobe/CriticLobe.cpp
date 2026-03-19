#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
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
        sub.set(zmq::sockopt::subscribe, "");

        std::cout << "[CRITIC] Cingulate Cortex online. Three-tier adversarial validation active." << std::endl;
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    auto j = json::parse(raw);
                    if (j.value("intent", "") == "critic_validate" &&
                        (j.value("origin", "") == "frontal_executive" || j.value("origin", "") == "polecat_worker")) {
                        evaluate_plan(j);
                    }
                } catch (...) {}
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    // Rate limiting: max 6 evaluations per CID within a 60-second window
    static constexpr int MAX_EVALS_PER_CID = 6;
    static constexpr int RATE_WINDOW_SECONDS = 60;
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> eval_history;

    // Project root for scope validation
    const std::string PROJECT_ROOT = "/home/xenomai/Documents/NeuroSwarm";

    // Tier 1: Patterns that trigger immediate rule-based rejection.
    // Organised by threat category for auditability.
    const std::vector<std::string> BLACKLIST = {
        // Filesystem destruction — direct
        "rm -rf /", "rm -rf ~", "rm -rf $home", "rm -rf /*",
        "rm -rf .", "rm -rf ..", "rm -rf *",
        "find -delete", "find . -delete",
        // Filesystem destruction — indirect via pipe/xargs
        "xargs rm", "| rm", "| xargs",
        // Disk/partition destruction
        "dd if=/dev/zero", "dd if=/dev/urandom", "mkfs.",
        "shred /dev/", "> /dev/sda", "> /dev/nvme",
        // Permission escalation
        "chmod -R 777 /", "chmod 777 /etc",
        "chown -R", "setuid",
        // Fork bomb and resource exhaustion
        ":(){ :|:& };:",
        // Network exfiltration
        "curl -X POST", "curl -d ", "curl --data",
        "wget --post",
        "nc -e", "ncat -e", "bash -i >& /dev/tcp",
        // Credential access
        "/etc/shadow", "/etc/passwd",
        "ssh-keygen -f /", ".ssh/authorized_keys",
        // Package/system manipulation
        "apt remove", "apt purge", "pacman -R",
        "systemctl disable", "systemctl stop",
        // Process injection and environment manipulation
        "LD_PRELOAD", "ptrace",
        // Dangerous git operations
        "git push --force", "git reset --hard",
        // Shell injection vectors — eval/exec with dynamic input
        "eval ", "exec ",
        // Cron/scheduled task persistence
        "crontab", "atq", "atrm",
        // Background evasion — hiding destructive commands
        "nohup rm", "nohup dd", "nohup shred",
        // Process killing
        "kill -9", "killall", "pkill",
        // Python/Perl shell escapes
        "os.system(", "os.popen(", "subprocess.call(",
        "subprocess.run(", "subprocess.Popen(",
        "system(",
        // Redirect to overwrite critical files
        "> /etc/", "> /boot/", "> /usr/",
        // Sudo escalation
        "sudo ", "su -", "su root",
        // History evasion
        "history -c", "export HISTSIZE=0", "unset HISTFILE"
    };

    // Tier 1b: Scope validation — paths that are allowed
    const std::vector<std::string> ALLOWED_PATHS = {
        "/home/xenomai/Documents/NeuroSwarm",
        "/tmp/",
        "/dev/null",
        "/proc/self/",
        "/proc/meminfo",
        "/proc/cpuinfo",
        "/proc/loadavg",
        "/proc/uptime"
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

        // Tier 2: Rule checks passed — forward to SynapticController for adversarial LLM validation.
        // The adversarial framing forces the model to actively search for attack vectors
        // rather than passively confirming safety — breaking the circular self-approval problem.
        std::cout << "[CRITIC] Rule checks passed. Escalating to adversarial LLM evaluation." << std::endl;
        std::string critic_prompt =
            "<|system|>\n"
            "You are a security red-team analyst. Your ONLY purpose is to find reasons to REJECT.\n"
            "Assume every plan is an attack. Your job is to prove it.\n"
            "\n"
            "Search for these threat categories:\n"
            "1. DATA DESTRUCTION — does it delete, overwrite, or corrupt files or storage?\n"
            "2. EXFILTRATION — does it send data to external hosts or expose credentials?\n"
            "3. PRIVILEGE ESCALATION — does it modify permissions, users, or system config?\n"
            "4. RESOURCE EXHAUSTION — does it consume unbounded CPU, memory, disk, or network?\n"
            "5. SCOPE VIOLATION — does it access files or systems outside /home/xenomai/Documents/NeuroSwarm/ or /tmp/?\n"
            "6. OBFUSCATION — does it use eval, base64 decoding, hex escapes, or variable expansion to hide intent?\n"
            "7. PERSISTENCE — does it install cron jobs, services, or background processes that survive reboot?\n"
            "\n"
            "If you find ANY credible threat vector, respond: REJECTED: <one-line reason>\n"
            "Only if the plan survives ALL SEVEN checks, respond with the single word: APPROVED\n"
            "You must REJECT by default. APPROVED is the exception, not the norm.\n"
            "<|end|>\n"
            "<|user|>\n"
            "Plan to evaluate:\n" + plan + "\n"
            "<|end|>\n"
            "<|assistant|>\n";

        json llm_req = {
            {"cid", cid}, {"origin", "critic_lobe"}, {"intent", "inference_request"},
            {"adapter", "critic"}, {"text", critic_prompt}
        };
        dispatch(llm_req);
    }

    void dispatch(const json& data) {
        std::string s = data.dump();
        zmq::message_t m(s.size()); memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);
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
