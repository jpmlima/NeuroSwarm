#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
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
#include <set>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

using json = nlohmann::json;

namespace neuroswarm {

class FrontalExecutive {
public:
    FrontalExecutive(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {
            "stimulus", "visual_stimulus", "execution_result",
            "high_stress_alert", "homeostatic_pulse", "prompt_update",
            "time_pulse", "search_result", "critic_result",
            "intrinsic_goal", "spike_ready", "spike_done",
            "inference_result", "metabolic_alert", "goal_plan",
            "primordial_ready", "dopamine_signal",
            "concept_transfer", "concept_response"
        });

        load_system_knowledge();
        std::cout << "[EXECUTIVE] Connected to Thalamus at " << thalamus_ip << std::endl;
    }

    void run_cognitive_cycle() {
        auto last_activity = std::chrono::steady_clock::now();

        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) {
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
                // Reap any zombie spike workers (child may exit after sending spike_done)
                reap_zombies();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            try {
                std::string origin = j.value("origin", "");
                std::string intent = j.value("intent", "");

                if (origin != "homeostasis" && intent != "homeostatic_pulse" && origin != "visualizer" && origin != "chronos" && origin != "statistics") {
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
                        // Phase 5: Read stamina from metabolic system
                        current_stamina = j.value("stamina", current_stamina);
                    } else if (intent == "metabolic_alert") {
                        current_stamina = j.value("stamina", 0.0f);
                        std::cout << "[EXECUTIVE] METABOLIC ALERT: stamina=" << (int)current_stamina << "%" << std::endl;
                        // Trigger REM sleep to recover
                        if (current_stamina < 20.0f) {
                            json sleep = {{"origin", "frontal_executive"}, {"intent", "initiate_sleep_cycle"},
                                          {"reason", "metabolic_exhaustion"}};
                            dispatch_to_all(sleep);
                        }
                    }
                }
                else if (origin == "rem_engine" && intent == "prompt_update") {
                    system_knowledge = j.value("knowledge", system_knowledge);
                    std::cout << "[EXECUTIVE] System knowledge updated by REM Engine ("
                              << j.value("learned_from", 0) << " traces)." << std::endl;
                }
                else if (origin == "chronos" && intent == "time_pulse") {
                    current_timestamp = j.value("timestamp", "");
                    current_uptime = j.value("uptime_human", "");
                    current_time_of_day = j.value("time_of_day", "");
                    is_night = j.value("is_night", false);
                }
                else if (origin == "hippocampus" && intent == "search_result") {
                    handle_memory_recall(j);
                }
                else if (origin == "critic_lobe" && intent == "critic_result") {
                    handle_critic_feedback(j);
                }
                else if (origin == "basal_ganglia" && intent == "intrinsic_goal") {
                    handle_intrinsic_goal(j);
                }
                else if (origin == "primordial_loop" && intent == "goal_plan") {
                    handle_goal_plan(j);
                }
                else if (origin == "primordial_loop" && intent == "primordial_ready") {
                    planner_ready_ = true;
                    std::cout << "[EXECUTIVE] PrimordialLoop ready: "
                              << j.value("operators", 0) << " operators, "
                              << j.value("world_facts", 0) << " facts." << std::endl;
                }
                else if (origin == "spike_worker" && intent == "spike_ready") {
                    handle_spike_ready(j);
                }
                else if (origin == "spike_worker" && intent == "spike_done") {
                    handle_spike_done(j);
                }
                else if (origin == "basal_ganglia" && intent == "dopamine_signal") {
                    handle_dopamine(j);
                }
                else if (origin == "basal_ganglia" && intent == "concept_transfer") {
                    handle_concept_transfer(j);
                }
                else if (origin == "concept_lobe" && intent == "concept_response") {
                    handle_concept_response(j);
                }
                else if (origin == "synaptic_controller" && intent == "inference_result") {
                    std::string adapter = j.value("adapter", "");
                    if (adapter == "critic") {
                        handle_critic_feedback(j);
                    } else if (adapter == "nlu_specialist") {
                        start_new_goal(j);
                    } else if (adapter == "executive" || adapter == "coder" || adapter == "default") {
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
        int total_thought_cycles = 0; // Total inference cycles — detects oscillating loops (matches SpikeWorker)
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
        // Intrinsic motivation fields
        std::string domain = "";       // capability domain (set by BasalGanglia)
        float fitness_score = 0.0f;    // fitness at time of selection
        bool is_intrinsic = false;     // true if goal came from BasalGanglia
        std::vector<std::string> suggested_commands; // from BasalGanglia
        std::vector<std::string> executed_commands;  // RLAIF: full chain for reinforcement
    };
    std::map<std::string, GoalState> active_goals;
    float system_stress = 0.0f;
    float current_stamina = 100.0f;  // Phase 5: metabolic energy level
    bool waiting_for_intrinsic_goal = false;
    bool planner_ready_ = false;  // true after PrimordialLoop broadcasts primordial_ready
    std::string system_knowledge; // injected into every prompt, updated by REM Engine
    // Temporal context from ChronosLobe
    std::string current_timestamp;
    std::string current_uptime;
    std::string current_time_of_day;
    bool is_night = false;
    int total_successes = 0;
    static constexpr int REM_TRIGGER_INTERVAL = 5;

    // Spike worker management
    int active_workers = 0;
    static constexpr int MAX_SPIKE_WORKERS = 2;
    std::map<std::string, json> pending_spike_assignments; // worker_id → assignment payload
    std::map<std::string, std::string> worker_cid_map;       // worker_id → CID (presence = worker alive)
    std::map<std::string, pid_t> worker_pid_map;              // worker_id → PID (for reaping zombies)
    int spike_counter = 0;

    void reap_zombies() {
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            // Remove from pid map
            for (auto it = worker_pid_map.begin(); it != worker_pid_map.end(); ++it) {
                if (it->second == pid) {
                    worker_pid_map.erase(it);
                    break;
                }
            }
        }
    }

    void spawn_spike(const std::string& cid, const GoalState& state) {
        std::string worker_id = "sp_" + std::to_string(++spike_counter) + "_" + std::to_string(std::time(nullptr));

        // Each worker gets its own unique CID to avoid cross-talk
        std::string worker_cid = cid + "_" + std::to_string(spike_counter);

        // Prepare assignment payload — will be sent when the worker announces readiness
        json assignment = {
            {"cid", worker_cid}, {"origin", "frontal_executive"}, {"intent", "spike_assign"},
            {"worker_id", worker_id},
            {"text", state.goal},
            {"task_id", state.task_id},
            {"domain", state.domain},
            {"fitness_score", state.fitness_score},
            {"is_intrinsic", state.is_intrinsic}
        };
        pending_spike_assignments[worker_id] = assignment;
        worker_cid_map[worker_id] = worker_cid;

        // Fork + exec the spike_worker binary
        pid_t pid = fork();
        if (pid == 0) {
            // Child process — exec spike_worker
            signal(SIGCHLD, SIG_DFL);
            std::string bin = "./build/spike_worker";
            execl(bin.c_str(), "spike_worker", worker_id.c_str(), nullptr);
            // If exec fails, exit immediately
            _exit(1);
        } else if (pid > 0) {
            active_workers++;
            worker_pid_map[worker_id] = pid;
            std::cout << "[EXECUTIVE] Spawned Spike worker " << worker_id
                      << " (pid=" << pid << ") for CID " << worker_cid << std::endl;
        } else {
            std::cerr << "[EXECUTIVE] Failed to fork Spike worker." << std::endl;
            pending_spike_assignments.erase(worker_id);
            worker_cid_map.erase(worker_id);
        }
    }

    void handle_spike_ready(const json& data) {
        std::string worker_id = data.value("worker_id", "");
        auto it = pending_spike_assignments.find(worker_id);
        if (it == pending_spike_assignments.end()) return;

        std::cout << "[EXECUTIVE] Spike " << worker_id << " ready. Dispatching assignment." << std::endl;
        dispatch_to_all(it->second);
        pending_spike_assignments.erase(it);
    }

    void handle_spike_done(const json& data) {
        std::string worker_id = data.value("worker_id", "");

        // Dedup guard — ignore if worker already finished (PUB/SUB can deliver duplicates)
        if (worker_cid_map.find(worker_id) == worker_cid_map.end()) return;

        bool success = data.value("success", false);
        std::string task_id = data.value("task_id", "");
        std::string cid = data.value("cid", "");

        active_workers = std::max(0, active_workers - 1);
        worker_cid_map.erase(worker_id);

        // Reap the child process to prevent zombies
        auto pit = worker_pid_map.find(worker_id);
        if (pit != worker_pid_map.end()) {
            waitpid(pit->second, nullptr, WNOHANG);
            worker_pid_map.erase(pit);
        }

        std::cout << "[EXECUTIVE] Spike " << worker_id << " finished: "
                  << (success ? "SUCCESS" : "FAILURE") << std::endl;

        // If this was a Ralph task, mark it complete
        if (success && !task_id.empty()) {
            GoalState tmp;
            tmp.task_id = task_id;
            tmp.last_cmd = data.value("command", "");
            active_goals[cid] = tmp;
            mark_task_complete(cid);
            active_goals.erase(cid);
        }

        // If intrinsic, report back to BasalGanglia
        if (data.value("is_intrinsic", false)) {
            json result = {
                {"cid", cid}, {"origin", "frontal_executive"},
                {"intent", "intrinsic_goal_result"},
                {"domain", data.value("domain", "")},
                {"fitness_score", data.value("fitness_score", 0.0f)},
                {"success", success},
                {"command", data.value("command", "")}
            };
            dispatch_to_all(result);
        }

        // Clean up goal state so the idle timer can trigger the next cycle
        if (!cid.empty()) {
            active_goals.erase(cid);
        }
        broadcast_idle_if_empty();
    }

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
                publish_intrinsic_result(cid, false);
                active_goals.erase(cid);
                broadcast_idle_if_empty();
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

    // Detect whether user input is an actionable goal (needs bash) or conversation
    bool is_conversational(const std::string& text) {
        std::string lower = text;
        for (auto& c : lower) c = std::tolower(c);

        // Actionable keywords — if present, treat as a goal for the bash pipeline
        static const std::vector<std::string> action_words = {
            "create ", "build ", "compile ", "install ", "run ", "execute ",
            "find ", "search ", "delete ", "remove ", "move ", "copy ",
            "make ", "write ", "read ", "open ", "close ", "start ", "stop ",
            "list ", "show me ", "download ", "upload ", "deploy ", "test ",
            "fix ", "update ", "modify ", "change ", "edit ", "add ",
            "cria ", "compila ", "instala ", "corre ", "executa ",
            "apaga ", "remove ", "move ", "copia ", "faz ", "escreve ",
            "git ", "cmake ", "npm ", "pip ", "docker ", "ssh ",
            "cat ", "grep ", "ls ", "cd ", "mkdir ", "rm ", "cp ", "mv ",
        };
        for (const auto& w : action_words) {
            if (lower.find(w) != std::string::npos) return false;
        }

        // Everything else from a user terminal is conversational
        return true;
    }

    void handle_conversational(const std::string& cid, const std::string& text) {
        std::cout << "[EXECUTIVE] Conversational query detected: " << text << std::endl;

        // Use LLM with free-form prompt (no JSON grammar, no bash command)
        json req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "inference_request"},
            {"adapter", "default"},
            {"text", "<|system|>\nYou are NeuroSwarm, an autonomous cognitive architecture that bootstraps from zero knowledge. "
                     "You are a distributed system of C++ lobes communicating via ZeroMQ, with intrinsic motivation, "
                     "neurogenesis, dream sandbox testing, and semantic memory. You run on the user's local machine. "
                     "Answer conversationally and concisely. Do not output JSON or bash commands.\n<|end|>\n"
                     "<|user|>\n" + text + "\n<|end|>\n<|assistant|>\n"}
        };
        dispatch_to_all(req);
    }

    void start_new_goal(const json& data) {
        std::string cid  = data.value("cid", "user_" + std::to_string(std::time(nullptr)));
        std::string goal = data.value("text", "");

        // Check if this is a conversational query (no bash command needed)
        bool is_user_stimulus = (data.value("origin", "") == "broca_terminal" ||
                                 data.value("origin", "") == "user_terminal");
        if (is_user_stimulus && is_conversational(goal)) {
            handle_conversational(cid, goal);
            return;
        }

        GoalState state;
        state.goal   = goal;
        state.active = true;

        // Delegate to Spike worker if capacity allows and not a user stimulus
        if (!is_user_stimulus && active_workers < MAX_SPIKE_WORKERS) {
            std::cout << "[EXECUTIVE] Delegating goal to Spike worker: " << goal.substr(0, 80) << " [CID: " << cid << "]" << std::endl;
            spawn_spike(cid, state);
            return;
        }

        active_goals[cid] = state;
        std::cout << "[EXECUTIVE] New Goal Registered (inline): " << goal << " [CID: " << cid << "]" << std::endl;

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
                bool mem_success = m.value("success", true);
                state.memory_context += (mem_success ? "- OK: " : "- FAILED (avoid): ")
                                      + m.value("command", "unknown")
                                      + " | " + m.value("result_summary", "").substr(0, 120)
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

        // RLAIF: track every command attempted for this goal
        if (!state.last_cmd.empty())
            state.executed_commands.push_back(state.last_cmd);

        if (status == "success") {
            if (mode == "dream") {
                std::cout << "[EXECUTIVE] Dream simulation SUCCESS. Collapsing to reality..." << std::endl;
                commit_to_reality(cid);
            } else if (!state.plan.empty()) {
                // Planner has more steps queued — execute next
                std::string next_cmd = state.plan.front();
                state.plan.erase(state.plan.begin());
                state.last_cmd = next_cmd;
                state.history += "\n[PLANNER] Next operator: " + next_cmd;
                std::cout << "[EXECUTIVE] Planner step OK. Next: " << next_cmd
                          << " (" << state.plan.size() << " remaining)" << std::endl;
                json exec_req = {
                    {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
                    {"command", next_cmd}, {"mode", "reality"}
                };
                dispatch_to_all(exec_req);
            } else {
                std::cout << "[EXECUTIVE] Reality Check: SUCCESS." << std::endl;
                if (state.last_mode == "neuro_surgery") {
                    std::cout << "[EXECUTIVE] Neuro-Surgery verified. Evolution complete." << std::endl;
                }
                if (!state.task_id.empty()) {
                    mark_task_complete(cid);
                }
                publish_intrinsic_result(cid, true);
                active_goals.erase(cid);
                broadcast_idle_if_empty();

                // Continuous REM: trigger learning every N successes
                total_successes++;
                if (total_successes % REM_TRIGGER_INTERVAL == 0) {
                    std::cout << "[EXECUTIVE] Triggering continuous REM after " << total_successes << " successes." << std::endl;
                    json rem = {{"origin", "frontal_executive"}, {"intent", "initiate_sleep_cycle"}};
                    dispatch_to_all(rem);
                }
            }
        } else {
            if (mode == "dream") {
                std::cout << "[EXECUTIVE] Dream simulation FAILED. Refining strategy." << std::endl;
            } else {
                std::cout << "[EXECUTIVE] Reality Check: FAILURE. Refining strategy." << std::endl;
            }
            state.history += "\n[ATTEMPT FAILED] cmd='" + state.last_cmd + "' error='" + output.substr(0, 300) + "'";
            state.retries++;

            // If we have more suggested commands, try the next one before LLM
            if (!state.suggested_commands.empty()) {
                std::string next_cmd = state.suggested_commands.front();
                state.suggested_commands.erase(state.suggested_commands.begin());
                state.last_cmd = next_cmd;
                state.last_mode = "reality";
                state.history += "\n[SUGGESTED] Trying next: " + next_cmd;
                std::cout << "[EXECUTIVE] Suggested cmd failed. Trying next: " << next_cmd
                          << " (" << state.suggested_commands.size() << " remaining)" << std::endl;
                json exec_req = {
                    {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
                    {"command", next_cmd}, {"mode", "reality"}
                };
                dispatch_to_all(exec_req);
                return;
            }

            if (state.retries >= 8) {
                std::cout << "[EXECUTIVE] Too many retries for CID " << cid << ". Abandoning goal." << std::endl;
                publish_intrinsic_result(cid, false);
                active_goals.erase(cid);
                broadcast_idle_if_empty();
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

    // Three-tier task selection:
    //   Tier 1 — External tasks from tasks.json (backward compatible)
    //   Tier 2 — Intrinsic motivation via BasalGanglia
    //   Tier 3 — Hardcoded epistemic fallback
    void pick_next_task() {
        // Phase 5: Emergency stamina gate — refuse all non-survival work when exhausted
        if (current_stamina < 5.0f) {
            std::cout << "[EXECUTIVE] EMERGENCY: stamina=" << (int)current_stamina
                      << "%. Refusing new goals. Waiting for recovery." << std::endl;
            json sleep = {{"origin", "frontal_executive"}, {"intent", "initiate_sleep_cycle"},
                          {"reason", "emergency_exhaustion"}};
            dispatch_to_all(sleep);
            return;
        }
        // Tier 1: Check tasks.json for incomplete external tasks
        std::ifstream f("./tasks.json");
        if (f.is_open()) {
            json tasks_doc;
            try {
                f >> tasks_doc;
                f.close();

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

                if (best_idx != -1) {
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

                    std::cout << "[EXECUTIVE] Ralph Loop — starting task [" << task_id << "]: " << title << std::endl;

                    // Delegate to Spike worker if capacity allows
                    if (active_workers < MAX_SPIKE_WORKERS) {
                        spawn_spike(cid, state);
                        return;
                    }

                    active_goals[cid] = state;

                    json mem_req = {
                        {"cid", cid}, {"origin", "frontal_executive"},
                        {"intent", "search_memory"}, {"query", desc}
                    };
                    dispatch_to_all(mem_req);
                    return;
                }
            } catch (...) {
                std::cout << "[EXECUTIVE] tasks.json is malformed. Falling through to intrinsic motivation." << std::endl;
            }
        }

        // Tier 2: Request intrinsic goal from BasalGanglia
        if (!waiting_for_intrinsic_goal) {
            waiting_for_intrinsic_goal = true;
            std::string cid = "intrinsic_" + std::to_string(std::time(nullptr));
            json req = {
                {"cid", cid}, {"origin", "frontal_executive"},
                {"intent", "intrinsic_goal_request"}
            };
            dispatch_to_all(req);
            std::cout << "[EXECUTIVE] All external tasks complete. Requesting intrinsic goal from BasalGanglia..." << std::endl;
            return;
        }

        // Tier 3: Hardcoded epistemic fallback (if BasalGanglia hasn't responded)
        waiting_for_intrinsic_goal = false;
        std::string cid = "epistemic_" + std::to_string(std::time(nullptr));
        std::string goal = "Self-Assigned Task: Analyze src/brainstem/Thalamus.cpp and suggest a performance optimization using neuro_surgery.";
        GoalState state;
        state.goal = goal;
        state.active = true;
        active_goals[cid] = state;
        std::cout << "[EXECUTIVE] Epistemic Drive Triggered (fallback)." << std::endl;
        request_thought(cid, "GOAL: " + goal + "\nInitiate task breakdown for this self-assigned learning goal.");
    }

    // Handle intrinsic goal from BasalGanglia — creates a GoalState from the motivation signal
    void handle_intrinsic_goal(const json& data) {
        // Dedup guard — only accept first intrinsic_goal per request cycle
        if (!waiting_for_intrinsic_goal) return;
        waiting_for_intrinsic_goal = false;

        std::string cid = data.value("cid", "intrinsic_" + std::to_string(std::time(nullptr)));
        std::string domain = data.value("domain", "unknown");
        float fitness = data.value("fitness", 0.0f);
        std::string context = data.value("context", "");
        auto suggested = data.value("suggested_commands", json::array());

        // Build the goal description from domain + suggested commands
        std::string goal = "Intrinsic exploration of '" + domain + "' domain. " + context;
        if (!suggested.empty()) {
            goal += "\nSuggested commands: ";
            for (size_t i = 0; i < suggested.size() && i < 5; i++) {
                goal += "\n  " + std::to_string(i+1) + ". " + suggested[i].get<std::string>();
            }
            goal += "\nSelect and execute the most informative command from the list above.";
        }

        GoalState state;
        state.goal = goal;
        state.active = true;
        state.is_intrinsic = true;
        state.domain = domain;
        state.fitness_score = fitness;
        // Store suggested commands for direct execution if Planner misses
        for (size_t i = 0; i < suggested.size() && i < 5; i++) {
            state.suggested_commands.push_back(suggested[i].get<std::string>());
        }

        std::cout << "[EXECUTIVE] Intrinsic goal accepted: domain='" << domain
                  << "' fitness=" << fitness << " [CID: " << cid << "]" << std::endl;

        active_goals[cid] = state;

        // Autopoiesis: try Planner first — use learned operators before LLM
        if (!domain.empty() && domain != "unknown") {
            try_planner_first(cid, domain);
            return;
        }

        // No domain or unknown — fall back to Spike/LLM
        if (active_workers < MAX_SPIKE_WORKERS) {
            active_goals.erase(cid);
            spawn_spike(cid, state);
            return;
        }

        json mem_req = {
            {"cid", cid}, {"origin", "frontal_executive"},
            {"intent", "search_memory"}, {"query", goal}
        };
        dispatch_to_all(mem_req);
    }

    // ─── Autopoiesis: Planner-first execution ───
    // Before using LLM, ask PrimordialLoop if a plan exists from learned operators.
    // Maps domain to postcondition, sends goal_request, handles plan response.

    std::set<std::string> pending_plans;  // CIDs awaiting goal_plan response

    void try_planner_first(const std::string& cid, const std::string& domain) {
        // If PrimordialLoop hasn't finished bootstrap yet, skip Planner
        if (!planner_ready_) {
            std::cout << "[EXECUTIVE] Planner not ready — trying suggested commands directly." << std::endl;
            auto it = active_goals.find(cid);
            if (it != active_goals.end() && !it->second.suggested_commands.empty()) {
                try_suggested_commands(cid);
            } else {
                fallback_to_llm(cid);
            }
            return;
        }

        // Map domain to postcondition for the Planner
        static const std::map<std::string, std::string> domain_goals = {
            {"file_write",          "can_write_file"},
            {"file_read",           "can_read_file"},
            {"file_search",         "can_search_files"},
            {"process_inspection",  "can_see_processes"},
            {"network_diagnostics", "network_info_available"},
            {"source_modification", "can_modify_source"},
            {"compilation",         "can_create_tool"},
            {"git_operations",      "know_git_state"},
            {"system_monitoring",   "know_system_state"},
            {"data_analysis",       "can_analyse_data"},
            {"script_creation",     "can_create_script"},
            {"self_inspection",     "know_self_state"},
            {"memory_analysis",     "can_analyse_memory"},
            {"log_analysis",        "can_analyse_logs"}
        };

        auto it = domain_goals.find(domain);
        if (it == domain_goals.end()) return;  // unknown domain, skip planner

        pending_plans.insert(cid);

        json req = {
            {"origin", "frontal_executive"},
            {"intent", "goal_request"},
            {"cid", cid},
            {"goals", json::array({it->second})},
            {"auto_execute", false},
            {"auto_generate", false}
        };
        dispatch_to_all(req);

        std::cout << "[EXECUTIVE] Planner query sent: domain='" << domain
                  << "' goal='" << it->second << "' [CID: " << cid << "]" << std::endl;
    }

    void handle_goal_plan(const json& data) {
        std::string cid = data.value("cid", "");
        if (!pending_plans.count(cid)) return;
        pending_plans.erase(cid);

        auto it = active_goals.find(cid);
        if (it == active_goals.end()) return;
        auto& state = it->second;

        bool success = data.value("success", false);
        auto steps = data.value("steps", json::array());

        if (success && !steps.empty()) {
            // Plan found — execute steps directly via MotorLobe, no LLM needed
            std::cout << "[EXECUTIVE] PLANNER HIT: " << steps.size()
                      << " operator steps for domain '" << state.domain << "'. Executing without LLM." << std::endl;

            // Execute the first step (chained via process_observation on success)
            auto& step = steps[0];
            std::string cmd = step.value("command", "");
            if (cmd.empty()) {
                std::cout << "[EXECUTIVE] Planner step has no command. Falling back to LLM." << std::endl;
                fallback_to_llm(cid);
                return;
            }

            // Store remaining steps in plan for sequential execution
            state.plan.clear();
            for (size_t i = 1; i < steps.size(); i++) {
                std::string step_cmd = steps[i].value("command", "");
                if (!step_cmd.empty()) state.plan.push_back(step_cmd);
            }

            state.last_cmd = cmd;
            state.last_mode = "reality";
            state.history += "\n[PLANNER] Executing learned operator: " + cmd;

            json exec_req = {
                {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
                {"command", cmd}, {"mode", "reality"}
            };
            dispatch_to_all(exec_req);
        } else if (!state.suggested_commands.empty()) {
            std::cout << "[EXECUTIVE] PLANNER MISS → trying suggested commands." << std::endl;
            try_suggested_commands(cid);
        } else {
            // No plan and no suggestions — fall back to LLM inference
            auto gaps = data.value("gaps", json::array());
            std::cout << "[EXECUTIVE] PLANNER MISS: no plan for domain '" << state.domain << "'";
            if (!gaps.empty()) std::cout << " (gaps: " << gaps.dump() << ")";
            std::cout << ". Falling back to LLM." << std::endl;
            fallback_to_llm(cid);
        }
    }

    void try_suggested_commands(const std::string& cid) {
        auto it = active_goals.find(cid);
        if (it == active_goals.end()) return;
        auto& state = it->second;

        if (state.suggested_commands.empty()) {
            fallback_to_llm(cid);
            return;
        }

        std::string cmd = state.suggested_commands.front();
        state.suggested_commands.erase(state.suggested_commands.begin());

        std::cout << "[EXECUTIVE] SUGGESTED CMD: " << cmd
                  << " (" << state.suggested_commands.size() << " remaining)" << std::endl;

        state.last_cmd = cmd;
        state.last_mode = "reality";
        state.history += "\n[SUGGESTED] " + cmd;

        json exec_req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "execution_request"},
            {"command", cmd}, {"mode", "reality"}
        };
        dispatch_to_all(exec_req);
    }

    void fallback_to_llm(const std::string& cid) {
        auto it = active_goals.find(cid);
        if (it == active_goals.end()) return;
        auto& state = it->second;

        // Query concept space for transfer before LLM
        query_concepts_for_goal(cid, state.goal);

        // Proceed with normal LLM-based flow: memory search → inference
        json mem_req = {
            {"cid", cid}, {"origin", "frontal_executive"},
            {"intent", "search_memory"}, {"query", state.goal}
        };
        dispatch_to_all(mem_req);
    }

    // RLAIF: dopamine signal received — reinforce participating commands
    void handle_dopamine(const json& data) {
        std::string domain = data.value("domain", "");
        float magnitude = data.value("magnitude", 0.0f);

        // Check recently completed goals for matching domain — reinforce once then consume
        std::vector<std::string> consumed;
        for (auto& [cid, chain] : recent_chains) {
            if (chain.domain == domain && !chain.commands.empty()) {
                json reinforce = {
                    {"origin", "frontal_executive"},
                    {"intent", "rlaif_reinforce"},
                    {"domain", domain},
                    {"magnitude", magnitude},
                    {"commands", chain.commands}
                };
                dispatch_to_all(reinforce);
                std::cout << "[EXECUTIVE] RLAIF: Reinforcing " << chain.commands.size()
                          << " commands in domain '" << domain
                          << "' (magnitude=" << magnitude << ")" << std::endl;
                consumed.push_back(cid);
            }
        }
        // Remove consumed chains to prevent duplicate reinforcement
        for (auto& cid : consumed) recent_chains.erase(cid);

        // Prune old chains (>60s)
        auto now = std::chrono::steady_clock::now();
        for (auto it = recent_chains.begin(); it != recent_chains.end(); ) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.completed_at).count() > 60)
                it = recent_chains.erase(it);
            else ++it;
        }
    }

    // RLAIF: completed execution chain cache
    struct CompletedChain {
        std::string domain;
        std::vector<std::string> commands;
        std::chrono::steady_clock::time_point completed_at;
    };
    std::map<std::string, CompletedChain> recent_chains;

    // --- Concept space integration ---

    // Handle concept transfer from BasalGanglia — additional commands from similar concepts
    void handle_concept_transfer(const json& data) {
        std::string cid = data.value("cid", "");
        auto transfer = data.value("transfer_commands", json::array());
        if (cid.empty() || transfer.empty()) return;

        auto it = active_goals.find(cid);
        if (it == active_goals.end()) return;

        // Append transfer commands to the goal's suggested commands
        int added = 0;
        for (auto& cmd : transfer) {
            std::string c = cmd.get<std::string>();
            // Avoid duplicates
            bool dup = false;
            for (auto& existing : it->second.suggested_commands) {
                if (existing == c) { dup = true; break; }
            }
            if (!dup && added < 3) {
                it->second.suggested_commands.push_back(c);
                added++;
            }
        }

        if (added > 0) {
            std::cout << "[EXECUTIVE] CONCEPT TRANSFER: Added " << added
                      << " commands from similar concepts for CID " << cid << std::endl;
        }
    }

    // Handle direct concept response — for future use (e.g., before LLM fallback)
    std::map<std::string, std::string> pending_concept_lookups;  // concept_cid → goal_cid

    void handle_concept_response(const json& data) {
        std::string cid = data.value("cid", "");
        if (!pending_concept_lookups.count(cid)) return;

        std::string goal_cid = pending_concept_lookups[cid];
        pending_concept_lookups.erase(cid);

        auto it = active_goals.find(goal_cid);
        if (it == active_goals.end()) return;

        auto concepts = data.value("concepts", json::array());
        if (concepts.empty()) return;

        // Use concept patterns as additional context for the LLM prompt
        std::string concept_context = "\nRelated concepts from experience:";
        for (auto& c : concepts) {
            float sim = c.value("similarity", 0.0f);
            if (sim < 0.4f) continue;
            std::string name = c.value("concept", "");
            std::string pattern = c.value("pattern", "");
            float rate = c.value("success_rate", 0.0f);
            concept_context += "\n  - " + name + " (pattern: " + pattern
                             + ", success: " + std::to_string((int)(rate * 100)) + "%)";
        }

        it->second.history += concept_context;
        std::cout << "[EXECUTIVE] CONCEPT CONTEXT: Enriched goal " << goal_cid
                  << " with " << concepts.size() << " related concepts" << std::endl;
    }

    // Query concept space before falling back to LLM
    void query_concepts_for_goal(const std::string& cid, const std::string& goal) {
        std::string concept_cid = "fe_concept_" + cid;
        pending_concept_lookups[concept_cid] = cid;

        json query = {
            {"origin", "frontal_executive"}, {"intent", "concept_query"},
            {"cid", concept_cid}, {"query", goal}
        };
        dispatch_to_all(query);
    }

    // Publish intrinsic goal result back to BasalGanglia for self-model update
    void publish_intrinsic_result(const std::string& cid, bool success) {
        auto it = active_goals.find(cid);
        if (it == active_goals.end()) return;
        if (!it->second.is_intrinsic) return;

        // RLAIF: cache completed chain for dopamine reinforcement
        // Only keep one chain per domain to prevent multiplied reinforcement
        if (success && !it->second.executed_commands.empty()) {
            // Remove any existing chain for this domain first
            for (auto rc = recent_chains.begin(); rc != recent_chains.end(); ) {
                if (rc->second.domain == it->second.domain)
                    rc = recent_chains.erase(rc);
                else ++rc;
            }
            CompletedChain chain;
            chain.domain = it->second.domain;
            chain.commands = it->second.executed_commands;
            chain.completed_at = std::chrono::steady_clock::now();
            recent_chains[cid] = chain;
        }

        json result = {
            {"cid", cid}, {"origin", "frontal_executive"},
            {"intent", "intrinsic_goal_result"},
            {"domain", it->second.domain},
            {"fitness_score", it->second.fitness_score},
            {"success", success},
            {"command", it->second.last_cmd},
            {"executed_commands", it->second.executed_commands}
        };
        dispatch_to_all(result);
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
    }

    void request_thought(const std::string& cid, const std::string& extra_prompt = "") {
        if (active_goals.find(cid) == active_goals.end()) return;
        auto& state = active_goals[cid];

        state.total_thought_cycles++;
        if (state.total_thought_cycles > 20) {
            std::cout << "[EXECUTIVE] Oscillation detected for CID " << cid
                      << " (" << state.total_thought_cycles << " thought cycles). Abandoning goal." << std::endl;
            publish_intrinsic_result(cid, false);
            active_goals.erase(cid);
            broadcast_idle_if_empty();
            return;
        }

        json req = {
            {"cid", cid}, {"origin", "frontal_executive"}, {"intent", "inference_request"},
            {"adapter", "coder"},
            {"grammar", "root   ::= object\nobject ::= \"{\" ws ( pair ( \",\" ws pair )* )? \"}\"\npair   ::= string \":\" ws value\nvalue  ::= string | number | object | array | \"true\" | \"false\" | \"null\"\nstring ::= \"\\\"\" ( [^\"\\\\\\n\\r] | \"\\\\\" ( [\"\\\\/bfnrt] | \"u\" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] ) )* \"\\\"\"\nnumber ::= \"-\"? ( [0-9] | [1-9] [0-9]* ) ( \".\" [0-9]+ )? ( [eE] [-+]? [0-9]+ )?\narray  ::= \"[\" ws ( value ( \",\" ws value )* )? \"]\"\nws     ::= [ \\t\\n\\r]*\n"},
            {"text", "<|system|>\nYou are NeuroSwarm, an autonomous cognitive architecture. Reply ONLY with JSON: {\"thought\":\"brief\",\"command\":\"REAL_BASH_CMD\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\nModes: reality (normal), neuro_surgery (modify+recompile src/*.cpp)\nRules: command MUST be executable bash. No placeholders. No explanations outside JSON.\n"
             + (system_knowledge.empty() ? "" : system_knowledge + "\n")
             + "<|end|>\n<|user|>\n"
             + (current_timestamp.empty() ? "" : "T:" + current_timestamp + " ")
             + "GOAL: " + state.goal + state.memory_context + "\n" + (state.history.empty() ? "" : "HISTORY:" + state.history.substr(0, 500) + "\n") + extra_prompt + "<|end|>\n<|assistant|>\n"}
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

    // Broadcast cognitive idle status so dashboard resets Cognitive Focus display
    void broadcast_idle_if_empty() {
        if (!active_goals.empty()) return;
        json idle = {
            {"origin", "frontal_executive"},
            {"intent", "cognitive_idle"}
        };
        dispatch_to_all(idle);
    }

    void dispatch_to_all(const json& data) {
        routing::publish(pub, data);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::FrontalExecutive exec;
    exec.run_cognitive_cycle();
    return 0;
}
