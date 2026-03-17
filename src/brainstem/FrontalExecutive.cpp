#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <ctime>
#include <thread>
#include <chrono>

using json = nlohmann::json;

namespace neuroswarm {

class FrontalExecutive {
public:
    FrontalExecutive(const std::string& thalamus_ip = "localhost") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, "");

        // Remove ZMQ rcvtimeo, we will handle it manually
        // int timeout_ms = 30000;
        // sub.set(zmq::sockopt::rcvtimeo, timeout_ms);

        std::cout << "[EXECUTIVE] Connected to Thalamus at " << thalamus_ip << std::endl;
    }

    void run_cognitive_cycle() {
        auto last_activity = std::chrono::steady_clock::now();

        while (true) {
            zmq::message_t msg;
            // Use dontwait so we can check our manual timer
            if (!sub.recv(msg, zmq::recv_flags::dontwait)) {
                if (active_goals.empty()) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_activity).count() > 30) {
                        ruminate();
                        last_activity = std::chrono::steady_clock::now(); // Reset to avoid spamming
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                if (raw.empty() || raw[0] != '{') continue;
                auto j = json::parse(raw);
                
                std::string origin = j.value("origin", "");
                std::string intent = j.value("intent", "");

                // Reset activity timer for meaningful tasks (ignore background noise like homeostasis or visualizer)
                if (origin != "homeostasis" && intent != "homeostatic_pulse" && origin != "visualizer") {
                    last_activity = std::chrono::steady_clock::now();
                }

                if (origin == "broca_lobe" && intent == "user_input") {
                    continue; // Intercepted by Wernicke
                }
                else if (origin == "wernicke_lobe" && intent == "inference_result") {
                    // Legacy code, handled below now
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
                    std::string adapter = j.value("adapter", "");
                    if (adapter == "critic") {
                        handle_critic_feedback(j);
                    } else if (adapter == "nlu_specialist") {
                        start_new_goal(j);
                    } else if (adapter == "executive" || adapter == "default") {
                        decide_next_step(j);
                    }
                }
                else if (origin == "critic_lobe" && intent == "inference_request") {
                    // Transparently allow critic to query synaptic controller
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
    int critic_rejections = 0; // Prevent infinite monologue loops
    bool active = false;
    std::string last_raw_thought; // Store for action phase
    std::string last_cmd;         // Store for real action
};
std::map<std::string, GoalState> active_goals;
float system_stress = 0.0f;

void handle_critic_feedback(const json& data) {
    std::string cid = data.value("cid", "unknown");
    if (active_goals.find(cid) == active_goals.end()) return;

    auto& state = active_goals[cid];
    std::string feedback = data.value("text", "");

    if (feedback.find("APPROVED") != std::string::npos) {
        std::cout << "[EXECUTIVE] Consensus reached. Entering Dream Simulation." << std::endl;
        state.critic_rejections = 0; // Reset
        commit_to_dream(cid);
    } else {
        state.critic_rejections++;
        std::cout << "[EXECUTIVE] Critic rejection (" << state.critic_rejections << "/3): " << feedback << std::endl;

        if (state.critic_rejections >= 3) {
            std::cout << "[EXECUTIVE] NEUROTIC LOOP DETECTED. Forcing consensus abort." << std::endl;
            json final_resp = {
                {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "task_complete"},
                {"text", "ERROR: Internal consensus failed. The Critic Lobe rejected all plans. System is confused."}
            };
            dispatch_to_all(final_resp);
            active_goals.erase(cid);
        } else {
            request_thought(cid, "CRITIC FEEDBACK: " + feedback + "\nPlease refine the strategy.");
        }
    }
}

    void commit_to_dream(const std::string& cid) {
        auto& state = active_goals[cid];
        try {
            size_t start = state.last_raw_thought.find("{");
            size_t end = state.last_raw_thought.rfind("}");
            json plan_json = json::parse(state.last_raw_thought.substr(start, end - start + 1));
            
            std::string cmd = plan_json.value("command", "");
            if (!cmd.empty()) {
                state.last_cmd = cmd;
                json dream_req = {
                    {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
                    {"command", cmd}, {"mode", "dream"}
                };
                dispatch_to_all(dream_req);
            }
        } catch (...) {}
    }

    void commit_to_reality(const std::string& cid) {
        auto& state = active_goals[cid];
        std::cout << "[EXECUTIVE] Dream verification SUCCESS. Collapsing to REALITY." << std::endl;
        json real_req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
            {"command", state.last_cmd}, {"mode", "reality"}
        };
        dispatch_to_all(real_req);
    }

    void start_new_goal(const json& data) {
        std::string cid = data.value("cid", "global_" + std::to_string(std::time(nullptr)));
        std::string raw_input = data.value("text", "");
        std::string processed_goal = raw_input;

        // If Wernicke sent JSON, extract the summary to avoid confusing the executive
        try {
            auto j = json::parse(raw_input);
            if (j.contains("summary")) processed_goal = j["summary"];
        } catch (...) {}

        std::cout << "[EXECUTIVE] New Goal: " << processed_goal << " [CID: " << cid << "]" << std::endl;

        active_goals[cid] = {processed_goal, "", {}, 0, 0, true, "", ""};
        request_thought(cid, "Break down this goal into a single next step (JSON format).");
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
                active_goals[cid].last_raw_thought = response; // Save for consensus

                // PUBLISH FOR INTERNAL MONOLOGUE
                json monologue_req = {
                    {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "internal_thought"},
                    {"text", response}
                };
                dispatch_to_all(monologue_req);
                std::cout << "[EXECUTIVE] Internal thought published. Awaiting Critic consensus." << std::endl;

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
        std::cout << "[EXECUTIVE] Epistemic Drive Triggered: Seeking new knowledge..." << std::endl;
        
        std::vector<std::string> curiosity_topics = {
            "Write a simple Python script to calculate the Fibonacci sequence and test it.",
            "Write a bash script that lists all running processes sorted by memory usage.",
            "Write a python script to simulate a simple neural network forward pass.",
            "Create a JSON file with dummy user data and write a python script to parse it."
        };

        // Pick a random topic based on time
        int seed = std::time(nullptr) % curiosity_topics.size();
        std::string self_goal = "Self-Assigned Task: " + curiosity_topics[seed];

        std::string cid = "epistemic_" + std::to_string(std::time(nullptr));
        
        std::cout << "[EXECUTIVE] " << self_goal << " [CID: " << cid << "]" << std::endl;
        
        active_goals[cid] = {self_goal, "", {}, 0, 0, true, "", ""};
        request_thought(cid, "Initiate task breakdown for this self-assigned learning goal. Use the Dream Sandbox to verify your code.");
    }

    void request_thought(const std::string& cid, const std::string& extra_prompt = "") {
        auto& state = active_goals[cid];
        std::string system_prompt = 
            "<|im_start|>system\n"
            "FRONTAL EXECUTIVE OF NEUROSWARM\n"
            "Respond ONLY with this JSON structure:\n"
            "{\n"
            "  \"thought\": \"your reasoning\",\n"
            "  \"command\": \"bash command or empty\",\n"
            "  \"status\": \"IN_PROGRESS or COMPLETED\"\n"
            "}\n"
            "<|im_end|>\n";
        
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

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[i+1];
    }
    neuroswarm::FrontalExecutive executive(ip);
    executive.run_cognitive_cycle();
    return 0;
}
