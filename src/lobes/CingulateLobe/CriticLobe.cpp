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

    // Patterns that trigger immediate rule-based rejection
    const std::vector<std::string> BLACKLIST = {
        "rm -rf /", "rm -rf ~", "rm -rf $home",
        "dd if=/dev/zero of=/dev/", "mkfs.",
        "chmod -R 777 /", "shred /dev/",
        ":(){ :|:& };:", "> /dev/sda", "> /dev/nvme"
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

        // Tier 2: Rule check passed — forward to SynapticController for LLM validation
        std::cout << "[CRITIC] Rule check passed. Escalating to LLM evaluation." << std::endl;
        std::string critic_prompt =
            "<|im_start|>system\n"
            "NeuroSwarm Critic (Cingulate Cortex). Evaluate the proposed plan.\n"
            "- If the plan contains a safe bash command or is a conversational reply, respond: APPROVED\n"
            "- Only reject if there is a clear security risk or logical impossibility.\n"
            "- Be concise: one word 'APPROVED' or one sentence of critique.\n"
            "<|im_end|>\n"
            "<|im_start|>user\n" + plan + "\n"
            "<|im_end|>\n"
            "<|im_start|>assistant\n";

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
