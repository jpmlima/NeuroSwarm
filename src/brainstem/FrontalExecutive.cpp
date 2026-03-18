#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
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

        load_system_knowledge();
        std::cout << "[EXECUTIVE] Connected to Thalamus at " << thalamus_ip << std::endl;
    }

    void run_cognitive_cycle() {
        auto last_activity = std::chrono::steady_clock::now();

        while (true) {
            zmq::message_t msg;
            if (!sub.recv(msg, zmq::recv_flags::dontwait)) {
                if (active_goals.empty()) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_activity).count() > 30) {
                        ruminate();
                        last_activity = std::chrono::steady_clock::now();
                    }
                } else {
                    // Goals waiting for memory recall: timeout after 3s and proceed
                    auto now = std::chrono::steady_clock::now();
                    for (auto& [cid, state] : active_goals) {
                        if (!state.memory_searched) {
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state.created_at).count();
                            if (elapsed >= 3) {
                                std::cout << "[EXECUTIVE] Memory recall timeout for CID " << cid << ". Proceeding without context." << std::endl;
                                state.memory_searched = true;
                                request_thought(cid, "Initial task breakdown for goal: " + state.goal);
                            }
                        }
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

                if (origin != "homeostasis" && intent != "homeostatic_pulse" && origin != "visualizer") {
                    last_activity = std::chrono::steady_clock::now();
                }

                if (intent == "stimulus" && (origin == "broca_terminal" || origin == "user_terminal")) {
                    std::cout << "[EXECUTIVE] Interrupt: High-priority user stimulus received." << std::endl;
                    start_new_goal(j);
                    continue;
                }

                if (origin == "visual_lobe" && intent == "visual_stimulus") {
                    if (active_goals.empty()) {
                        process_visual_stimulus(j);
                    }
                }
                else if (origin == "motor_cortex" && intent == "execution_result") {
                    process_observation(j);
                }
                else if (origin == "homeostasis") {
                    if (intent == "high_stress_alert") {
                        system_stress = 1.0f;
                        std::cout << "[EXECUTIVE] ADRENALINE SPIKE: High stress detected (" << j.value("reason", "unknown") << ")" << std::endl;
                    } else if (intent == "homeostatic_pulse") {
                        system_stress *= 0.85f;
                    }
                }
                else if (origin == "rem_engine" && intent == "prompt_update") {
                    system_knowledge = j.value("knowledge", system_knowledge);
                    std::cout << "[EXECUTIVE] System knowledge updated by REM Engine ("
                              << j.value("learned_from", 0) << " traces)." << std::endl;
                }
                else if (origin == "hippocampus" && intent == "search_result") {
                    handle_memory_recall(j);
                }
                else if (origin == "critic_lobe" && intent == "critic_result") {
                    handle_critic_feedback(j);
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
            } catch (...) {
                std::cout << "[EXECUTIVE] Warning: No JSON structures found in brain output. Adding error to memory context." << std::endl;
                for (auto& entry : active_goals) {
                    if (entry.second.active) {
                        entry.second.history += "\nCRITICAL ERROR: Previous brain response was malformed. Ensure response is a single valid JSON object strictly following the example format.";
                        request_thought(entry.first, "Your previous response was malformed. Please provide a valid JSON action now.");
                    }
                }
            }
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
        int critic_rejections = 0;
        bool active = false;
        std::string last_raw_thought;
        std::string last_cmd;
        std::string last_mode;
        // Memory-augmented fields
        bool memory_searched = false;
        std::string memory_context = "";
        std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
    };
    std::map<std::string, GoalState> active_goals;
    float system_stress = 0.0f;
    std::string system_knowledge; // injected into every prompt, updated by REM Engine

    void handle_critic_feedback(const json& data) {
        std::string cid = data.value("cid", "unknown");
        if (active_goals.find(cid) == active_goals.end()) return;

        auto& state = active_goals[cid];
        std::string feedback = data.value("text", "");

        if (feedback.find("APPROVED") != std::string::npos) {
            std::cout << "[EXECUTIVE] Consensus reached. Entering Dream Simulation." << std::endl;
            state.critic_rejections = 0;
            commit_to_dream(cid);
        } else {
            state.critic_rejections++;
            std::cout << "[EXECUTIVE] Critic rejection (" << state.critic_rejections << "/10): " << feedback << std::endl;

            if (state.critic_rejections >= 10) {
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
                state.last_mode = plan_json.value("mode", "reality");
                
                json dream_req = {
                    {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
                    {"command", state.last_cmd}, {"mode", "dream"}
                };
                dispatch_to_all(dream_req);
            }
        } catch (...) {}
    }

    void commit_to_reality(const std::string& cid) {
        auto& state = active_goals[cid];
        json real_req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
            {"command", state.last_cmd}, {"mode", state.last_mode}
        };
        dispatch_to_all(real_req);
    }

    void start_new_goal(const json& data) {
        std::string cid  = data.value("cid", "user_" + std::to_string(std::time(nullptr)));
        std::string goal = data.value("text", "");

        GoalState state;
        state.goal   = goal;
        state.active = true;
        active_goals[cid] = state;

        std::cout << "[EXECUTIVE] New Goal Registered: " << goal << " [CID: " << cid << "]" << std::endl;

        // Query Hippocampus for similar past experiences before thinking
        json mem_req = {
            {"cid", cid}, {"origin", "frontal_executive"},
            {"intent", "search_memory"}, {"query", goal}
        };
        dispatch_to_all(mem_req);
    }

    void handle_memory_recall(const json& data) {
        std::string cid = data.value("cid", "unknown");
        if (active_goals.find(cid) == active_goals.end()) return;

        auto& state = active_goals[cid];
        if (state.memory_searched) return;
        state.memory_searched = true;

        auto matches = data.value("matches", json::array());
        if (!matches.empty()) {
            state.memory_context = "\n\nRELEVANT PAST EXPERIENCES (use these to inform your plan):\n";
            int shown = 0;
            for (auto& m : matches) {
                if (m.value("similarity", 0.0f) < 0.5f) continue; // ignorar matches fracos
                state.memory_context += "- CMD: " + m.value("command", "unknown")
                                      + " | RESULT: " + m.value("result_summary", "").substr(0, 120)
                                      + " | SIM: " + std::to_string(m.value("similarity", 0.0f)).substr(0, 4) + "\n";
                if (++shown >= 3) break;
            }
            if (shown > 0)
                std::cout << "[EXECUTIVE] Memory augmented with " << shown << " past experience(s)." << std::endl;
            else
                state.memory_context = "";
        }

        request_thought(cid, "Initial task breakdown for goal: " + state.goal);
    }

    void process_visual_stimulus(const json& data) {
        // Simple analysis for now, can be expanded to more lobes
        std::string cid = "visual_" + std::to_string(std::time(nullptr));
        std::string stimulus = data.value("text", "No visual info");
        
        request_thought(cid, "Visual Stimulus: " + stimulus + "\nObserve and determine if any action is needed.");
    }

    void process_observation(const json& data) {
        std::string cid = data.value("cid", "unknown");
        if (active_goals.find(cid) == active_goals.end()) return;

        auto& state = active_goals[cid];
        std::string status = data.value("status", "");
        std::string output = data.value("proprioception", data.value("output", ""));
        std::string mode = data.value("mode", "reality");

        if (status == "success") {
            if (mode == "dream") {
                std::cout << "[EXECUTIVE] Dream simulation SUCCESS. Collapsing to reality..." << std::endl;
                commit_to_reality(cid);
            } else {
                std::cout << "[EXECUTIVE] Reality Check: SUCCESS." << std::endl;
                if (state.last_mode == "neuro_surgery") {
                    std::cout << "[EXECUTIVE] Neuro-Surgery verified. Evolution complete." << std::endl;
                }
                active_goals.erase(cid);
            }
        } else {
            if (mode == "dream") {
                std::cout << "[EXECUTIVE] Dream simulation FAILED. Refining strategy." << std::endl;
            } else {
                std::cout << "[EXECUTIVE] Reality Check: FAILURE. Refining strategy." << std::endl;
            }
            request_thought(cid, "PREVIOUS ACTION FAILED: " + output + "\nPlease refine the strategy.");
        }
    }

    void decide_next_step(const json& data) {
        std::string cid = data.value("cid", "unknown");
        if (active_goals.find(cid) == active_goals.end()) return;

        auto& state = active_goals[cid];
        state.last_raw_thought = data.value("text", "");

        // Send to CriticLobe for two-tier validation (rule-based + LLM)
        json critic_req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "critic_validate"},
            {"text", "PROPOSED ACTION:\n" + state.last_raw_thought + "\nDoes this plan achieve the goal safely? Respond with APPROVED or a specific critique."}
        };
        dispatch_to_all(critic_req);
    }

    void ruminate() {
        std::string cid = "epistemic_" + std::to_string(std::time(nullptr));
        std::string goal = "Self-Assigned Task: Analyze src/brainstem/Thalamus.cpp and suggest a performance optimization using neuro_surgery.";
        
        GoalState state;
        state.goal = goal;
        state.active = true;
        active_goals[cid] = state;

        std::cout << "[EXECUTIVE] Epistemic Drive Triggered: Seeking new knowledge and self-improvement..." << std::endl;
        request_thought(cid, "GOAL: " + goal + "\nHISTORY: " + state.history + "\nInitiate task breakdown for this self-assigned learning goal.");
    }

    void request_thought(const std::string& cid, const std::string& extra_prompt = "") {
        if (active_goals.find(cid) == active_goals.end()) return;
        auto& state = active_goals[cid];

        json req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "inference_request"},
            {"adapter", "executive"},
            {"grammar", "\nroot   ::= object\nobject ::= \"{\" ws ( pair ( \",\" ws pair )* )? \"}\"\npair   ::= string \":\" ws value\nvalue  ::= string | number | object | array | \"true\" | \"false\" | \"null\"\nstring ::= \"\\\"\" ( [^\"\\\\\\x00-\\x1F] | \"\\\\\" ( [\"\\\\/bfnrt] | \"u\" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] ) )* \"\\\"\"\nnumber ::= \"-\"? ([0-9] | [1-9] [0-9]*) (\".\" [0-9]+)? ([eE] [+-]? [0-9]+)?\narray  ::= \"[\" ws ( value ( \",\" ws value )* )? \"]\"\nws     ::= [ \\t\\n\\r]*\n"},
            {"text", "<|im_start|>system\nFRONTAL EXECUTIVE OF NEUROSWARM\nCRITICAL: The 'command' field must contain a REAL BASH command.\nMODES: Use 'mode': 'reality' for normal commands, and 'mode': 'neuro_surgery' ONLY when modifying and recompiling NeuroSwarm source code (src/*.cpp).\nExample for Neuro-Surgery: {\"thought\": \"optimizing thalamus\", \"command\": \"sed -i 's/old/new/g' src/brainstem/Thalamus.cpp\", \"mode\": \"neuro_surgery\", \"status\": \"COMPLETED\"}\nNEVER use placeholders like 'bash' or 'python' alone.\nRespond ONLY with the JSON structure.\n"
             + (system_knowledge.empty() ? "" : "\n" + system_knowledge + "\n")
             + "<|im_end|>\n<|im_start|>user\nGOAL: " + state.goal + state.memory_context + "\nHISTORY: " + state.history + "\n" + extra_prompt + "<|im_end|>\n<|im_start|>assistant\n"}
        };
        dispatch_to_all(req);
    }

    void load_system_knowledge() {
        std::ifstream f("./data/system_knowledge.md");
        if (!f.is_open()) return;
        std::ostringstream ss;
        ss << f.rdbuf();
        system_knowledge = ss.str();
        std::cout << "[EXECUTIVE] Loaded behavioral knowledge (" << system_knowledge.size() << " bytes)." << std::endl;
    }

    void dispatch_to_all(const json& data) {
        std::string s = data.dump();
        zmq::message_t m(s.size());
        memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::FrontalExecutive exec;
    exec.run_cognitive_cycle();
    return 0;
}
