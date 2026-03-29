#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdlib>
#include <signal.h>
#include <regex>

using json = nlohmann::json;

namespace neuroswarm {

// SpikeWorker — ephemeral single-goal cognitive process.
//
// Biological analogue: a cortical minicolumn recruited for a specific task,
// which fires intensely for the duration of the task and is then pruned.
//
// Each worker handles exactly one goal through the full cognitive cycle:
// memory recall → thought → critic → dream → reality → done.
// On completion or failure the process terminates. Workers share the
// SynapticController, CriticLobe, MotorLobe, and Hippocampus via the bus.
//
// Spawned by FrontalExecutive via fork+exec. Auto-reaped by SIGCHLD=SIG_IGN
// inherited from CerebralMatrix.

class SpikeWorker {
public:
    SpikeWorker(const std::string& worker_id,
                  const std::string& thalamus_ip = "localhost")
        : worker_id(worker_id),
          ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {
            "spike_assign", "search_result", "inference_result",
            "critic_result", "execution_result", "time_pulse",
            "specialist_advice"
        });

        // Allow ZMQ subscription to propagate before sending messages
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        load_system_knowledge();

        std::cout << "[SPIKE:" << worker_id << "] Ephemeral worker online." << std::endl;
    }

    void run() {
        // Announce readiness — FrontalExecutive will respond with assignment
        json ready = {
            {"origin", "spike_worker"}, {"intent", "spike_ready"},
            {"worker_id", worker_id}
        };
        dispatch(ready);

        auto last_activity = std::chrono::steady_clock::now();

        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) {
                // Timeout: self-terminate if idle for 120 seconds
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_activity).count() > 120) {
                    std::cout << "[SPIKE:" << worker_id << "] Timeout — no activity for 120s. Terminating." << std::endl;
                    publish_done(false);
                    return;
                }

                // Memory recall timeout: proceed after 3 seconds
                if (!cid.empty() && !memory_searched) {
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - assignment_time).count() >= 3) {
                        std::cout << "[SPIKE:" << worker_id << "] Memory recall timeout. Proceeding." << std::endl;
                        memory_searched = true;
                        request_thought("Initial task breakdown for goal: " + goal);
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            try {
                std::string origin = j.value("origin", "");
                std::string intent = j.value("intent", "");
                std::string msg_cid = j.value("cid", "");

                // Accept assignment from FrontalExecutive
                if (intent == "spike_assign" && j.value("worker_id", "") == worker_id) {
                    handle_assignment(j);
                    last_activity = std::chrono::steady_clock::now();
                    continue;
                }

                // Ignore messages not matching our CID (avoid cross-talk)
                if (cid.empty() || msg_cid != cid) continue;

                last_activity = std::chrono::steady_clock::now();

                // Temporal context from ChronosLobe (accept without CID filter)
                if (origin == "chronos" && intent == "time_pulse") {
                    current_timestamp = j.value("timestamp", "");
                    current_uptime = j.value("uptime_human", "");
                    current_time_of_day = j.value("time_of_day", "");
                    continue;
                }

                if (origin == "hippocampus" && intent == "search_result") {
                    handle_memory_recall(j);
                }
                else if (origin == "synaptic_controller" && intent == "inference_result") {
                    std::string adapter = j.value("adapter", "");
                    if (adapter == "critic") {
                        handle_critic_feedback(j);
                    } else if (adapter == "executive" || adapter == "default") {
                        handle_thought(j);
                    }
                }
                else if (origin == "critic_lobe" && intent == "critic_result") {
                    handle_critic_feedback(j);
                }
                else if (origin == "motor_cortex" && intent == "execution_result") {
                    handle_execution_result(j);
                }
                // Specialist advice — buffer for use in commit_to_dream
                else if (intent == "specialist_advice") {
                    handle_specialist_advice(j);
                }

            } catch (...) {}

        }
    }

private:
    std::string worker_id;
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    // Single goal state
    std::string cid;
    std::string goal;
    std::string task_id;
    std::string domain;
    float fitness_score = 0.0f;
    bool is_intrinsic = false;
    std::string history;
    std::string last_raw_thought;
    std::string last_cmd;
    std::string last_mode;
    int retries = 0;
    int critic_rejections = 0;
    int total_thought_cycles = 0; // Total inference cycles — detects oscillating loops
    bool memory_searched = false;
    std::string memory_context;
    std::chrono::steady_clock::time_point assignment_time;

    // System context
    std::string system_knowledge;
    std::string current_timestamp;
    std::string current_uptime;
    std::string current_time_of_day;

    // Specialist advice buffer: domain → pre-validations + cached alternatives
    struct SpecialistAdvice {
        std::vector<std::string> pre_validations;
        std::vector<std::string> cached_alternatives;
        std::chrono::steady_clock::time_point received_at;
    };
    std::map<std::string, SpecialistAdvice> specialist_advice_buffer;

    void handle_assignment(const json& data) {
        cid = data.value("cid", "");
        goal = data.value("text", "");
        task_id = data.value("task_id", "");
        domain = data.value("domain", "");
        fitness_score = data.value("fitness_score", 0.0f);
        is_intrinsic = data.value("is_intrinsic", false);
        assignment_time = std::chrono::steady_clock::now();

        std::cout << "[SPIKE:" << worker_id << "] Assigned goal [CID: " << cid << "]: "
                  << goal.substr(0, 80) << std::endl;

        // Query Hippocampus for relevant memory
        json mem_req = {
            {"cid", cid}, {"origin", "spike_worker"},
            {"intent", "search_memory"}, {"query", goal}
        };
        dispatch(mem_req);
    }

    void handle_memory_recall(const json& data) {
        if (memory_searched) return;
        memory_searched = true;

        auto matches = data.value("matches", json::array());
        if (!matches.empty()) {
            memory_context = "\n\nRELEVANT PAST EXPERIENCES (use these to inform your plan):\n";
            int shown = 0;
            for (auto& m : matches) {
                if (m.value("similarity", 0.0f) < 0.5f) continue;
                bool mem_success = m.value("success", true);
                memory_context += (mem_success ? "- OK: " : "- FAILED (avoid): ")
                                + m.value("command", "unknown")
                                + " | " + m.value("result_summary", "").substr(0, 120)
                                + " | SIM: " + std::to_string(m.value("similarity", 0.0f)).substr(0, 4) + "\n";
                if (++shown >= 3) break;
            }
            if (shown > 0)
                std::cout << "[SPIKE:" << worker_id << "] Memory augmented with " << shown << " experience(s)." << std::endl;
            else
                memory_context = "";
        }

        request_thought("Initial task breakdown for goal: " + goal);
    }

    void handle_thought(const json& data) {
        last_raw_thought = data.value("text", "");
        std::cout << "[SPIKE:" << worker_id << "] Thought received, size=" << last_raw_thought.size() << std::endl;

        // Detect inference engine errors — don't waste critic cycles on broken responses
        if (last_raw_thought.rfind("ERROR:", 0) == 0 || last_raw_thought.size() < 5) {
            std::cout << "[SPIKE:" << worker_id << "] Inference error detected: " << last_raw_thought.substr(0, 50) << ". Aborting." << std::endl;
            publish_done(false);
            return;
        }

        history += "\n[THOUGHT] " + last_raw_thought.substr(0, 200);

        // Send to CriticLobe
        json critic_req = {
            {"cid", cid}, {"origin", "spike_worker"}, {"intent", "critic_validate"},
            {"text", "PROPOSED ACTION:\n" + last_raw_thought + "\nDoes this plan achieve the goal safely? Respond with APPROVED or a specific critique."}
        };
        dispatch(critic_req);
    }

    void handle_critic_feedback(const json& data) {
        std::string feedback = data.value("text", "");

        if (feedback.find("APPROVED") != std::string::npos) {
            std::cout << "[SPIKE:" << worker_id << "] Consensus reached. Dream simulation." << std::endl;
            critic_rejections = 0;
            commit_to_dream();
        } else {
            critic_rejections++;
            std::cout << "[SPIKE:" << worker_id << "] Critic rejection (" << critic_rejections << "/3): "
                      << feedback.substr(0, 100) << std::endl;

            if (critic_rejections >= 3) {
                std::cout << "[SPIKE:" << worker_id << "] Neurotic loop. Aborting." << std::endl;
                publish_done(false);
                return;
            }
            request_thought("CRITIC FEEDBACK: " + feedback + "\nPlease refine the strategy.");
        }
    }

    void handle_specialist_advice(const json& data) {
        std::string adv_domain = data.value("domain", "");
        if (adv_domain.empty()) return;

        SpecialistAdvice advice;
        if (data.contains("pre_validations") && data["pre_validations"].is_array()) {
            for (auto& pv : data["pre_validations"])
                advice.pre_validations.push_back(pv.get<std::string>());
        }
        if (data.contains("cached_alternatives") && data["cached_alternatives"].is_array()) {
            for (auto& alt : data["cached_alternatives"])
                advice.cached_alternatives.push_back(alt.get<std::string>());
        }
        advice.received_at = std::chrono::steady_clock::now();
        specialist_advice_buffer[adv_domain] = advice;
    }

    void commit_to_dream() {
        auto [cmd, mode] = extract_command(last_raw_thought);

        if (cmd.empty()) {
            request_thought("Respond ONLY with a compact JSON: {\"thought\":\"brief\",\"command\":\"bash_cmd\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}. No long explanations.");
            return;
        }

        last_cmd = cmd;
        last_mode = mode;

        // Apply specialist pre-validations if available for this domain
        if (!domain.empty() && specialist_advice_buffer.count(domain)) {
            auto& advice = specialist_advice_buffer[domain];
            auto age = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - advice.received_at).count();
            if (age < 10 && !advice.pre_validations.empty()) {
                std::string prefix;
                for (auto& pv : advice.pre_validations)
                    prefix += pv + " && ";
                cmd = prefix + cmd;
                std::cout << "[SPIKE:" << worker_id << "] Applied specialist pre-validations for '"
                          << domain << "'." << std::endl;
            }
        }

        // Source modification / neuro-surgery must run against real files, not dream sandbox
        std::string exec_mode = "dream";
        if (mode == "neuro_surgery" || cid.find("surgery_") != std::string::npos ||
            domain == "source_modification") {
            exec_mode = "neuro_surgery";
        }

        json dream_req = {
            {"cid", cid}, {"origin", "spike_worker"}, {"intent", "execution_request"},
            {"command", cmd}, {"mode", exec_mode},
            {"domain", domain}
        };
        dispatch(dream_req);
    }

    void commit_to_reality() {
        json real_req = {
            {"cid", cid}, {"origin", "spike_worker"}, {"intent", "execution_request"},
            {"command", last_cmd}, {"mode", last_mode},
            {"domain", domain}
        };
        dispatch(real_req);
    }

    void handle_execution_result(const json& data) {
        std::string status = data.value("status", "");
        std::string output = data.value("proprioception", data.value("output", ""));
        std::string mode = data.value("mode", "reality");

        if (status == "success") {
            if (mode == "dream") {
                std::cout << "[SPIKE:" << worker_id << "] Dream SUCCESS. Collapsing to reality." << std::endl;
                commit_to_reality();
            } else {
                std::cout << "[SPIKE:" << worker_id << "] Reality SUCCESS. Task complete." << std::endl;
                publish_done(true);
            }
        } else {
            std::cout << "[SPIKE:" << worker_id << "] " << mode << " FAILED. Retrying." << std::endl;
            history += "\n[ATTEMPT FAILED] cmd='" + last_cmd + "' error='" + output.substr(0, 300) + "'";
            retries++;
            if (retries >= 8) {
                std::cout << "[SPIKE:" << worker_id << "] Too many retries. Aborting." << std::endl;
                publish_done(false);
                return;
            }
            request_thought("PREVIOUS ACTION FAILED (attempt " + std::to_string(retries) + "/8): "
                          + output.substr(0, 300) + "\nTry a different approach.");
        }
    }

    void publish_done(bool success) {
        json done = {
            {"cid", cid}, {"origin", "spike_worker"}, {"intent", "spike_done"},
            {"worker_id", worker_id},
            {"success", success},
            {"task_id", task_id},
            {"domain", domain},
            {"fitness_score", fitness_score},
            {"is_intrinsic", is_intrinsic},
            {"command", last_cmd}
        };
        dispatch(done);

        // Brief pause to ensure message is sent before exit
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Build source context for source_modification goals
    std::string get_source_context() {
        if (domain != "source_modification") return "";

        // Pick a target file from goal/history or rotate through key files
        std::string target;
        std::regex path_re(R"((src/[^\s'"]+\.(?:cpp|hpp|h))|(include/[^\s'"]+\.(?:hpp|h)))");
        std::smatch m;
        std::string search = goal + " " + history;
        if (std::regex_search(search, m, path_re)) {
            target = m[0].str();
        }
        if (target.empty()) {
            static const std::vector<std::string> key_files = {
                "src/brainstem/PrimordialLoop.cpp",
                "src/brainstem/FrontalExecutive.cpp",
                "src/lobes/BasalGanglia/BasalGangliaLobe.cpp",
                "include/OperatorRegistry.hpp",
                "src/brainstem/MetaCognition.cpp"
            };
            target = key_files[std::time(nullptr) % key_files.size()];
        }

        std::ifstream f(target);
        if (!f.is_open()) return "";
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(f, line)) lines.push_back(line);
        if (lines.empty()) return "";

        int total = lines.size();
        int window = std::min(40, total);
        int start = 0;
        if (total > window) {
            start = (std::time(nullptr) / 60) % (total - window);
        }

        std::string snippet;
        for (int i = start; i < start + window && i < total; i++) {
            snippet += std::to_string(i + 1) + ": " + lines[i] + "\n";
        }

        return "\n\nSOURCE FILE: " + target + " (lines " + std::to_string(start + 1)
             + "-" + std::to_string(start + window) + " of " + std::to_string(total) + ")\n"
             + snippet
             + "\nIMPORTANT: Write a sed -i command targeting EXACT text from the file above. "
               "The pattern must match a real line. Example: sed -i 's/old_exact_text/new_text/' " + target + "\n";
    }

    void request_thought(const std::string& extra_prompt = "") {
        total_thought_cycles++;
        if (total_thought_cycles > 20) {
            std::cout << "[SPIKE:" << worker_id << "] Oscillation detected (" << total_thought_cycles
                      << " thought cycles). Aborting goal." << std::endl;
            publish_done(false);
            return;
        }

        // Inject source file context for source_modification goals
        std::string source_ctx = get_source_context();

        json req = {
            {"cid", cid}, {"origin", "spike_worker"}, {"intent", "inference_request"},
            {"adapter", "executive"},
            {"grammar", "root   ::= object\nobject ::= \"{\" ws ( pair ( \",\" ws pair )* )? \"}\"\npair   ::= string \":\" ws value\nvalue  ::= string | number | object | array | \"true\" | \"false\" | \"null\"\nstring ::= \"\\\"\" ( [^\"\\\\\\n\\r] | \"\\\\\" ( [\"\\\\/bfnrt] | \"u\" [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] ) )* \"\\\"\"\nnumber ::= \"-\"? ( [0-9] | [1-9] [0-9]* ) ( \".\" [0-9]+ )? ( [eE] [-+]? [0-9]+ )?\narray  ::= \"[\" ws ( value ( \",\" ws value )* )? \"]\"\nws     ::= [ \\t\\n\\r]*\n"},
            {"text", "<|im_start|>system\nYou are NeuroSwarm, an autonomous cognitive architecture. Your working directory is /home/xenomai/Documents/NeuroSwarm/.\nReply ONLY with compact JSON: {\"thought\":\"brief\",\"command\":\"bash_cmd\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\nRules:\n- command MUST be a real, executable bash command. No placeholders like REAL_BASH_CMD.\n- Only access files within the project directory or /tmp/.\n- Never use sudo. Never reference paths outside the project.\n- Keep commands simple and direct.\nExamples:\n{\"thought\":\"list source files\",\"command\":\"find src/ -name '*.cpp'\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\n{\"thought\":\"compile\",\"command\":\"cmake --build build -j$(nproc)\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\n{\"thought\":\"check status\",\"command\":\"git status\",\"mode\":\"reality\",\"status\":\"IN_PROGRESS\"}\n"
             + (system_knowledge.empty() ? "" : system_knowledge + "\n")
             + "<|im_end|>\n<|im_start|>user\n/no_think\n"
             + (current_timestamp.empty() ? "" : "T:" + current_timestamp + " ")
             + "GOAL: " + goal + memory_context + "\n" + (history.empty() ? "" : "HISTORY:" + history.substr(0, 500) + "\n") + source_ctx + extra_prompt + "<|im_end|>\n<|im_start|>assistant\n"}
        };
        dispatch(req);
    }

    std::pair<std::string, std::string> extract_command(const std::string& raw) {
        try {
            size_t start = raw.find("{");
            size_t end   = raw.rfind("}");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                auto j = json::parse(raw.substr(start, end - start + 1));
                return {j.value("command", ""), j.value("mode", "reality")};
            }
        } catch (...) {}

        std::string cmd, mode = "reality";
        std::smatch m;
        std::regex cmd_re("\"command\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        if (std::regex_search(raw, m, cmd_re)) cmd = m[1].str();
        std::regex mode_re("\"mode\"\\s*:\\s*\"([^\"]*)\"");
        if (std::regex_search(raw, m, mode_re)) mode = m[1].str();
        return {cmd, mode};
    }

    void load_system_knowledge() {
        std::ifstream f("./data/system_knowledge.md");
        if (!f.is_open()) return;
        std::ostringstream ss;
        ss << f.rdbuf();
        system_knowledge = ss.str();
    }

    void dispatch(const json& data) {
        routing::publish(pub, data);
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: spike_worker <worker_id> [--thalamus <ip>]" << std::endl;
        return 1;
    }

    signal(SIGCHLD, SIG_DFL);

    std::string worker_id = argv[1];
    std::string ip = "localhost";
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[++i];
    }

    neuroswarm::SpikeWorker worker(worker_id, ip);
    worker.run();
    return 0;
}
