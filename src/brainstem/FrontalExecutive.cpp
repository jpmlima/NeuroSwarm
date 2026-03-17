#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <ctime>

using json = nlohmann::json;

namespace neuroswarm {

class FrontalExecutive {
public:
    FrontalExecutive(const std::string& pub_addr = "tcp://localhost:5555", 
                     const std::string& sub_addr = "tcp://localhost:5556") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect(pub_addr);
        sub.connect(sub_addr);
        sub.set(zmq::sockopt::subscribe, "");

        // Set receive timeout for DMN (Default Mode Network) activation
        int timeout_ms = 30000; // 30 seconds of silence triggers rumination
        sub.set(zmq::sockopt::rcvtimeo, timeout_ms);

        std::cout << "[EXECUTIVE] Resonance Engine Online. DMN Active (30s threshold)." << std::endl;
    }

    void run_cognitive_cycle() {
        while (true) {
            zmq::message_t msg;
            auto res = sub.recv(msg, zmq::recv_flags::none);

            if (!res) {
                // Timeout reached: Enter Default Mode Network (Rumination)
                if (active_goals.empty()) {
                    ruminate();
                }
                continue;
            }

            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                if (raw.empty() || raw[0] != '{') continue;
                auto j = json::parse(raw);
                
                std::string origin = j.value("origin", "");
                std::string intent = j.value("intent", "");

                if (origin == "broca_lobe" && intent == "user_input") {
                    continue; // Intercepted by Wernicke
                }
                else if (origin == "wernicke_lobe" && intent == "inference_result") {
                    start_new_goal(j);
                }
                else if (origin == "visual_lobe" && intent == "visual_stimulus") {
                    process_visual_stimulus(j);
                }
                else if (origin == "motor_cortex" && intent == "execution_result") {
                    process_observation(j);
                }
                else if (origin == "homeostasis") {
                    if (intent == "high_stress_alert") {
                        system_stress = 1.0f;
                        std::cout << "[EXECUTIVE] ADRENALINE SPIKE: High stress detected (" << j.value("reason", "unknown") << ")" << std::endl;
                    } else if (intent == "homeostatic_pulse") {
                        // Slowly decay stress if no new alerts
                        system_stress *= 0.95f;
                    }
                }
                else if (origin == "synaptic_controller" && intent == "inference_result") {
                    decide_next_step(j);
                }
            } catch (...) {}
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    struct GoalState {
        std::string goal;
        std::string history;
        std::vector<std::string> plan;
        int retries = 0;
        bool active = false;
    };
    std::map<std::string, GoalState> active_goals;
    float system_stress = 0.0f;

    void start_new_goal(const json& data) {
        std::string cid = data.value("cid", "global_" + std::to_string(std::time(nullptr)));
        std::string text = data.value("text", "");

        std::cout << "[EXECUTIVE] New High-Level Objective: " << text << " [CID: " << cid << "]" << std::endl;
        
        active_goals[cid] = {text, "", {}, 0, true};
        request_thought(cid, "Initialize task breakdown and first step.");
    }

    void process_visual_stimulus(const json& data) {
        std::string text = data.value("text", "");
        std::cout << "[EXECUTIVE] Visual awareness update." << std::endl;
        
        for (auto& pair : active_goals) {
            pair.second.history += "\nVISUAL STIMULUS:\n" + text;
        }
    }

    void process_observation(const json& data) {
        std::string cid = data.value("cid", "global");
        if (active_goals.find(cid) == active_goals.end()) return;

        std::string result = data.value("proprioception", "");
        bool success = data.value("success", true);

        if (!success) {
            active_goals[cid].retries++;
        } else {
            active_goals[cid].retries = 0;
        }

        active_goals[cid].history += "\nENVIRONMENT FEEDBACK:\n" + result;
        
        if (active_goals[cid].retries > 3) {
            request_thought(cid, "CRITICAL: Previous approach failed 3 times. CHANGE STRATEGY.");
        } else {
            request_thought(cid);
        }
    }

    void decide_next_step(const json& data) {
        std::string cid = data.value("cid", "global");
        if (active_goals.find(cid) == active_goals.end()) return;

        std::string response = data.value("text", "");
        
        try {
            size_t start = response.find("{");
            size_t end = response.rfind("}");
            if (start != std::string::npos && end != std::string::npos) {
                json plan_json = json::parse(response.substr(start, end - start + 1));
                
                std::string thought = plan_json.value("thought", "");
                std::string cmd = plan_json.value("command", "");
                std::string status = plan_json.value("status", "IN_PROGRESS");

                if (plan_json.contains("plan") && plan_json["plan"].is_array()) {
                    active_goals[cid].plan = plan_json["plan"].get<std::vector<std::string>>();
                }

                std::cout << "[EXECUTIVE] Thought: " << thought << std::endl;
                active_goals[cid].history += "\nTHOUGHT: " + thought;

                if (!cmd.empty()) {
                    dispatch_command(cid, cmd);
                }

                if (status == "COMPLETED") {
                    std::cout << "[EXECUTIVE] Goal Accomplished: " << cid << std::endl;
                    json final_resp = {
                        {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "task_complete"},
                        {"text", "Success: " + thought}
                    };
                    dispatch_to_all(final_resp);
                    active_goals.erase(cid);
                }
            } else {
                throw std::runtime_error("No JSON");
            }
        } catch (...) {
            request_thought(cid, "ERROR: Invalid JSON response.");
        }
    }

    void ruminate() {
        std::cout << "[EXECUTIVE] Entering Default Mode Network (Rumination)..." << std::endl;
        std::string dmn_cid = "dmn_" + std::to_string(std::time(nullptr));
        std::string dmn_prompt = 
            "<|im_start|>system\nNEUROSWARM DMN\nRuminate on system state.\nRespond ONLY JSON.\n<|im_end|>\n"
            "<|im_start|>user\nInitiate internal rumination cycle.\n<|im_end|>\n<|im_start|>assistant\n";

        json req = {
            {"cid", dmn_cid}, {"origin", "frontal_executive"}, {"intent", "inference_request"},
            {"adapter", "executive"}, {"text", dmn_prompt}
        };
        dispatch_to_all(req);
    }

    void request_thought(const std::string& cid, const std::string& extra_prompt = "") {
        auto& state = active_goals[cid];
        std::string system_prompt = "<|im_start|>system\nFRONTAL EXECUTIVE\nRespond ONLY JSON.\n<|im_end|>\n";
        
        std::string stress_context = "";
        if (system_stress > 0.5f) {
            stress_context = "\nSYSTEM STRESS ALERT: Execution failure rate is high. Be more cautious and descriptive.\n";
        }

        std::string full_prompt = system_prompt + "<|im_start|>user\nGOAL: " + state.goal + "\nHISTORY: " + state.history + stress_context + "\n" + extra_prompt + "<|im_end|>\n<|im_start|>assistant\n";

        json req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "inference_request"},
            {"adapter", "executive"}, {"text", full_prompt}
        };
        dispatch_to_all(req);
    }

    void dispatch_command(const std::string& cid, const std::string& cmd) {
        json req = {{"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"}, {"command", cmd}};
        dispatch_to_all(req);
    }

    void dispatch_to_all(const json& data) {
        std::string s = data.dump();
        zmq::message_t msg(s.size());
        memcpy(msg.data(), s.c_str(), s.size());
        pub.send(msg, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::FrontalExecutive executive;
    executive.run_cognitive_cycle();
    return 0;
}
