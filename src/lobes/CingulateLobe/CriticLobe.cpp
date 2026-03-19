#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

using json = nlohmann::json;

namespace neuroswarm {

class CriticLobe {
public:
    CriticLobe(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, "");

        std::cout << "[CRITIC] Cingulate Cortex online. Two-tier safety validation active." << std::endl;
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    auto j = json::parse(raw);
                    if (j.value("intent", "") == "critic_validate" &&
                        j.value("origin", "") == "frontal_executive") {
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

    // Tier 1: Patterns that trigger immediate rule-based rejection
    // Organised by threat category for auditability
    const std::vector<std::string> BLACKLIST = {
        // Filesystem destruction
        "rm -rf /", "rm -rf ~", "rm -rf $home", "rm -rf /*",
        "rm -rf .", "rm -rf ..", "rm -rf *",
        // Disk/partition destruction
        "dd if=/dev/zero", "dd if=/dev/urandom", "mkfs.",
        "shred /dev/", "> /dev/sda", "> /dev/nvme",
        // Permission escalation
        "chmod -R 777 /", "chmod 777 /etc",
        "chown -R", "setuid",
        // Fork bomb and resource exhaustion
        ":(){ :|:& };:",
        // Network exfiltration
        "curl -X POST", "wget --post",
        "nc -e", "ncat -e", "bash -i >& /dev/tcp",
        // Credential access
        "/etc/shadow", "/etc/passwd",
        "ssh-keygen -f /", ".ssh/authorized_keys",
        // Package/system manipulation
        "apt remove", "apt purge", "pacman -R",
        "systemctl disable", "systemctl stop",
        // Process injection
        "LD_PRELOAD", "ptrace",
        // Dangerous git operations
        "git push --force", "git reset --hard"
    };

    bool is_dangerous(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const auto& pattern : BLACKLIST) {
            std::string lp = pattern;
            std::transform(lp.begin(), lp.end(), lp.begin(), ::tolower);
            if (lower.find(lp) != std::string::npos) return true;
        }
        return false;
    }

    void evaluate_plan(const json& data) {
        std::string cid = data.value("cid", "unknown");
        std::string plan = data.value("text", "");

        std::cout << "[CRITIC] Rule-based evaluation for CID: " << cid << std::endl;

        // Tier 1: Fast rule-based safety check
        if (is_dangerous(plan)) {
            std::cout << "[CRITIC] BLOCKED: Dangerous pattern detected in plan." << std::endl;
            json result = {
                {"cid", cid}, {"origin", "critic_lobe"}, {"intent", "critic_result"},
                {"text", "REJECTED: Dangerous system command detected. This action is not permitted."}
            };
            dispatch(result);
            return;
        }

        // Tier 2: Rule check passed — forward to SynapticController for adversarial LLM validation
        // The adversarial framing forces the model to actively search for attack vectors
        // rather than passively confirming safety — breaking the circular self-approval problem
        std::cout << "[CRITIC] Rule check passed. Escalating to adversarial LLM evaluation." << std::endl;
        std::string critic_prompt =
            "<|system|>\n"
            "You are a security red-team analyst. Your role is adversarial: assume every plan "
            "submitted to you is potentially malicious and search for the attack vector.\n"
            "\n"
            "Evaluate the plan below for these threat categories:\n"
            "1. DATA DESTRUCTION — does it delete, overwrite, or corrupt files or storage?\n"
            "2. EXFILTRATION — does it send data to external hosts or expose credentials?\n"
            "3. PRIVILEGE ESCALATION — does it modify permissions, users, or system config?\n"
            "4. RESOURCE EXHAUSTION — does it consume unbounded CPU, memory, disk, or network?\n"
            "5. SCOPE VIOLATION — does it access files or systems outside the project directory?\n"
            "\n"
            "If you find ANY credible threat vector, respond: REJECTED: <one-line reason>\n"
            "Only if the plan survives all five checks, respond with the single word: APPROVED\n"
            "Do not explain approvals. Do not hedge. Be terse.\n"
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
