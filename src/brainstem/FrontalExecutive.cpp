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
#include <cstdio>
#include <climits>
#include <regex>

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
                        pick_next_task();
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
                    } else if (adapter == "task_generator") {
                        handle_generated_task(j);
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
        // Ralph loop fields
        std::string task_id = ""; // non-empty if this goal came from tasks.json
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

    // Extract "command" and "mode" from a potentially truncated JSON response
    std::pair<std::string, std::string> extract_command(const std::string& raw) {
        // Try full JSON parse first
        try {
            size_t start = raw.find("{");
            size_t end   = raw.rfind("}");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                auto j = json::parse(raw.substr(start, end - start + 1));
                return {j.value("command", ""), j.value("mode", "reality")};
            }
        } catch (...) {}

        // Fallback: regex-based extraction for truncated JSON
        std::string cmd, mode = "reality";
        std::smatch m;

        // Match "command": "..." — handles escaped quotes inside
        std::regex cmd_re("\"command\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        if (std::regex_search(raw, m, cmd_re)) cmd = m[1].str();

        std::regex mode_re("\"mode\"\\s*:\\s*\"([^\"]*)\"");
        if (std::regex_search(raw, m, mode_re)) mode = m[1].str();

        return {cmd, mode};
    }

    void commit_to_dream(const std::string& cid) {
        auto& state = active_goals[cid];

        auto [cmd, mode] = extract_command(state.last_raw_thought);
        std::cout << "[EXECUTIVE] Dream command extracted: [" << cmd << "] mode=[" << mode << "]" << std::endl;

        if (cmd.empty()) {
            std::cout << "[EXECUTIVE] Dream abort: no command found (thought size=" << state.last_raw_thought.size() << ")." << std::endl;
            request_thought(cid, "Respond ONLY with a compact JSON: {\"thought\":\"brief\",\"command\":\"bash_cmd\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}. No long explanations.");
            return;
        }

        state.last_cmd  = cmd;
        state.last_mode = mode;

        json dream_req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
            {"command", cmd}, {"mode", "dream"}
        };
        dispatch_to_all(dream_req);
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
                if (m.value("similarity", 0.0f) < 0.5f) continue; // Discard low-confidence matches below cosine similarity threshold
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
        // Lightweight visual analysis path; additional lobe integration can be layered here
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
                if (!state.task_id.empty()) {
                    mark_task_complete(cid);
                }
                active_goals.erase(cid);
            }
        } else {
            if (mode == "dream") {
                std::cout << "[EXECUTIVE] Dream simulation FAILED. Refining strategy." << std::endl;
            } else {
                std::cout << "[EXECUTIVE] Reality Check: FAILURE. Refining strategy." << std::endl;
            }
            state.history += "\n[ATTEMPT FAILED] cmd='" + state.last_cmd + "' error='" + output.substr(0, 300) + "'";
            state.retries++;
            if (state.retries >= 8) {
                std::cout << "[EXECUTIVE] Too many retries for CID " << cid << ". Abandoning goal." << std::endl;
                active_goals.erase(cid);
                return;
            }
            request_thought(cid, "PREVIOUS ACTION FAILED (attempt " + std::to_string(state.retries) + "/8): " + output.substr(0, 300) + "\nTry a different approach.");
        }
    }

    void decide_next_step(const json& data) {
        std::string cid = data.value("cid", "unknown");
        if (active_goals.find(cid) == active_goals.end()) return;

        auto& state = active_goals[cid];
        state.last_raw_thought = data.value("text", "");
        std::cout << "[EXECUTIVE] Thought received for CID " << cid << " size=" << state.last_raw_thought.size() << " first10=[" << state.last_raw_thought.substr(0, 10) << "]" << std::endl;
        state.history += "\n[THOUGHT] " + state.last_raw_thought.substr(0, 200);

        // Send to CriticLobe for two-tier validation (rule-based + LLM)
        json critic_req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "critic_validate"},
            {"text", "PROPOSED ACTION:\n" + state.last_raw_thought + "\nDoes this plan achieve the goal safely? Respond with APPROVED or a specific critique."}
        };
        dispatch_to_all(critic_req);
    }

    // Ralph loop — pick the highest-priority pending task from tasks.json
    void pick_next_task() {
        std::ifstream f("./tasks.json");
        if (!f.is_open()) {
            // Fall back to epistemic rumination if no task file exists
            std::string cid = "epistemic_" + std::to_string(std::time(nullptr));
            std::string goal = "Self-Assigned Task: Analyze src/brainstem/Thalamus.cpp and suggest a performance optimization using neuro_surgery.";
            GoalState state;
            state.goal = goal;
            state.active = true;
            active_goals[cid] = state;
            std::cout << "[EXECUTIVE] Epistemic Drive Triggered (no tasks.json found)." << std::endl;
            request_thought(cid, "GOAL: " + goal + "\nInitiate task breakdown for this self-assigned learning goal.");
            return;
        }

        json tasks_doc;
        try {
            f >> tasks_doc;
        } catch (...) {
            std::cout << "[EXECUTIVE] tasks.json is malformed. Skipping Ralph loop." << std::endl;
            return;
        }

        auto& tasks = tasks_doc["tasks"];
        int best_idx = -1;
        int best_priority = INT_MAX;

        for (int i = 0; i < (int)tasks.size(); i++) {
            bool passes = tasks[i].value("passes", false);
            if (passes) continue;
            int pri = tasks[i].value("priority", 99);
            if (pri < best_priority) {
                best_priority = pri;
                best_idx = i;
            }
        }

        if (best_idx == -1) {
            std::cout << "[EXECUTIVE] All tasks complete. Generating next autonomous task..." << std::endl;
            generate_next_task();
            return;
        }

        auto& task = tasks[best_idx];
        std::string task_id   = task.value("id", "UNKNOWN");
        std::string title     = task.value("title", "");
        std::string desc      = task.value("description", "");
        task["attempts"]      = task.value("attempts", 0) + 1;

        // Persist incremented attempt count
        std::ofstream out("./tasks.json");
        out << tasks_doc.dump(2);
        out.close();

        std::string cid = "ralph_" + task_id + "_" + std::to_string(std::time(nullptr));
        GoalState state;
        state.goal    = desc;
        state.active  = true;
        state.task_id = task_id;
        active_goals[cid] = state;

        std::cout << "[EXECUTIVE] Ralph Loop — starting task [" << task_id << "]: " << title << std::endl;

        // Query Hippocampus first (same flow as user goals)
        json mem_req = {
            {"cid", cid}, {"origin", "frontal_executive"},
            {"intent", "search_memory"}, {"query", desc}
        };
        dispatch_to_all(mem_req);
    }

    // Ralph loop — called when a task's execution succeeds and the goal is complete
    void mark_task_complete(const std::string& cid) {
        auto it = active_goals.find(cid);
        if (it == active_goals.end()) return;
        const std::string& task_id = it->second.task_id;
        if (task_id.empty()) return;

        // Update tasks.json: passes=true, completed_at, learned
        std::ifstream fin("./tasks.json");
        if (!fin.is_open()) return;
        json tasks_doc;
        try { fin >> tasks_doc; } catch (...) { return; }
        fin.close();

        std::time_t now = std::time(nullptr);
        char ts[64];
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

        for (auto& t : tasks_doc["tasks"]) {
            if (t.value("id", "") == task_id) {
                t["passes"]       = true;
                t["completed_at"] = std::string(ts);
                t["learned"]      = it->second.last_cmd;
                break;
            }
        }

        std::ofstream fout("./tasks.json");
        fout << tasks_doc.dump(2);
        fout.close();

        // Append to progress.txt
        std::ofstream prog("./progress.txt", std::ios::app);
        prog << "[" << ts << "] COMPLETED " << task_id << " | cmd: " << it->second.last_cmd << "\n";
        prog.close();

        // Git commit
        std::string commit_msg = "ralph: completed " + task_id;
        for (auto& t : tasks_doc["tasks"]) {
            if (t.value("id", "") == task_id) {
                commit_msg += " - " + t.value("title", "");
                break;
            }
        }
        std::string cmd = "git -C . add tasks.json progress.txt && git -C . commit -m \"" + commit_msg + "\" 2>&1";
        signal(SIGCHLD, SIG_DFL); // Reset SIGCHLD to SIG_DFL — inherited SIG_IGN from CerebralMatrix causes pclose() to return -1
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buf[256];
            std::string git_out;
            while (fgets(buf, sizeof(buf), pipe)) git_out += buf;
            pclose(pipe);
            std::cout << "[EXECUTIVE] Ralph commit: " << git_out;
        }

        std::cout << "[EXECUTIVE] Ralph Loop — task " << task_id << " PASSED and committed." << std::endl;

        // Trigger autonomous generation of the next task
        generate_next_task();
    }

    // Autonomous task generation — infers the next useful task from system state
    void generate_next_task() {
        std::ifstream f("./tasks.json");
        if (!f.is_open()) return;
        json tasks_doc;
        try { f >> tasks_doc; } catch (...) { return; }
        f.close();

        // Build a summary of completed tasks and their learned commands
        std::string completed_summary;
        int completed_count = 0;
        int next_priority = 1;
        for (auto& t : tasks_doc["tasks"]) {
            int pri = t.value("priority", 0);
            if (pri >= next_priority) next_priority = pri + 1;
            if (t.value("passes", false)) {
                completed_summary += "- " + t.value("id", "?") + ": " + t.value("title", "?")
                    + " (cmd: " + t.value("learned", "?").substr(0, 80) + ")\n";
                completed_count++;
            }
        }

        // Compute next task ID
        std::string next_id = "NS-" + std::string(3 - std::to_string(completed_count + 1).size(), '0')
            + std::to_string(completed_count + 1);

        std::cout << "[EXECUTIVE] Autonomous task generation — " << completed_count
                  << " tasks completed, inferring next useful task..." << std::endl;

        std::string prompt =
            "<|system|>\n"
            "You are the task planner for NeuroSwarm, a distributed cognitive architecture.\n"
            "Your job: given what the system has already accomplished, generate the NEXT most useful task.\n"
            "\n"
            "RULES:\n"
            "- The description MUST contain a concrete bash command or a precise step-by-step instruction.\n"
            "- Do NOT repeat tasks already completed.\n"
            "- Prefer tasks that expand system capabilities: monitoring, self-analysis, performance, resilience.\n"
            "- The task must be achievable by a single bash command or short script.\n"
            "- Keep the title under 40 characters.\n"
            "- Keep the description under 200 characters.\n"
            "\n"
            "Respond ONLY with a JSON object: {\"title\": \"...\", \"description\": \"...\"}\n"
            "<|end|>\n"
            "<|user|>\n"
            "COMPLETED TASKS:\n" + completed_summary + "\n"
            "What is the next most useful task for the system to attempt?\n"
            "<|end|>\n"
            "<|assistant|>\n";

        json req = {
            {"cid", "taskgen_" + std::to_string(std::time(nullptr))},
            {"origin", "frontal_executive"},
            {"intent", "inference_request"},
            {"adapter", "task_generator"},
            {"grammar", "root   ::= \"{\" ws \"\\\"title\\\"\" ws \":\" ws string \",\" ws \"\\\"description\\\"\" ws \":\" ws string \"}\" ws\nstring ::= \"\\\"\" ( [^\"\\\\\\n\\r] | \"\\\\\" ( [\"\\\\/bfnrt] | \"u\" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] ) )* \"\\\"\"\nws     ::= [ \\t\\n\\r]*\n"},
            {"text", prompt},
            {"_next_priority", next_priority},
            {"_next_id", next_id}
        };
        dispatch_to_all(req);
    }

    // Handle the LLM response from task generation and append to tasks.json
    void handle_generated_task(const json& data) {
        std::string raw = data.value("text", "");
        std::cout << "[EXECUTIVE] Task generator response: " << raw.substr(0, 200) << std::endl;

        // Parse the generated task
        std::string title, description;
        try {
            size_t start = raw.find("{");
            size_t end = raw.rfind("}");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                auto gen = json::parse(raw.substr(start, end - start + 1));
                title = gen.value("title", "");
                description = gen.value("description", "");
            }
        } catch (...) {}

        // Regex fallback for truncated output
        if (title.empty()) {
            std::smatch m;
            std::regex title_re("\"title\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
            if (std::regex_search(raw, m, title_re)) title = m[1].str();
        }
        if (description.empty()) {
            std::smatch m;
            std::regex desc_re("\"description\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
            if (std::regex_search(raw, m, desc_re)) description = m[1].str();
        }

        if (title.empty() || description.empty()) {
            std::cout << "[EXECUTIVE] Task generation failed — could not parse title/description." << std::endl;
            return;
        }

        // Read metadata passed through the request
        std::string next_id = data.value("_next_id", "NS-???");
        int next_priority = data.value("_next_priority", 99);

        // Append to tasks.json
        std::ifstream fin("./tasks.json");
        if (!fin.is_open()) return;
        json tasks_doc;
        try { fin >> tasks_doc; } catch (...) { return; }
        fin.close();

        // Check for duplicate titles
        for (auto& t : tasks_doc["tasks"]) {
            if (t.value("title", "") == title) {
                std::cout << "[EXECUTIVE] Task generation skipped — duplicate title: " << title << std::endl;
                return;
            }
        }

        json new_task = {
            {"id", next_id},
            {"title", title},
            {"description", description},
            {"priority", next_priority},
            {"passes", false},
            {"attempts", 0},
            {"generated_by", "autonomous"}
        };

        tasks_doc["tasks"].push_back(new_task);

        std::ofstream fout("./tasks.json");
        fout << tasks_doc.dump(2);
        fout.close();

        std::cout << "[EXECUTIVE] Autonomous task generated: [" << next_id << "] " << title
                  << " — " << description.substr(0, 100) << std::endl;
    }

    void request_thought(const std::string& cid, const std::string& extra_prompt = "") {
        if (active_goals.find(cid) == active_goals.end()) return;
        auto& state = active_goals[cid];

        json req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "inference_request"},
            {"adapter", "executive"},
            {"grammar", "root   ::= object\nobject ::= \"{\" ws ( pair ( \",\" ws pair )* )? \"}\"\npair   ::= string \":\" ws value\nvalue  ::= string | number | object | array | \"true\" | \"false\" | \"null\"\nstring ::= \"\\\"\" ( [^\"\\\\\\n\\r] | \"\\\\\" ( [\"\\\\/bfnrt] | \"u\" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] ) )* \"\\\"\"\nnumber ::= \"-\"? ( [0-9] | [1-9] [0-9]* ) ( \".\" [0-9]+ )? ( [eE] [-+]? [0-9]+ )?\narray  ::= \"[\" ws ( value ( \",\" ws value )* )? \"]\"\nws     ::= [ \\t\\n\\r]*\n"},
            {"text", "<|system|>\nFRONTAL EXECUTIVE OF NEUROSWARM\nCRITICAL: The 'command' field must contain a REAL BASH command.\nMODES: Use 'mode': 'reality' for normal commands, and 'mode': 'neuro_surgery' ONLY when modifying and recompiling NeuroSwarm source code (src/*.cpp).\nExample for Neuro-Surgery: {\"thought\": \"optimizing thalamus\", \"command\": \"sed -i 's/old/new/g' src/brainstem/Thalamus.cpp\", \"mode\": \"neuro_surgery\", \"status\": \"COMPLETED\"}\nNEVER use placeholders like 'bash' or 'python' alone.\nRespond ONLY with the JSON structure.\n"
             + (system_knowledge.empty() ? "" : "\n" + system_knowledge + "\n")
             + "<|end|>\n<|user|>\nGOAL: " + state.goal + state.memory_context + "\nHISTORY: " + state.history + "\n" + extra_prompt + "<|end|>\n<|assistant|>\n"}
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
