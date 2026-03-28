// NeuroSwarm Autopoiesis — PrimordialLoop
//
// The first act of consciousness. The system wakes up knowing nothing
// except that it can execute and observe. Everything else is discovered.
//
// Biological analogue: neonatal reflexes. A newborn doesn't know what a
// hand is — but it grasps. It doesn't know what light is — but it blinks.
// From these reflexes, a world model emerges.

#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <functional>
#include <filesystem>
#include <unistd.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <SurpriseEngine.hpp>
#include <OperatorRegistry.hpp>
#include <VariationEngine.hpp>
#include <Planner.hpp>
#include <LLMOracle.hpp>
#include <NetworkExpander.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace neuroswarm {

class PrimordialLoop {
public:
    PrimordialLoop()
        : ctx_(1),
          pub_(ctx_, zmq::socket_type::pub),
          sub_(ctx_, zmq::socket_type::sub),
          registry_("data/operators.jsonl") {

        pub_.connect("tcp://localhost:5555");
        routing::set_buffer_limit(sub_, 500);
        sub_.connect("tcp://localhost:5556");
        routing::subscribe(sub_, {"operator_request", "probe_request", "goal_request", "inference_result", "domain_resolve_request", "execution_result", "concept_update", "precondition_discovery"});

        fs::create_directories("data");
        fs::create_directories("data/sandbox");

        // Try to load previous world state + self model
        bool has_memory = load_persisted_state();

        // Purge degenerate operators from previous runs (mkdir spam, trivial commands)
        int purged = registry_.purge_degenerate();
        if (purged > 0) {
            std::cout << "[PRIMORDIAL] Purged " << purged << " degenerate operators from genome." << std::endl;
        }

        if (has_memory) {
            std::cout << "[PRIMORDIAL] Awakening. I remember " << world_state_.size()
                      << " facts, " << registry_.size() << " operators." << std::endl;
        } else {
            std::cout << "[PRIMORDIAL] Awakening. I know nothing." << std::endl;
        }
    }

    void start() {
        bool incremental = !world_state_.empty();

        // Phase 0: Existence
        std::cout << "[PRIMORDIAL] Phase 0 — I exist. I can execute." << std::endl;
        broadcast_phase("existence", "I can execute and observe.");

        // Phase 1: First Contact
        if (incremental && !self_.user.empty()) {
            std::cout << "[PRIMORDIAL] Phase 1 — Recalled: " << self_.user
                      << "@" << self_.hostname << " (" << self_.os << " " << self_.arch << ")" << std::endl;
        } else {
            std::cout << "[PRIMORDIAL] Phase 1 — First contact with reality." << std::endl;
            phase_first_contact();
        }

        // Phase 2: Capability Discovery
        if (incremental && !self_.available_binaries.empty()) {
            std::cout << "[PRIMORDIAL] Phase 2 — Recalled: " << self_.available_binaries.size()
                      << " binaries, " << self_.writable_dirs.size() << " writable dirs." << std::endl;
        } else {
            std::cout << "[PRIMORDIAL] Phase 2 — Discovering capabilities." << std::endl;
            phase_capability_discovery();
        }

        // Phase 3: Sense Acquisition — always re-probe (senses change)
        std::cout << "[PRIMORDIAL] Phase 3 — " << (incremental ? "Re-probing" : "Acquiring") << " senses." << std::endl;
        phase_sense_acquisition();

        // Phase 4: Tool Discovery
        if (incremental && !self_.languages.empty()) {
            std::cout << "[PRIMORDIAL] Phase 4 — Recalled: " << self_.languages.size()
                      << " languages (" ;
            for (const auto& l : self_.languages) std::cout << l << " ";
            std::cout << ")" << std::endl;
            // Re-verify compilers still exist (fast check)
            self_.can_compile_cpp = (exec("which g++ 2>/dev/null").exit_code == 0);
            self_.can_run_python = (exec("which python3 2>/dev/null").exit_code == 0);
        } else {
            std::cout << "[PRIMORDIAL] Phase 4 — Discovering tools." << std::endl;
            phase_tool_discovery();
        }

        // Always ensure self-modification operators exist (idempotent)
        ensure_self_modification_operators();

        // Phase 5: Active Exploration — scaled by experience
        // With many operators already known, reduce exploration to stay fast
        int probe_count = incremental ? 5 : 20;  // 5 probes if experienced, 20 if fresh
        std::cout << "[PRIMORDIAL] Phase 5 — Active exploration"
                  << (incremental ? " (light — " + std::to_string(registry_.size()) + " operators known)" : "")
                  << "." << std::endl;
        phase_active_exploration(probe_count);

        // Report
        report_self_model();

        std::cout << "[PRIMORDIAL] Bootstrap complete. "
                  << registry_.size() << " operators learned. "
                  << "Global surprise: " << surprise_.global_surprise()
                  << std::endl;

        // Initialize planner and LLM oracle with learned operators
        planner_ = std::make_unique<Planner>(registry_);
        oracle_ = std::make_unique<LLMOracle>(pub_, sub_);

        // Build initial world state from what we discovered
        for (const auto& [id, op] : get_all_postconditions()) {
            world_state_.insert(id);
        }

        broadcast_phase("bootstrap_complete", "Learned " + std::to_string(registry_.size()) + " operators.");

        // Phase 6: Self-test — verify the planner works with a simple goal
        std::cout << "[PRIMORDIAL] Phase X — Experimentation initialization." << std::endl;
        phase_planner_selftest();

        // Persist world state after all phases (world_state_ populated by selftest)
        save_world_state();

        // Phase 7: Network Expansion — discover and probe remote hosts
        if (self_.has_network) {
            std::cout << "[PRIMORDIAL] Phase 7 — Network expansion." << std::endl;
            network_ = std::make_unique<NetworkExpander>(pub_);
            network_->set_local_info(self_.user, self_.arch);
            phase_network_expansion();
        } else {
            std::cout << "[PRIMORDIAL] Phase 7 — Skipped (no network)." << std::endl;
        }

        // Enrich operators with inferred postconditions
        enrich_operator_postconditions();

        // Signal readiness to all lobes
        json ready = {
            {"origin", "primordial_loop"},
            {"intent", "primordial_ready"},
            {"operators", static_cast<int>(registry_.size())},
            {"world_facts", static_cast<int>(world_state_.size())}
        };
        routing::publish(pub_, ready);
        std::cout << "[PRIMORDIAL] Ready signal broadcast. " << registry_.size()
                  << " operators, " << world_state_.size() << " facts." << std::endl;

        // Enter service loop — respond to operator/goal requests from other lobes
        service_loop();
    }

private:
    zmq::context_t ctx_;
    zmq::socket_t pub_;
    zmq::socket_t sub_;
    SurpriseEngine surprise_;
    OperatorRegistry registry_;
    VariationEngine variation_;
    std::unique_ptr<Planner> planner_;
    std::unique_ptr<LLMOracle> oracle_;
    std::unique_ptr<NetworkExpander> network_;
    std::vector<RemoteHost> remote_hosts_;
    WorldState world_state_;  // current known facts about the world

    // Helper: collect all postconditions from learned operators as known facts
    std::vector<std::pair<std::string, std::string>> get_all_postconditions() {
        std::vector<std::pair<std::string, std::string>> facts;
        auto stable = registry_.get_stable();
        // Since stable requires 5 uses, also include all bootstrap operators
        // by directly scanning the registry's postconditions
        // For now, seed with facts from successful probes
        return facts;
    }

    // Self-model (Axiom 5)
    struct SelfModel {
        std::string user;
        std::string home;
        std::string hostname;
        std::string os;
        std::string arch;
        std::vector<std::string> path_dirs;
        std::vector<std::string> available_binaries;
        std::vector<std::string> writable_dirs;
        std::vector<std::string> languages;      // discovered programming languages
        bool has_network = false;
        bool has_gpu = false;
        bool can_compile_cpp = false;
        bool can_run_python = false;
    } self_;

    // ─── Execute and observe (Axiom 1) ───

    struct ExecResult {
        std::string output;
        int exit_code;
        double duration_ms;
    };

    ExecResult exec(const std::string& cmd) {
        auto start = std::chrono::steady_clock::now();

        std::array<char, 256> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(
            popen((cmd + " 2>&1").c_str(), "r"), pclose);

        int exit_code = -1;
        if (pipe) {
            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                result += buffer.data();
                if (result.size() > 8192) break; // cap output
            }
            exit_code = pclose(pipe.release());
        }

        auto end = std::chrono::steady_clock::now();
        double duration = std::chrono::duration<double, std::milli>(end - start).count();

        return {result, exit_code, duration};
    }

    // Execute, compute surprise, and optionally learn operator
    bool probe(const std::string& cmd, const std::string& context,
               const std::string& op_name = "",
               const std::vector<std::string>& postconditions = {}) {

        auto r = exec(cmd);
        
        // Smarter success detection for tools
        bool success = (r.exit_code == 0);
        
        // Heuristic: tools that output version info are alive even if they return non-zero
        if (!success && !r.output.empty()) {
            if (cmd.find("--version") != std::string::npos || cmd.find("-V") != std::string::npos) {
                if (r.output.find("gcc") != std::string::npos || r.output.find("g++") != std::string::npos ||
                    r.output.find("clang") != std::string::npos || r.output.find("cmake") != std::string::npos ||
                    r.output.find("Copyright") != std::string::npos) {
                    success = true;
                }
            }
        }

        size_t output_hash = std::hash<std::string>{}(r.output);
        auto s = surprise_.compute(context, success, output_hash);

        std::string surprise_bar(static_cast<int>(s.surprise * 10), '!');
        std::cout << "[PRIMORDIAL]   " << context
                  << " → " << (success ? "OK" : "FAIL")
                  << " [" << surprise_bar << "] "
                  << r.duration_ms << "ms" << std::endl;

        // Learn operator if successful and named (templatize if possible)
        if (success && !op_name.empty()) {
            auto btmpl = templatize_command(cmd);
            Operator op;
            op.name = op_name;
            op.command_template = btmpl.was_templatized ? btmpl.template_cmd : cmd;
            op.parameters = btmpl.param_names;
            op.preconditions = infer_preconditions(btmpl.param_names);
            op.language = "bash";
            op.postconditions = postconditions;
            op.learned_from = "bootstrap";
            op.record_use(true, r.duration_ms);
            registry_.add(op);
        }

        return success;
    }

    // ─── Phase 1: First Contact ───

    void phase_first_contact() {
        // Can I produce output?
        auto r = exec("echo __ALIVE__");
        if (r.exit_code == 0 && r.output.find("__ALIVE__") != std::string::npos) {
            std::cout << "[PRIMORDIAL]   I can produce predictable output." << std::endl;
        }

        // Who am I?
        r = exec("echo $USER");
        self_.user = trim(r.output);
        std::cout << "[PRIMORDIAL]   I am: " << self_.user << std::endl;

        // Where am I?
        r = exec("echo $HOME");
        self_.home = trim(r.output);
        std::cout << "[PRIMORDIAL]   Home: " << self_.home << std::endl;

        // What is this machine?
        r = exec("hostname");
        self_.hostname = trim(r.output);

        r = exec("uname -s");
        self_.os = trim(r.output);

        r = exec("uname -m");
        self_.arch = trim(r.output);

        std::cout << "[PRIMORDIAL]   Machine: " << self_.hostname
                  << " (" << self_.os << " " << self_.arch << ")" << std::endl;

        // What can I access?
        r = exec("echo $PATH");
        std::string path_str = trim(r.output);
        std::istringstream path_stream(path_str);
        std::string dir;
        while (std::getline(path_stream, dir, ':')) {
            if (!dir.empty()) self_.path_dirs.push_back(dir);
        }
        std::cout << "[PRIMORDIAL]   PATH has " << self_.path_dirs.size() << " directories." << std::endl;

        // Learn basic operators
        Operator echo_op;
        echo_op.name = "echo";
        echo_op.command_template = "echo {text}";
        echo_op.language = "bash";
        echo_op.parameters = {"text"};
        echo_op.postconditions = {"produces_output"};
        echo_op.learned_from = "bootstrap";
        echo_op.record_use(true, r.duration_ms);
        registry_.add(echo_op);
    }

    // ─── Phase 2: Capability Discovery ───

    void phase_capability_discovery() {
        // Scan PATH for available binaries
        int total_binaries = 0;
        for (const auto& dir : self_.path_dirs) {
            try {
                if (!fs::exists(dir)) continue;
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (entry.is_regular_file()) {
                        auto perms = entry.status().permissions();
                        if ((perms & fs::perms::owner_exec) != fs::perms::none) {
                            std::string name = entry.path().filename().string();
                            self_.available_binaries.push_back(name);
                            total_binaries++;
                        }
                    }
                }
            } catch (...) {}
        }

        // Deduplicate
        std::sort(self_.available_binaries.begin(), self_.available_binaries.end());
        self_.available_binaries.erase(
            std::unique(self_.available_binaries.begin(), self_.available_binaries.end()),
            self_.available_binaries.end());

        std::cout << "[PRIMORDIAL]   Found " << self_.available_binaries.size()
                  << " unique executables in PATH." << std::endl;

        // Test which directories are writable
        std::vector<std::string> test_dirs = {"/tmp", self_.home, "/var/tmp"};
        for (const auto& d : test_dirs) {
            auto r = exec("touch " + d + "/.neuroswarm_probe 2>/dev/null && rm " + d + "/.neuroswarm_probe");
            if (r.exit_code == 0) {
                self_.writable_dirs.push_back(d);
                std::cout << "[PRIMORDIAL]   Writable: " << d << std::endl;
            }
        }
    }

    // ─── Phase 3: Sense Acquisition ───

    void phase_sense_acquisition() {
        // Filesystem sense
        probe("ls /", "sense_filesystem", "list_root",
              {"can_see_filesystem"});

        probe("ls " + self_.home, "sense_home", "list_home",
              {"can_see_home"});

        probe("pwd", "sense_location", "get_cwd",
              {"know_current_directory"});

        // Process sense
        probe("ps aux --no-header | head -20", "sense_processes", "list_processes",
              {"can_see_processes"});

        // Hardware sense
        probe("cat /proc/cpuinfo | head -10", "sense_cpu", "read_cpu_info",
              {"know_cpu"});

        probe("cat /proc/meminfo | head -5", "sense_memory", "read_memory_info",
              {"know_memory"});

        // GPU sense
        if (probe("nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null", "sense_gpu")) {
            self_.has_gpu = true;
            std::cout << "[PRIMORDIAL]   DISCOVERY: GPU detected." << std::endl;

            Operator op;
            op.name = "query_gpu";
            op.command_template = "nvidia-smi --query-gpu={query} --format=csv,noheader";
            op.parameters = {"query"};
            op.postconditions = {"gpu_info_available"};
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }

        // Network sense
        if (probe("ip addr show 2>/dev/null | grep 'inet ' | head -5", "sense_network")) {
            self_.has_network = true;
            std::cout << "[PRIMORDIAL]   DISCOVERY: Network interfaces detected." << std::endl;

            Operator op;
            op.name = "list_network";
            op.command_template = "ip addr show";
            op.postconditions = {"network_info_available"};
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }

        // Disk sense
        probe("df -h / | tail -1", "sense_disk", "disk_usage",
              {"know_disk_space"});

        // Time sense
        probe("date +%Y-%m-%dT%H:%M:%S", "sense_time", "get_time",
              {"know_current_time"});
    }

    // ─── Phase 4: Tool Discovery ───

    void phase_tool_discovery() {
        // C++ compiler
        if (probe("g++ --version 2>/dev/null | head -1", "tool_gpp")) {
            self_.can_compile_cpp = true;
            self_.languages.push_back("c++");
            std::cout << "[PRIMORDIAL]   DISCOVERY: C++ compiler available (g++)." << std::endl;

            Operator op;
            op.name = "compile_cpp";
            op.command_template = "g++ -std=c++17 -o {output} {source}";
            op.parameters = {"source", "output"};
            op.preconditions = {"file_exists({source})"};
            op.postconditions = {"binary_exists({output})", "can_create_tool"};
            op.language = "bash";
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        } else if (probe("gcc --version 2>/dev/null | head -1", "tool_gcc")) {
            self_.can_compile_cpp = true;
            self_.languages.push_back("c");
            std::cout << "[PRIMORDIAL]   DISCOVERY: C compiler available (gcc)." << std::endl;
        }

        // Python
        if (probe("python3 --version 2>/dev/null", "tool_python3")) {
            self_.can_run_python = true;
            self_.languages.push_back("python");
            std::cout << "[PRIMORDIAL]   DISCOVERY: Python3 available." << std::endl;

            // Check if pyzmq is available (can generate bus-connected scripts)
            if (probe("python3 -c 'import zmq; print(zmq.zmq_version())' 2>/dev/null", "tool_pyzmq")) {
                std::cout << "[PRIMORDIAL]   DISCOVERY: pyzmq available — can generate bus-connected sensors." << std::endl;

                Operator op;
                op.name = "generate_python_sensor";
                op.command_template = "python3 {script}";
                op.parameters = {"script"};
                op.preconditions = {"file_exists({script})", "pyzmq_available"};
                op.postconditions = {"sensor_running", "bus_connected"};
                op.language = "python";
                op.learned_from = "bootstrap";
                op.record_use(true, 0);
                registry_.add(op);
            }
        }

        // Rust
        if (probe("rustc --version 2>/dev/null", "tool_rustc")) {
            self_.languages.push_back("rust");
            std::cout << "[PRIMORDIAL]   DISCOVERY: Rust compiler available." << std::endl;
        }

        // Node.js
        if (probe("node --version 2>/dev/null", "tool_node")) {
            self_.languages.push_back("javascript");
            std::cout << "[PRIMORDIAL]   DISCOVERY: Node.js available." << std::endl;
        }

        // Git
        if (probe("git --version 2>/dev/null", "tool_git")) {
            std::cout << "[PRIMORDIAL]   DISCOVERY: Git available." << std::endl;

            Operator op;
            op.name = "git_status";
            op.command_template = "git -C {path} status --short";
            op.parameters = {"path"};
            op.postconditions = {"know_git_state"};
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }

        // Docker
        if (probe("docker --version 2>/dev/null", "tool_docker")) {
            std::cout << "[PRIMORDIAL]   DISCOVERY: Docker available." << std::endl;
        }

        // SSH (can reach other machines)
        if (probe("ssh -V 2>&1 | head -1", "tool_ssh")) {
            std::cout << "[PRIMORDIAL]   DISCOVERY: SSH client available — can reach other machines." << std::endl;
        }

        // CMake
        if (probe("cmake --version 2>/dev/null | head -1", "tool_cmake")) {
            std::cout << "[PRIMORDIAL]   DISCOVERY: CMake available." << std::endl;
        }

        // Self-modification operators now registered in ensure_self_modification_operators()
        // (runs regardless of incremental mode, see start())

        std::cout << "[PRIMORDIAL]   Languages available: ";
        for (const auto& l : self_.languages) std::cout << l << " ";
        std::cout << std::endl;
    }

    // ─── Self-Modification Operators (always registered, idempotent) ───
    void ensure_self_modification_operators() {
        // Skip if already registered (idempotent across incremental restarts)
        if (registry_.find("sed_replace") != nullptr) {
            // Even if operators exist, ensure world state has file_exists facts
            // so the Planner can satisfy preconditions
            if (world_state_.find("file_exists(src/)") == world_state_.end()) {
                world_state_.insert("file_exists(src/)");
                world_state_.insert("file_exists(include/)");
                world_state_.insert("source_files_available");
            }
            return;
        }

        // Add file_exists facts so the Planner can chain through preconditions
        world_state_.insert("file_exists(src/)");
        world_state_.insert("file_exists(include/)");
        world_state_.insert("source_files_available");

        // SAFETY: Commands use sed (in-place edit) and cp (backup).
        // NEVER register tee — it truncates files when run via popen (no stdin).
        {
            Operator op;
            op.name = "sed_replace";
            op.command_template = "sed -i 's/{pattern}/{replacement}/' {file}";
            op.parameters = {"pattern", "replacement", "file"};
            op.preconditions = {"file_exists({file})"};
            op.postconditions = {"can_modify_source", "source_modified"};
            op.language = "bash";
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }
        {
            Operator op;
            op.name = "diff_files";
            op.command_template = "diff -u {file_a} {file_b}";
            op.parameters = {"file_a", "file_b"};
            op.preconditions = {"file_exists({file_a})", "file_exists({file_b})"};
            op.postconditions = {"can_compare_files"};
            op.language = "bash";
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }
        {
            Operator op;
            op.name = "copy_file";
            op.command_template = "cp {source} {destination}";
            op.parameters = {"source", "destination"};
            op.preconditions = {"file_exists({source})"};
            op.postconditions = {"can_write_file", "file_backed_up"};
            op.language = "bash";
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }
        {
            Operator op;
            op.name = "rebuild_self";
            op.command_template = "cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) 2>&1 | tail -5";
            op.parameters = {};
            op.preconditions = {"source_modified"};
            op.postconditions = {"can_create_tool", "self_recompiled"};
            op.language = "bash";
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }
        {
            Operator op;
            op.name = "read_source";
            op.command_template = "head -50 {file}";
            op.parameters = {"file"};
            op.preconditions = {"file_exists({file})"};
            op.postconditions = {"can_read_file", "know_source_content"};
            op.language = "bash";
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }
        {
            Operator op;
            op.name = "grep_source";
            op.command_template = "grep -rn '{pattern}' {path}";
            op.parameters = {"pattern", "path"};
            op.preconditions = {};
            op.postconditions = {"can_search_files", "know_source_content"};
            op.language = "bash";
            op.learned_from = "bootstrap";
            op.record_use(true, 0);
            registry_.add(op);
        }
        std::cout << "[PRIMORDIAL]   Self-modification operators ensured (6 ops)." << std::endl;
    }

    // ─── Phase 5: Active Exploration ───

    void phase_active_exploration(int max_binary_probes = 20) {
        // Sync the variation engine with all known fragments
        variation_.sync_fragments(registry_);

        size_t ops_before = registry_.size();
        int candidates_tested = 0;
        int candidates_succeeded = 0;

        // Get stable operators to use as parents (cap at 10 for speed)
        auto stable = registry_.get_stable();

        // Strategy 1: Mutate known operators (cap to keep fast)
        int max_mutate = std::min(static_cast<int>(stable.size()), 10);
        std::cout << "[PRIMORDIAL]   Mutating known operators..." << std::endl;
        for (int i = 0; i < max_mutate; i++) {
            auto mutations = variation_.mutate(*stable[i], 2);
            for (const auto& candidate : mutations) {
                if (test_candidate(candidate)) candidates_succeeded++;
                candidates_tested++;
            }
        }

        // Strategy 2: Recombine pairs of operators
        if (stable.size() >= 2) {
            int max_recomb = std::min(static_cast<int>(stable.size()) - 1, 3);
            for (int i = 0; i < max_recomb; i++) {
                auto crosses = variation_.recombine(*stable[i], *stable[i + 1], 1);
                for (const auto& candidate : crosses) {
                    if (test_candidate(candidate)) candidates_succeeded++;
                    candidates_tested++;
                }
            }
        }

        // Strategy 3: Explore unknown binaries from PATH
        std::cout << "[PRIMORDIAL]   Probing unknown binaries..." << std::endl;
        probe_unknown_binaries(max_binary_probes);

        // Strategy 4: Fragment assembly (pure exploration)
        std::cout << "[PRIMORDIAL]   Assembling from fragments..." << std::endl;
        auto assembled = variation_.assemble_from_fragments(10);
        for (const auto& candidate : assembled) {
            if (test_candidate(candidate)) candidates_succeeded++;
            candidates_tested++;
        }

        std::cout << "[PRIMORDIAL]   Exploration complete: "
                  << candidates_tested << " candidates tested, "
                  << candidates_succeeded << " succeeded, "
                  << (registry_.size() - ops_before) << " new operators learned."
                  << std::endl;
    }

    bool test_candidate(const VariationEngine::Candidate& candidate) {
        // Safety: skip obviously dangerous or degenerate commands
        if (is_dangerous(candidate.command)) return false;
        if (is_degenerate(candidate.command)) return false;

        // Execute in sandbox context (short timeout via timeout command)
        std::string safe_cmd = "timeout 5 bash -c " + shell_escape(candidate.command);
        auto r = exec(safe_cmd);
        bool success = (r.exit_code == 0);

        std::string context = "variation_" + candidate.origin + "_" + candidate.command.substr(0, 30);
        size_t output_hash = std::hash<std::string>{}(r.output);
        auto s = surprise_.compute(context, success, output_hash);

        if (success && s.is_novel && !r.output.empty()) {
            // New successful command that produces novel output — learn it!
            auto tmpl = templatize_command(candidate.command);
            std::string op_name = infer_operator_name(candidate.command);

            // Don't duplicate existing operators — template match
            if (!registry_.find(op_name)) {
                Operator op;
                op.name = op_name;
                op.command_template = tmpl.was_templatized ? tmpl.template_cmd : candidate.command;
                op.parameters = tmpl.param_names;
                op.preconditions = infer_preconditions(tmpl.param_names);
                op.language = "bash";
                op.learned_from = candidate.origin;
                op.record_use(true, r.duration_ms);
                registry_.add(op);

                std::cout << "[PRIMORDIAL]   EVOLVED: \"" << candidate.command << "\"";
                if (tmpl.was_templatized)
                    std::cout << " → TEMPLATE: \"" << tmpl.template_cmd << "\"";
                std::cout << " via " << candidate.origin;
                if (!candidate.parent_a.empty())
                    std::cout << " (from " << candidate.parent_a;
                if (!candidate.parent_b.empty())
                    std::cout << " x " << candidate.parent_b;
                if (!candidate.parent_a.empty())
                    std::cout << ")";
                std::cout << std::endl;

                // Update fragment pool
                variation_.sync_fragments(registry_);
                return true;
            }
        }
        return false;
    }

    void probe_unknown_binaries(int max_probes) {
        // Pick random binaries from PATH that we haven't tried yet
        int probed = 0;
        auto& bins = self_.available_binaries;

        // Prioritise shorter names (core utils tend to be short: ls, cp, mv, df...)
        std::vector<std::string> sorted_bins = bins;
        std::sort(sorted_bins.begin(), sorted_bins.end(),
            [](const std::string& a, const std::string& b) { return a.size() < b.size(); });

        for (const auto& bin : sorted_bins) {
            if (probed >= max_probes) break;
            // Skip if we already have an operator for this
            if (registry_.find(bin)) continue;
            // Skip multi-word or complex names
            if (bin.find('-') != std::string::npos && bin.size() > 10) continue;
            if (bin.find('.') != std::string::npos) continue;
            // Skip GUI applications and interactive programs
            if (is_gui_or_interactive(bin)) continue;

            // Try running with --help (1s timeout to stay fast)
            std::string cmd = bin + " --help";
            auto r = exec("timeout 1 " + cmd + " 2>&1 | head -3");

            if (r.exit_code == 0 || (r.exit_code != 0 && !r.output.empty() && r.output.size() > 10)) {
                std::string context = "binary_probe_" + bin;
                size_t output_hash = std::hash<std::string>{}(r.output);
                surprise_.compute(context, true, output_hash);

                // Try without arguments (1s timeout)
                auto r2 = exec("timeout 1 " + bin + " 2>&1 | head -3");
                if (r2.exit_code == 0 && !r2.output.empty()) {
                    Operator op;
                    op.name = bin;
                    op.command_template = bin;
                    op.language = "bash";
                    op.learned_from = "binary_probe";
                    op.record_use(true, r2.duration_ms);
                    registry_.add(op);
                    probed++;
                }
            }
        }

        std::cout << "[PRIMORDIAL]   Probed " << probed << " new binaries." << std::endl;
    }

    static bool is_gui_or_interactive(const std::string& bin) {
        static const std::vector<std::string> skip = {
            // Editors / IDEs
            "code", "codium", "vim", "nvim", "nano", "emacs", "gedit", "kate",
            "subl", "atom", "micro", "helix",
            // Browsers
            "firefox", "chromium", "chrome", "brave", "vivaldi", "opera",
            "qutebrowser", "surf", "midori", "epiphany",
            // GUI apps
            "gimp", "inkscape", "blender", "vlc", "mpv", "feh", "eog",
            "nautilus", "thunar", "dolphin", "pcmanfm", "nemo",
            "libreoffice", "okular", "evince", "zathura",
            "steam", "discord", "slack", "telegram", "signal",
            "obs", "kdenlive", "audacity", "shotwell",
            // Terminal emulators
            "alacritty", "kitty", "wezterm", "foot", "xterm", "urxvt",
            "gnome", "konsole", "tilix", "terminator",
            // Qt / GTK tools
            "qdoc", "qmake", "designer", "assistant", "linguist", "qdbusviewer",
            "gtk-launch", "gtk3-demo", "gtk4-demo",
            // Window managers / compositors
            "hyprland", "sway", "i3", "dwm", "bspwm", "awesome",
            "waybar", "polybar", "rofi", "dmenu", "wofi", "walker",
            // System UI
            "xdg", "dbus", "systemctl", "journalctl", "loginctl",
            "bluetoothctl", "nmcli", "nmtui", "pavucontrol", "pamixer",
            // Interactive tools
            "python", "python3", "node", "irb", "ghci", "lua", "R",
            "gdb", "lldb", "valgrind",
            "less", "more", "man", "info", "vi",
            "ssh", "telnet", "ftp", "sftp",
            "mysql", "psql", "sqlite3", "redis",
            "htop", "btop", "top", "nmon",
        };
        for (const auto& s : skip) {
            if (bin == s) return true;
        }
        return false;
    }

    // Degenerate commands: trivial, produce no useful learning, flood the genome
    static bool is_degenerate(const std::string& cmd) {
        // Single-word trivial commands
        static const std::vector<std::string> trivial = {
            "whoami", "id", "hostname", "pwd", "uname", "uname -a",
            "date", "uptime", "true", "false", "yes", "no"
        };
        // Trim whitespace for comparison
        std::string trimmed = cmd;
        while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
        while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
        for (const auto& t : trivial) {
            if (trimmed == t) return true;
        }

        // mkdir spam — the #1 degenerate pattern
        if (trimmed.find("mkdir") == 0 && trimmed.find("&&") == std::string::npos) return true;

        // Pure echo with no piping/chaining
        if (trimmed.find("echo ") == 0 && trimmed.find("&&") == std::string::npos
            && trimmed.find("|") == std::string::npos && trimmed.find(">") == std::string::npos) return true;

        // Corrupted fragments starting with flags
        if (!trimmed.empty() && trimmed[0] == '-') return true;

        return false;
    }

    static bool is_dangerous(const std::string& cmd) {
        static const std::vector<std::string> dangerous = {
            "rm ", "rm\t", "rmdir", "mkfs", "dd ", "shred",
            "chmod 777", "chmod -R", "> /dev/", "fork", ":()",
            "shutdown", "reboot", "halt", "init ",
            "mv /", "cp /dev/", "wget ", "curl ",
            "kill ", "killall", "pkill",
        };
        for (const auto& d : dangerous) {
            if (cmd.find(d) != std::string::npos) return true;
        }
        return false;
    }

    // ─── Parametric Operators: generalize concrete commands into templates ───

    struct TemplatizedCommand {
        std::string template_cmd;              // e.g., "cat {path}"
        std::vector<std::string> param_names;  // e.g., ["path"]
        std::vector<std::string> param_values; // e.g., ["README.md"]
        bool was_templatized = false;           // true if any arg was generalized
    };

    static bool is_compound_command(const std::string& cmd) {
        // Don't templatize piped/chained commands — they're already interesting
        return cmd.find('|') != std::string::npos ||
               cmd.find("&&") != std::string::npos ||
               cmd.find(';') != std::string::npos ||
               cmd.find("$(") != std::string::npos ||
               cmd.find('`') != std::string::npos;
    }

    static bool is_path_like(const std::string& token) {
        // Contains directory separator
        if (token.find('/') != std::string::npos) return true;
        // Has common file extension
        static const std::vector<std::string> exts = {
            ".cpp", ".c", ".h", ".hpp", ".py", ".sh", ".txt", ".md", ".json",
            ".jsonl", ".yaml", ".yml", ".xml", ".html", ".css", ".js", ".ts",
            ".log", ".csv", ".tsv", ".conf", ".cfg", ".ini", ".toml", ".lock",
            ".o", ".so", ".a", ".out", ".cmake"
        };
        for (const auto& ext : exts) {
            if (token.size() > ext.size() &&
                token.substr(token.size() - ext.size()) == ext) return true;
        }
        // Known special filenames
        if (token == "Makefile" || token == "Dockerfile" || token == "README" ||
            token == "CMakeLists.txt" || token == "Cargo.toml") return true;
        // Glob patterns with wildcards
        if (token.find('*') != std::string::npos || token.find('?') != std::string::npos) return true;
        return false;
    }

    static TemplatizedCommand templatize_command(const std::string& cmd) {
        TemplatizedCommand result;
        result.template_cmd = cmd;

        // Only templatize simple commands (no pipes, chains, subshells)
        if (is_compound_command(cmd)) return result;

        // Tokenize
        std::vector<std::string> tokens;
        std::istringstream iss(cmd);
        std::string token;
        while (iss >> token) tokens.push_back(token);

        if (tokens.size() < 2) return result;  // single-word commands have nothing to parameterize

        // Find verb index (skip sudo, env, VAR=val prefixes)
        int verb_idx = 0;
        while (verb_idx < (int)tokens.size() &&
               (tokens[verb_idx] == "sudo" || tokens[verb_idx] == "env" ||
                tokens[verb_idx].find('=') != std::string::npos)) {
            verb_idx++;
        }
        if (verb_idx >= (int)tokens.size()) return result;

        // Extract verb for context-aware param naming
        std::string verb = tokens[verb_idx];
        auto slash = verb.rfind('/');
        if (slash != std::string::npos) verb = verb.substr(slash + 1);

        // Build templatized command
        result.template_cmd.clear();
        int path_count = 0;

        for (int i = 0; i < (int)tokens.size(); i++) {
            if (i > 0) result.template_cmd += " ";

            if (i <= verb_idx) {
                // Keep verb and prefixes as-is
                result.template_cmd += tokens[i];
            } else if (!tokens[i].empty() && tokens[i][0] == '-') {
                // Flags stay as-is
                result.template_cmd += tokens[i];
                // If next token is a number after a numeric flag (e.g., -n 20), keep it too
                // This is handled by not matching numbers as paths
            } else if (is_path_like(tokens[i])) {
                // Replace path with named parameter
                std::string pname;
                if (verb == "cp" || verb == "mv" || verb == "install") {
                    pname = (path_count == 0) ? "src" : "dst";
                } else if (verb == "g++" || verb == "gcc" || verb == "cc" || verb == "c++") {
                    pname = (i > 0 && tokens[i-1] == "-o") ? "output" : "source";
                } else if (verb == "grep" || verb == "rg") {
                    pname = "path";  // grep's path arg (pattern handled below)
                } else if (verb == "tee") {
                    pname = "output";
                } else {
                    pname = "path";
                    if (path_count > 0) pname += std::to_string(path_count + 1);
                }
                path_count++;

                result.template_cmd += "{" + pname + "}";
                result.param_names.push_back(pname);
                result.param_values.push_back(tokens[i]);
                result.was_templatized = true;
            } else if ((verb == "grep" || verb == "rg") && path_count == 0 &&
                       result.param_names.empty()) {
                // First non-flag arg to grep is the pattern
                result.template_cmd += "{pattern}";
                result.param_names.push_back("pattern");
                result.param_values.push_back(tokens[i]);
                result.was_templatized = true;
            } else {
                // Keep as-is (numbers, unknown args)
                result.template_cmd += tokens[i];
            }
        }

        return result;
    }

    // Infer preconditions from parameter types
    static std::vector<std::string> infer_preconditions(const std::vector<std::string>& param_names) {
        std::vector<std::string> preconds;
        for (const auto& p : param_names) {
            if (p == "path" || p == "src" || p == "source" || p.substr(0, 4) == "path") {
                preconds.push_back("path_exists({" + p + "})");
            }
        }
        return preconds;
    }

    static std::string infer_operator_name(const std::string& cmd) {
        // Try templatizing first — if successful, name by verb + params
        auto tmpl = templatize_command(cmd);
        if (tmpl.was_templatized) {
            // Extract verb
            std::string verb;
            std::istringstream iss(tmpl.template_cmd);
            std::string token;
            while (iss >> token) {
                if (token == "sudo" || token == "env" || token.find('=') != std::string::npos) continue;
                auto slash = token.rfind('/');
                if (slash != std::string::npos) token = token.substr(slash + 1);
                verb = token;
                break;
            }
            // Name = verb + param names joined by _
            std::string name = verb;
            for (const auto& p : tmpl.param_names) {
                name += "_" + p;
            }
            return name;
        }

        // Fallback for compound/non-templatizable commands: verb + hash
        std::string name;
        for (char c : cmd) {
            if (c == ' ' || c == '\t' || c == '|' || c == ';') break;
            if (c == '/') { name.clear(); continue; }
            name += c;
        }
        size_t hash = std::hash<std::string>{}(cmd) % 10000;
        return name + "_" + std::to_string(hash);
    }

    static std::string shell_escape(const std::string& cmd) {
        std::string escaped = "'";
        for (char c : cmd) {
            if (c == '\'') escaped += "'\\''";
            else escaped += c;
        }
        escaped += "'";
        return escaped;
    }

    // ─── Phase 7: Network Expansion ───

    void phase_network_expansion() {
        // Discover potential hosts
        remote_hosts_ = network_->discover_hosts();

        if (remote_hosts_.empty()) {
            std::cout << "[PRIMORDIAL]   No remote hosts found in SSH config." << std::endl;
            return;
        }

        // Probe for reachability (short timeout per host)
        network_->probe_hosts(remote_hosts_, 3);

        int reachable = 0;
        for (const auto& h : remote_hosts_) {
            if (h.reachable) reachable++;
        }

        if (reachable == 0) {
            std::cout << "[PRIMORDIAL]   No reachable hosts. Network expansion deferred." << std::endl;
            return;
        }

        // Report what we found
        std::cout << "[PRIMORDIAL]   Reachable hosts:" << std::endl;
        for (const auto& h : remote_hosts_) {
            if (!h.reachable) continue;
            std::cout << "[PRIMORDIAL]     " << h.user << "@" << h.address
                      << " (" << h.os << " " << h.arch << ")"
                      << (h.has_kernel ? " [kernel deployed]" : "")
                      << std::endl;
        }

        // Update world state
        world_state_.insert("has_remote_hosts");

        // Learn SSH operator
        Operator ssh_op;
        ssh_op.name = "ssh_exec";
        ssh_op.command_template = "ssh -o BatchMode=yes {user}@{host} '{command}'";
        ssh_op.parameters = {"user", "host", "command"};
        ssh_op.preconditions = {"has_remote_hosts"};
        ssh_op.postconditions = {"remote_execution_available"};
        ssh_op.language = "bash";
        ssh_op.learned_from = "network_expansion";
        ssh_op.record_use(true, 0);
        registry_.add(ssh_op);

        // Learn SCP operator
        Operator scp_op;
        scp_op.name = "scp_copy";
        scp_op.command_template = "scp -o BatchMode=yes {source} {user}@{host}:{dest}";
        scp_op.parameters = {"source", "user", "host", "dest"};
        scp_op.preconditions = {"has_remote_hosts"};
        scp_op.postconditions = {"file_on_remote"};
        scp_op.language = "bash";
        scp_op.learned_from = "network_expansion";
        scp_op.record_use(true, 0);
        registry_.add(scp_op);

        // For hosts with kernel already deployed, sync operators
        for (auto& h : remote_hosts_) {
            if (h.has_kernel) {
                auto remote_model = network_->query_remote(h);
                if (!remote_model.is_null()) {
                    std::cout << "[PRIMORDIAL]   Remote " << h.address << " has "
                              << remote_model.value("operators_learned", 0)
                              << " operators." << std::endl;

                    int imported = network_->sync_operators(h, "data/operators.jsonl");
                    if (imported > 0) {
                        std::cout << "[PRIMORDIAL]   Imported " << imported
                                  << " operators from " << h.address << std::endl;
                    }
                }
            }
        }

        // Broadcast network map
        nlohmann::json net_event = {
            {"origin", "primordial_loop"},
            {"intent", "network_map"},
            {"total_hosts", static_cast<int>(remote_hosts_.size())},
            {"reachable_hosts", reachable}
        };
        routing::publish(pub_, net_event);

        std::cout << "[PRIMORDIAL]   Network expansion complete. "
                  << reachable << " hosts reachable." << std::endl;
    }

    // ─── Phase 6: Planner Self-Test ───

    void phase_planner_selftest() {
        // Seed world state with facts we know from bootstrap
        world_state_.insert("can_see_filesystem");
        world_state_.insert("can_see_home");
        world_state_.insert("know_current_directory");
        world_state_.insert("can_see_processes");
        world_state_.insert("know_cpu");
        world_state_.insert("know_memory");
        world_state_.insert("know_disk_space");
        world_state_.insert("know_current_time");
        world_state_.insert("produces_output");
        if (self_.has_gpu) world_state_.insert("gpu_info_available");
        if (self_.has_network) world_state_.insert("network_info_available");
        if (self_.can_compile_cpp) world_state_.insert("can_create_tool");
        if (self_.can_run_python) world_state_.insert("pyzmq_available");
        world_state_.insert("know_git_state");

        std::cout << "[PRIMORDIAL]   World state: " << world_state_.size() << " known facts." << std::endl;

        // Test 1: Goal already satisfied
        auto r1 = planner_->plan(world_state_, {"can_see_filesystem"});
        std::cout << "[PRIMORDIAL]   Test 1 (already satisfied): "
                  << (r1.success ? "PASS" : "FAIL")
                  << " — " << r1.steps.size() << " steps" << std::endl;

        // Test 2: Goal that requires one operator
        // Remove a fact temporarily and see if planner finds the operator
        WorldState partial = world_state_;
        partial.erase("know_disk_space");
        auto r2 = planner_->plan(partial, {"know_disk_space"});
        std::cout << "[PRIMORDIAL]   Test 2 (one step): "
                  << (r2.success ? "PASS" : "FAIL")
                  << " — " << r2.steps.size() << " steps";
        if (r2.success && !r2.steps.empty()) {
            std::cout << " [" << r2.steps[0].operator_name << "]";
        }
        std::cout << std::endl;

        // Test 3: Goal with chained preconditions
        // "binary_exists(test_tool)" requires "compile_cpp" which requires "file_exists({source})"
        auto r3 = planner_->plan(world_state_, {"binary_exists(test_tool)"});
        std::cout << "[PRIMORDIAL]   Test 3 (multi-step): "
                  << (r3.success ? "PASS" : "FAIL")
                  << " — " << r3.steps.size() << " steps, "
                  << r3.gaps.size() << " gaps, "
                  << r3.nodes_explored << " nodes explored";
        if (!r3.gaps.empty()) {
            std::cout << " [gap: " << r3.gaps[0] << "]";
        }
        std::cout << std::endl;

        // Test 4: Impossible goal (should report gap)
        auto r4 = planner_->plan(world_state_, {"teleport_to_mars"});
        std::cout << "[PRIMORDIAL]   Test 4 (impossible): "
                  << (r4.gaps.size() > 0 ? "PASS" : "FAIL")
                  << " — correctly identified " << r4.gaps.size() << " gap(s)"
                  << std::endl;

        // Execute a plan if we found one
        if (r2.success && !r2.steps.empty()) {
            std::cout << "[PRIMORDIAL]   Executing plan for 'know_disk_space'..." << std::endl;
            execute_plan(r2);
        }
    }

    void execute_plan(const PlanResult& plan) {
        for (const auto& step : plan.steps) {
            std::string cmd = step.command;

            // ── Runtime precondition verification ──
            // Check that concrete preconditions (file/path existence) are still true
            // before executing. Prevents plans based on stale world state.
            auto checks = PreconditionVerifier::verify(step.preconditions);
            bool precond_failed = false;
            for (const auto& check : checks) {
                if (!check.passed) {
                    std::cout << "[PRIMORDIAL]     PRECONDITION FAILED: " << check.precondition
                              << " — " << check.reason << std::endl;
                    // Remove stale fact from world state
                    invalidate_stale_fact(check.precondition);
                    precond_failed = true;
                }
            }
            if (precond_failed) {
                std::cout << "[PRIMORDIAL]     Aborting plan at step: " << step.operator_name
                          << " (precondition violated)" << std::endl;
                break;
            }

            std::cout << "[PRIMORDIAL]     Step: " << step.operator_name
                      << " → " << cmd << std::endl;

            auto r = exec(cmd);
            bool success = (r.exit_code == 0);

            std::cout << "[PRIMORDIAL]     Result: "
                      << (success ? "OK" : "FAIL")
                      << " (" << r.duration_ms << "ms)" << std::endl;

            // Update operator stats
            auto* op = registry_.find(step.operator_name);
            if (op) {
                op->record_use(success, r.duration_ms);
            }

            // Update world state with postconditions
            if (success) {
                for (const auto& post : step.postconditions) {
                    world_state_.insert(post);
                }
                save_world_state();
            } else {
    // Failure feedback: invalidate stale facts + strengthen preconditionss
                handle_plan_step_failure(step, r.output, op);
                std::cout << "[PRIMORDIAL]     Plan execution failed at step: "
                          << step.operator_name << std::endl;
                break;
            }
        }
    }

    // Remove a stale world state fact that matches a failed precondition
    void invalidate_stale_fact(const std::string& precondition) {
        // Direct match
        if (world_state_.erase(precondition) > 0) {
            std::cout << "[PRIMORDIAL]     STALE FACT REMOVED: " << precondition << std::endl;
            save_world_state();
            return;
        }
        // Prefix match: "path_exists(/home/x)" → look for "path_exists(*)" facts
        auto paren = precondition.find('(');
        if (paren != std::string::npos) {
            std::string prefix = precondition.substr(0, paren);
            for (auto it = world_state_.begin(); it != world_state_.end(); ) {
                if (it->find(prefix) == 0 && *it == precondition) {
                    std::cout << "[PRIMORDIAL]     STALE FACT REMOVED: " << *it << std::endl;
                    it = world_state_.erase(it);
                } else {
                    ++it;
                }
            }
            save_world_state();
        }
    }

    // When a plan step fails, analyze the error and strengthen the operator's preconditions
    void handle_plan_step_failure(const PlanStep& step, const std::string& error_output,
                                  Operator* op) {
        std::string failure_type = PreconditionVerifier::classify_failure(error_output);
        if (failure_type.empty()) return;

        // Remove any world state facts that match the failure type
        std::vector<std::string> to_remove;
        for (const auto& fact : world_state_) {
            if (fact.find(failure_type) == 0) {
                // Verify it's actually stale before removing
                if (PreconditionVerifier::is_transient_fact(fact) &&
                    !PreconditionVerifier::reverify_fact(fact)) {
                    to_remove.push_back(fact);
                }
            }
        }
        for (const auto& fact : to_remove) {
            world_state_.erase(fact);
            std::cout << "[PRIMORDIAL]     STALE FACT REMOVED (from error): " << fact << std::endl;
        }
        if (!to_remove.empty()) save_world_state();

        // Strengthen operator preconditions: if the operator doesn't have this type
        // of precondition, add it so future plans won't repeat the same failure
        if (op) {
            std::string needed_pre = failure_type + "({path})";
            bool already_has = false;
            for (const auto& pre : op->preconditions) {
                if (pre.find(failure_type) != std::string::npos) {
                    already_has = true;
                    break;
                }
            }
            if (!already_has) {
                op->preconditions.push_back(needed_pre);
                registry_.save_full();
                std::cout << "[PRIMORDIAL]     PRECONDITION STRENGTHENED: " << step.operator_name
                          << " now requires '" << needed_pre << "'" << std::endl;
            }
        }
    }

    // ── World State Staleness Sweep ──
    // Re-verify all transient facts (file/path existence). Called periodically.
    void sweep_stale_facts() {
        std::vector<std::string> stale;
        for (const auto& fact : world_state_) {
            if (PreconditionVerifier::is_transient_fact(fact)) {
                if (!PreconditionVerifier::reverify_fact(fact)) {
                    stale.push_back(fact);
                }
            }
        }
        for (const auto& fact : stale) {
            world_state_.erase(fact);
            std::cout << "[PRIMORDIAL] STALENESS SWEEP: removed '" << fact << "'" << std::endl;
        }
        if (!stale.empty()) {
            save_world_state();
            std::cout << "[PRIMORDIAL] Staleness sweep: removed " << stale.size()
                      << " stale facts. World state: " << world_state_.size() << " facts." << std::endl;
        }
    }

    // ─── Persistence: world state + self model ───

    static constexpr const char* WORLD_STATE_PATH = "data/world_state.json";
    static constexpr const char* SELF_MODEL_PATH = "data/self_model_primordial.json";

    bool load_persisted_state() {
        bool loaded = false;

        // Load world state
        std::ifstream ws_f(WORLD_STATE_PATH);
        if (ws_f.is_open()) {
            try {
                json ws_doc;
                ws_f >> ws_doc;
                if (ws_doc.contains("facts") && ws_doc["facts"].is_array()) {
                    for (const auto& fact : ws_doc["facts"]) {
                        world_state_.insert(fact.get<std::string>());
                    }
                }
                loaded = true;
            } catch (...) {}
        }

        // Load self model
        std::ifstream sm_f(SELF_MODEL_PATH);
        if (sm_f.is_open()) {
            try {
                json sm_doc;
                sm_f >> sm_doc;
                self_.user = sm_doc.value("user", "");
                self_.home = sm_doc.value("home", "");
                self_.hostname = sm_doc.value("hostname", "");
                self_.os = sm_doc.value("os", "");
                self_.arch = sm_doc.value("arch", "");
                self_.has_network = sm_doc.value("has_network", false);
                self_.has_gpu = sm_doc.value("has_gpu", false);
                self_.can_compile_cpp = sm_doc.value("can_compile_cpp", false);
                self_.can_run_python = sm_doc.value("can_run_python", false);

                if (sm_doc.contains("writable_dirs") && sm_doc["writable_dirs"].is_array()) {
                    self_.writable_dirs.clear();
                    for (const auto& d : sm_doc["writable_dirs"])
                        self_.writable_dirs.push_back(d.get<std::string>());
                }
                if (sm_doc.contains("languages") && sm_doc["languages"].is_array()) {
                    self_.languages.clear();
                    for (const auto& l : sm_doc["languages"])
                        self_.languages.push_back(l.get<std::string>());
                }

                // Rebuild available_binaries list (fast, needed for variation)
                int bin_count = sm_doc.value("available_binaries", 0);
                if (bin_count > 0) {
                    // Re-scan PATH (fast operation, ensures accuracy)
                    self_.path_dirs.clear();
                    self_.available_binaries.clear();
                    std::string path_str;
                    auto r = exec("echo $PATH");
                    path_str = trim(r.output);
                    std::istringstream ps(path_str);
                    std::string dir;
                    while (std::getline(ps, dir, ':')) {
                        if (!dir.empty()) self_.path_dirs.push_back(dir);
                    }

                    for (const auto& d : self_.path_dirs) {
                        try {
                            if (!fs::exists(d)) continue;
                            for (const auto& entry : fs::directory_iterator(d)) {
                                if (entry.is_regular_file()) {
                                    auto perms = entry.status().permissions();
                                    if ((perms & fs::perms::owner_exec) != fs::perms::none) {
                                        self_.available_binaries.push_back(entry.path().filename().string());
                                    }
                                }
                            }
                        } catch (...) {}
                    }
                    std::sort(self_.available_binaries.begin(), self_.available_binaries.end());
                    self_.available_binaries.erase(
                        std::unique(self_.available_binaries.begin(), self_.available_binaries.end()),
                        self_.available_binaries.end());
                }

                loaded = !self_.user.empty();
            } catch (...) {}
        }

        return loaded;
    }

    void save_world_state() {
        json facts = json::array();
        for (const auto& fact : world_state_) {
            facts.push_back(fact);
        }
        json doc = {{"facts", facts}, {"count", static_cast<int>(world_state_.size())}};
        std::ofstream f(WORLD_STATE_PATH);
        if (f.is_open()) f << doc.dump(2);
    }

    // ─── Self Model Report ───

    void report_self_model() {
        json model = {
            {"user", self_.user},
            {"home", self_.home},
            {"hostname", self_.hostname},
            {"os", self_.os},
            {"arch", self_.arch},
            {"path_dirs", static_cast<int>(self_.path_dirs.size())},
            {"available_binaries", static_cast<int>(self_.available_binaries.size())},
            {"writable_dirs", self_.writable_dirs},
            {"languages", self_.languages},
            {"has_network", self_.has_network},
            {"has_gpu", self_.has_gpu},
            {"can_compile_cpp", self_.can_compile_cpp},
            {"can_run_python", self_.can_run_python},
            {"operators_learned", static_cast<int>(registry_.size())},
            {"global_surprise", surprise_.global_surprise()},
            {"contexts_known", static_cast<int>(surprise_.contexts_known())},
            {"llm_calls", oracle_ ? oracle_->total_calls() : 0},
            {"llm_successes", oracle_ ? oracle_->total_successes() : 0},
            {"llm_dependency", oracle_ ? oracle_->dependency_ratio() : 0.0}
        };

        // Save to disk
        std::ofstream f("data/self_model_primordial.json");
        f << model.dump(2) << std::endl;

        // Broadcast on bus
        json msg = {
            {"origin", "primordial_loop"},
            {"intent", "self_model_bootstrap"},
            {"model", model}
        };
        routing::publish(pub_, msg);

        std::cout << "\n[PRIMORDIAL] ═══ SELF MODEL ═══" << std::endl;
        std::cout << "[PRIMORDIAL]   Identity:    " << self_.user << "@" << self_.hostname << std::endl;
        std::cout << "[PRIMORDIAL]   Platform:    " << self_.os << " " << self_.arch << std::endl;
        std::cout << "[PRIMORDIAL]   Binaries:    " << self_.available_binaries.size() << std::endl;
        std::cout << "[PRIMORDIAL]   Languages:   " << self_.languages.size() << std::endl;
        std::cout << "[PRIMORDIAL]   GPU:         " << (self_.has_gpu ? "yes" : "no") << std::endl;
        std::cout << "[PRIMORDIAL]   Network:     " << (self_.has_network ? "yes" : "no") << std::endl;
        std::cout << "[PRIMORDIAL]   Operators:   " << registry_.size() << std::endl;
        std::cout << "[PRIMORDIAL]   Surprise:    " << surprise_.global_surprise() << std::endl;
        std::cout << "[PRIMORDIAL] ═══════════════════" << std::endl;
    }

    // ─── Postcondition Inference ───
    // Scan all operators and infer postconditions from command patterns.
    // This makes the Planner useful — without postconditions, operators are invisible to it.

    void enrich_operator_postconditions() {
        struct PatternRule {
            std::string pattern;       // substring to match in command_template
            std::string postcondition; // postcondition to add
        };

        static const std::vector<PatternRule> rules = {
            // File operations
            {"cat ",        "can_read_file"},
            {"head ",       "can_read_file"},
            {"tail ",       "can_read_file"},
            {"less ",       "can_read_file"},
            {"stat ",       "can_read_file"},
            {"wc ",         "can_read_file"},
            {"file ",       "can_read_file"},
            {"echo ",       "can_write_file"},
            {"tee ",        "can_write_file"},
            {"touch ",      "can_write_file"},
            {"cp ",         "can_write_file"},
            {"mv ",         "can_write_file"},
            {"mkdir ",      "can_write_file"},
            // Search
            {"find ",       "can_search_files"},
            {"grep ",       "can_search_files"},
            {"locate ",     "can_search_files"},
            {"rg ",         "can_search_files"},
            // Process
            {"ps ",         "can_see_processes"},
            {"pgrep",       "can_see_processes"},
            {"top ",        "can_see_processes"},
            // Network
            {"ss ",         "network_info_available"},
            {"netstat",     "network_info_available"},
            {"ip addr",     "network_info_available"},
            {"ping ",       "network_info_available"},
            {"curl ",       "network_info_available"},
            // System
            {"uptime",      "know_system_state"},
            {"free ",       "know_system_state"},
            {"df ",         "know_disk_space"},
            {"uname",       "know_system_state"},
            {"lscpu",       "know_cpu"},
            // Git
            {"git ",        "know_git_state"},
            // Compilation
            {"g++ ",        "can_create_tool"},
            {"gcc ",        "can_create_tool"},
            {"cmake ",      "can_create_tool"},
            {"make ",       "can_create_tool"},
            // Data
            {"jq ",         "can_analyse_data"},
            {"sort ",       "can_analyse_data"},
            {"awk ",        "can_analyse_data"},
            {"python3 ",    "can_run_script"},
            {"bash ",       "can_run_script"},
            // Self
            {"data/self_model",  "know_self_state"},
            {"data/engrams",     "can_analyse_memory"},
            {"data/metrics",     "can_analyse_logs"},
            {"progress.txt",     "can_analyse_logs"},
        };

        int enriched = 0;
        // Iterate all operators via get_stable + find
        auto stable = registry_.get_stable();
        for (auto* op : stable) {
            if (op->postconditions.empty()) {
                for (const auto& rule : rules) {
                    if (op->command_template.find(rule.pattern) != std::string::npos) {
                        op->postconditions.push_back(rule.postcondition);
                        // Also add to world state since we know this command works
                        world_state_.insert(rule.postcondition);
                        enriched++;
                        break; // one postcondition per operator is enough
                    }
                }
            }
        }

        if (enriched > 0) {
            registry_.save_full();
            save_world_state();
            std::cout << "[PRIMORDIAL] Enriched " << enriched
                      << " operators with inferred postconditions. World state: "
                      << world_state_.size() << " facts." << std::endl;
        }
    }

    // ─── Service Loop ───

    void service_loop() {
        std::cout << "[PRIMORDIAL] Entering service loop — ready for operator requests." << std::endl;

        while (true) {
            auto j = routing::receive(sub_, zmq::recv_flags::dontwait);
            if (!j.is_null()) {
                std::string intent = j.value("intent", "");

                if (intent == "operator_request") {
                    handle_operator_request(j);
                } else if (intent == "probe_request") {
                    handle_probe_request(j);
                } else if (intent == "goal_request") {
                    handle_goal_request(j);
                } else if (intent == "domain_resolve_request") {
                    handle_domain_resolve(j);
                } else if (intent == "execution_result") {
                    learn_from_execution(j);
                } else if (intent == "concept_update") {
                    handle_concept_update(j);
                } else if (intent == "precondition_discovery") {
                    handle_precondition_discovery(j);
                }
            }

            // Periodic maintenance
            static int tick = 0;
            if (++tick % 600 == 0) {  // every ~60 seconds
                int pruned = registry_.prune();
                if (pruned > 0) {
                    std::cout << "[PRIMORDIAL] Pruned " << pruned << " dead operators." << std::endl;
                }
                // Re-verify transient facts (file_exists, path_exists) and remove stale ones
                sweep_stale_facts();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // ─── Runtime Learning: learn from every successful execution ───

    void learn_from_execution(const json& j) {
        // Only learn from motor_cortex successes
        if (j.value("origin", "") != "motor_cortex") return;
        if (j.value("status", "") != "success") return;
        // Skip dream mode — those are sandbox tests
        if (j.value("mode", "") == "dream") return;

        std::string cmd = j.value("command", "");
        if (cmd.empty() || cmd.size() > 200) return;

        // Skip if dangerous or degenerate (trivial commands pollute the genome)
        if (is_dangerous(cmd)) return;
        if (is_degenerate(cmd)) return;

        // Templatize command: generalize concrete args into parameters
        auto tmpl = templatize_command(cmd);
        std::string op_name = infer_operator_name(cmd);

        // Skip if we already know this operator (template match)
        if (registry_.find(op_name)) {
            auto* existing = registry_.find(op_name);
            existing->record_use(true, 0);
            return;
        }

        // Learn as new operator — with template if possible
        Operator op;
        op.name = op_name;
        op.command_template = tmpl.was_templatized ? tmpl.template_cmd : cmd;
        op.parameters = tmpl.param_names;
        op.preconditions = infer_preconditions(tmpl.param_names);
        op.language = "bash";
        op.learned_from = "runtime";

        // Infer postcondition: try concept space first, then regex patterns
        std::string post = infer_postcondition_from_concepts(cmd);
        if (post.empty()) post = infer_postcondition(cmd);
        if (!post.empty()) {
            op.postconditions.push_back(post);
            world_state_.insert(post);
        }

        op.record_use(true, 0);
        registry_.add(op);

        // Compute surprise
        size_t output_hash = std::hash<std::string>{}(j.value("proprioception", ""));
        auto s = surprise_.compute("runtime_" + op_name, true, output_hash);

        if (s.is_novel) {
            std::cout << "[PRIMORDIAL] RUNTIME LEARNED: \"" << cmd.substr(0, 60) << "\"";
            if (tmpl.was_templatized)
                std::cout << " → TEMPLATE: \"" << tmpl.template_cmd << "\" params=" << tmpl.param_names.size();
            if (!post.empty())
                std::cout << " → " << post;
            std::cout << std::endl;
            variation_.sync_fragments(registry_);
        }
    }

    // --- Concept space integration ---

    // Cached concept abstractions from ConceptLobe
    struct ConceptAbstraction {
        std::string name;      // e.g., "read_content"
        std::string pattern;   // e.g., "cat <path>"
    };
    std::vector<ConceptAbstraction> concept_abstractions;

    void handle_concept_update(const json& j) {
        auto clusters = j.value("clusters", json::array());
        concept_abstractions.clear();
        for (auto& c : clusters) {
            ConceptAbstraction ca;
            ca.name = c.value("concept", "");
            ca.pattern = c.value("pattern", "");
            if (!ca.name.empty() && c.value("members", 0) >= 3) {
                concept_abstractions.push_back(ca);
            }
        }
        if (!concept_abstractions.empty()) {
            std::cout << "[PRIMORDIAL] Concept space: " << concept_abstractions.size()
                      << " abstractions available for postcondition enrichment" << std::endl;
        }
    }

    // ─── Precondition Discovery: MetaCognition teaches the Planner ───
    //
    // When MetaCognition observes "domain X fails because of error Y",
    // it discovers that operators in domain X need capability Y as a precondition.
    // We translate this into concrete operator preconditions so the Planner
    // can chain operators: "to read a file, first verify the path exists".

    // Map MetaCognition capability names to operator-level preconditions
    static std::string capability_to_precondition(const std::string& capability) {
        static const std::map<std::string, std::string> mapping = {
            {"filesystem_navigation", "path_exists({path})"},
            {"access_control",        "has_permissions({path})"},
            {"tool_discovery",        "tool_in_path"},
            {"path_type_awareness",   "path_type_known({path})"},
            {"build_dependencies",    "dependencies_met"},
            {"dependency_resolution", "dependencies_met"},
            {"resource_discovery",    "resource_located"},
            {"state_awareness",       "state_current"},
            {"debugging",             "code_validated"},
            {"language_syntax",       "syntax_valid"},
        };
        auto it = mapping.find(capability);
        return (it != mapping.end()) ? it->second : "";
    }

    // domain_to_postcondition is defined below (shared with domain_resolve)

    void handle_precondition_discovery(const json& j) {
        std::string domain = j.value("domain", "");
        std::string required_cap = j.value("required_capability", "");
        int evidence = j.value("evidence_count", 1);

        if (domain.empty() || required_cap.empty()) return;

        // Only act on discoveries with enough evidence (≥3 observations)
        if (evidence < 3) return;

        // Translate to operator-level precondition
        std::string precondition = capability_to_precondition(required_cap);
        if (precondition.empty()) return;

        // Find operators that serve this domain (by postcondition match)
        std::string domain_post = domain_to_postcondition(domain);
        if (domain_post.empty()) return;

        auto operators = registry_.find_by_postcondition(domain_post);
        int enriched = 0;

        for (auto* op : operators) {
            // Check if this precondition already exists
            bool already_has = false;
            for (const auto& pre : op->preconditions) {
                if (pre.find(precondition.substr(0, precondition.find('('))) != std::string::npos) {
                    already_has = true;
                    break;
                }
            }
            if (!already_has) {
                op->preconditions.push_back(precondition);
                enriched++;
            }
        }

        if (enriched > 0) {
            registry_.save_full();
            std::cout << "[PRIMORDIAL] PRECONDITION ENRICHMENT: " << enriched << " operators in '"
                      << domain << "' now require '" << precondition
                      << "' (evidence: " << evidence << "x)" << std::endl;

            // Add the capability to world state if we can satisfy it
            // This makes the Planner aware of what we can provide
            world_state_.insert(precondition);
        }
    }

    // Try concept-based postcondition before falling back to regex
    std::string infer_postcondition_from_concepts(const std::string& cmd) {
        if (concept_abstractions.empty()) return "";

        // Extract verb from command
        std::string verb;
        std::istringstream iss(cmd);
        std::string token;
        while (iss >> token) {
            if (token.find('=') != std::string::npos) continue;
            if (token == "sudo" || token == "env") continue;
            auto slash = token.rfind('/');
            if (slash != std::string::npos) token = token.substr(slash + 1);
            verb = token;
            break;
        }

        // Check if any concept pattern starts with this verb
        for (auto& ca : concept_abstractions) {
            std::string pattern_verb;
            std::istringstream piss(ca.pattern);
            piss >> pattern_verb;
            if (pattern_verb == verb) {
                // Convert concept name to postcondition format: "can_" + abstraction
                std::string post = "can_" + ca.name;
                return post;
            }
        }
        return "";
    }

    // Infer a postcondition from a command string using pattern rules
    static std::string infer_postcondition(const std::string& cmd) {
        static const std::vector<std::pair<std::string, std::string>> rules = {
            {"cat ",    "can_read_file"},   {"head ",   "can_read_file"},
            {"tail ",   "can_read_file"},   {"wc ",     "can_read_file"},
            {"stat ",   "can_read_file"},   {"file ",   "can_read_file"},
            {"tee ",    "can_write_file"},
            {"cp ",     "can_write_file"},
            {"mv ",     "can_write_file"},
            {"find ",   "can_search_files"},{"grep ",   "can_search_files"},
            {"rg ",     "can_search_files"},
            {"ps ",     "can_see_processes"},{"pgrep",  "can_see_processes"},
            {"ss ",     "network_info_available"},{"netstat","network_info_available"},
            {"curl ",   "network_info_available"},
            {"uptime",  "know_system_state"},{"free ",  "know_system_state"},
            {"df ",     "know_disk_space"},  {"uname",  "know_system_state"},
            {"git ",    "know_git_state"},
            {"g++ ",    "can_create_tool"},  {"gcc ",   "can_create_tool"},
            {"cmake ",  "can_create_tool"},  {"make ",  "can_create_tool"},
            {"python3 ","can_run_script"},   {"bash ",  "can_run_script"},
        };
        for (const auto& [pattern, post] : rules) {
            if (cmd.find(pattern) != std::string::npos) return post;
        }
        return "";
    }

    void handle_operator_request(const json& j) {
        std::string name = j.value("operator_name", "");
        std::string cid = j.value("cid", "unknown");

        auto* op = registry_.find(name);
        if (op) {
            json resp = {
                {"origin", "primordial_loop"},
                {"intent", "operator_response"},
                {"cid", cid},
                {"found", true},
                {"operator", op->to_json()}
            };
            routing::publish(pub_, resp);
        } else {
            json resp = {
                {"origin", "primordial_loop"},
                {"intent", "operator_response"},
                {"cid", cid},
                {"found", false},
                {"operator_name", name}
            };
            routing::publish(pub_, resp);
        }
    }

    void handle_probe_request(const json& j) {
        std::string cmd = j.value("command", "");
        std::string context = j.value("context", "external_probe");
        std::string op_name = j.value("operator_name", "");
        std::string cid = j.value("cid", "unknown");

        if (cmd.empty()) return;

        auto r = exec(cmd);
        bool success = (r.exit_code == 0);

        size_t output_hash = std::hash<std::string>{}(r.output);
        auto s = surprise_.compute(context, success, output_hash);

        // Learn if successful and named (templatize)
        if (success && !op_name.empty() && !registry_.find(op_name)) {
            auto ptmpl = templatize_command(cmd);
            Operator op;
            op.name = op_name;
            op.command_template = ptmpl.was_templatized ? ptmpl.template_cmd : cmd;
            op.parameters = ptmpl.param_names;
            op.preconditions = infer_preconditions(ptmpl.param_names);
            op.language = "bash";
            op.learned_from = "external_probe";
            op.record_use(true, r.duration_ms);
            registry_.add(op);
        }

        json resp = {
            {"origin", "primordial_loop"},
            {"intent", "probe_result"},
            {"cid", cid},
            {"command", cmd},
            {"output", r.output.substr(0, 2048)},
            {"exit_code", r.exit_code},
            {"duration_ms", r.duration_ms},
            {"surprise", s.surprise},
            {"is_novel", s.is_novel},
            {"operators_known", static_cast<int>(registry_.size())}
        };
        routing::publish(pub_, resp);
    }

    void handle_goal_request(const json& j) {
        std::string cid = j.value("cid", "unknown");
        auto goals = j.value("goals", std::vector<std::string>{});

        if (goals.empty()) {
            // Single goal format
            std::string goal = j.value("goal", "");
            if (!goal.empty()) goals.push_back(goal);
        }

        if (goals.empty()) return;

        std::cout << "[PRIMORDIAL] Goal request [" << cid << "]: ";
        for (const auto& g : goals) std::cout << g << " ";
        std::cout << std::endl;

        auto result = planner_->plan(world_state_, goals);

        json resp = {
            {"origin", "primordial_loop"},
            {"intent", "goal_plan"},
            {"cid", cid},
            {"success", result.success},
            {"steps_count", static_cast<int>(result.steps.size())},
            {"gaps", result.gaps},
            {"nodes_explored", result.nodes_explored}
        };

        // Include plan steps
        json steps_json = json::array();
        for (const auto& step : result.steps) {
            steps_json.push_back({
                {"operator_id", step.operator_id},
                {"operator_name", step.operator_name},
                {"command", step.command},
                {"preconditions", step.preconditions},
                {"postconditions", step.postconditions}
            });
        }
        resp["steps"] = steps_json;

        if (result.success) {
            std::cout << "[PRIMORDIAL] Plan found: " << result.steps.size()
                      << " steps, " << result.nodes_explored << " nodes explored." << std::endl;

            // Auto-execute if requested
            if (j.value("auto_execute", false)) {
                std::cout << "[PRIMORDIAL] Auto-executing plan..." << std::endl;
                execute_plan(result);
                resp["executed"] = true;
            }
        } else {
            std::cout << "[PRIMORDIAL] Plan FAILED: " << result.failure_reason << std::endl;

            // Attempt to fill gaps: variation first, then LLM as last resort
            if (j.value("auto_generate", false) && !result.gaps.empty()) {
                std::vector<std::string> remaining_gaps;

                // Stage 1: Try variation (no LLM cost)
                std::cout << "[PRIMORDIAL] Stage 1: Filling gaps via variation..." << std::endl;
                variation_.sync_fragments(registry_);

                for (const auto& gap : result.gaps) {
                    bool filled = false;
                    auto candidates = variation_.targeted_variation(gap, registry_, 10);
                    for (const auto& c : candidates) {
                        if (test_candidate(c)) {
                            std::cout << "[PRIMORDIAL] Gap filled (variation): " << gap << std::endl;
                            filled = true;
                            break;
                        }
                    }
                    if (!filled) remaining_gaps.push_back(gap);
                }

                // Stage 2: LLM oracle for remaining gaps
                if (!remaining_gaps.empty() && oracle_) {
                    std::cout << "[PRIMORDIAL] Stage 2: Consulting LLM oracle for "
                              << remaining_gaps.size() << " remaining gap(s)..." << std::endl;

                    // Build environment summary for context
                    std::string env = self_.os + " " + self_.arch + ", user=" + self_.user;
                    if (self_.can_compile_cpp) env += ", g++ available";
                    if (self_.can_run_python) env += ", python3+pyzmq available";
                    if (self_.has_gpu) env += ", GPU available";
                    if (self_.has_network) env += ", network available";

                    // Collect known operator names for context
                    std::vector<std::string> known_op_names;
                    auto stable_ops = registry_.get_stable();
                    for (auto* op : stable_ops) {
                        known_op_names.push_back(op->name + "=" + op->command_template);
                    }

                    for (const auto& gap : remaining_gaps) {
                        std::vector<std::string> failed_attempts;

                        auto oracle_result = oracle_->generate_operator(
                            gap, known_op_names, failed_attempts, env);

                        if (oracle_result.success) {
                            // Test the LLM-generated command in sandbox
                            std::string safe_cmd = "timeout 5 bash -c "
                                + shell_escape(oracle_result.command);
                            auto r = exec(safe_cmd);

                            if (r.exit_code == 0) {
                                // SUCCESS — promote to operator (templatized)
                                auto otmpl = templatize_command(oracle_result.command);
                                Operator new_op;
                                new_op.name = infer_operator_name(oracle_result.command);
                                new_op.command_template = otmpl.was_templatized ? otmpl.template_cmd : oracle_result.command;
                                new_op.parameters = otmpl.param_names;
                                new_op.preconditions = infer_preconditions(otmpl.param_names);
                                new_op.language = oracle_result.language;
                                new_op.postconditions = {gap};
                                new_op.learned_from = "llm_oracle";
                                new_op.record_use(true, r.duration_ms);
                                registry_.add(new_op);

                                // Update fragment pool
                                variation_.sync_fragments(registry_);

                                std::cout << "[PRIMORDIAL] Gap filled (LLM): " << gap
                                          << " → " << oracle_result.command << std::endl;
                            } else {
                                std::cout << "[PRIMORDIAL] LLM suggestion failed sandbox test: "
                                          << oracle_result.command << std::endl;
                            }
                        } else {
                            std::cout << "[PRIMORDIAL] LLM oracle could not generate for: "
                                      << gap << std::endl;
                        }
                    }
                }

                // Retry planning after gap-filling
                auto retry = planner_->plan(world_state_, goals);
                if (retry.success) {
                    std::cout << "[PRIMORDIAL] Re-plan succeeded after gap-filling!" << std::endl;
                    resp["success"] = true;
                    resp["gaps"] = json::array();
                    resp["generation_method"] = "variation+llm";
                    resp["llm_calls"] = oracle_ ? oracle_->total_calls() : 0;
                    steps_json.clear();
                    for (const auto& step : retry.steps) {
                        steps_json.push_back({
                            {"operator_id", step.operator_id},
                            {"operator_name", step.operator_name},
                            {"command", step.command},
                            {"preconditions", step.preconditions},
                            {"postconditions", step.postconditions}
                        });
                    }
                    resp["steps"] = steps_json;

                    if (j.value("auto_execute", false)) {
                        execute_plan(retry);
                        resp["executed"] = true;
                    }
                }
            }
        }

        routing::publish(pub_, resp);
    }

    // ─── Dream Sandbox: test candidates via MotorLobe isolation ───

    // Send a command to MotorLobe in dream mode and wait for result.
    // Used during runtime (domain_resolve) for proper sandboxing.
    // Bootstrap still uses inline exec() for speed.
    struct DreamResult {
        bool success;
        std::string output;
        int exit_code;
    };

    DreamResult dream_test(const std::string& cmd, int timeout_ms = 5000) {
        std::string cid = "dream_" + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());

        json req = {
            {"cid", cid},
            {"origin", "primordial_loop"},
            {"intent", "execution_request"},
            {"command", cmd},
            {"mode", "dream"}
        };
        routing::publish(pub_, req);

        // Wait for execution_result with matching CID
        auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_ms);

        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = routing::receive(sub_, zmq::recv_flags::dontwait);
            if (!resp.is_null()) {
                if (resp.value("intent", "") == "execution_result" &&
                    resp.value("cid", "") == cid) {
                    return {
                        resp.value("status", "") == "success",
                        resp.value("proprioception", ""),
                        resp.value("exit_code", -1)
                    };
                }
                // Re-queue other messages? No — they'll be handled next loop.
                // For domain_resolve we accept some message loss.
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        return {false, "timeout", -1};
    }

    // Test a candidate via dream sandbox (runtime) or inline (bootstrap)
    bool test_candidate_sandboxed(const VariationEngine::Candidate& candidate) {
        if (is_dangerous(candidate.command)) return false;
        if (is_degenerate(candidate.command)) return false;
        // Extract first token (binary name) and skip GUI/interactive programs
        std::string bin = candidate.command.substr(0, candidate.command.find(' '));
        if (is_gui_or_interactive(bin)) return false;

        auto dr = dream_test(candidate.command);

        std::string context = "dream_" + candidate.origin + "_" + candidate.command.substr(0, 30);
        size_t output_hash = std::hash<std::string>{}(dr.output);
        auto s = surprise_.compute(context, dr.success, output_hash);

        if (dr.success && s.is_novel && !dr.output.empty()) {
            auto tmpl = templatize_command(candidate.command);
            std::string op_name = infer_operator_name(candidate.command);
            if (!registry_.find(op_name)) {
                Operator op;
                op.name = op_name;
                op.command_template = tmpl.was_templatized ? tmpl.template_cmd : candidate.command;
                op.parameters = tmpl.param_names;
                op.preconditions = infer_preconditions(tmpl.param_names);
                op.language = "bash";
                op.learned_from = "dream_" + candidate.origin;
                op.record_use(true, 0);
                registry_.add(op);
                variation_.sync_fragments(registry_);

                std::cout << "[PRIMORDIAL]   DREAM EVOLVED: \"" << candidate.command << "\"";
                if (tmpl.was_templatized)
                    std::cout << " → TEMPLATE: \"" << tmpl.template_cmd << "\"";
                std::cout << " via " << candidate.origin << std::endl;
                return true;
            }
        }
        return false;
    }

    // ─── Domain Resolution: Planner + Variation + LLM for chronic failures ───

    void handle_domain_resolve(const json& j) {
        std::string domain = j.value("domain", "");
        std::string cid = j.value("cid", "unknown");
        auto failed_commands = j.value("failed_commands", std::vector<std::string>{});

        if (domain.empty()) return;

        std::cout << "[PRIMORDIAL] Domain resolve request: " << domain
                  << " (" << failed_commands.size() << " failed commands)" << std::endl;

        int new_operators = 0;
        int candidates_tested = 0;
        int candidates_succeeded = 0;

        // Stage 1: Targeted variation — mutate/recombine operators related to this domain
        std::cout << "[PRIMORDIAL]   Stage 1: Targeted variation for '" << domain << "'..." << std::endl;
        variation_.sync_fragments(registry_);

        // Map domain to postcondition patterns for targeted search
        std::string target_postcondition = domain_to_postcondition(domain);

        auto candidates = variation_.targeted_variation(target_postcondition, registry_, 5);
        for (const auto& c : candidates) {
            candidates_tested++;
            if (test_candidate_sandboxed(c)) {
                candidates_succeeded++;
                new_operators++;
            }
        }

        // Stage 2: Mutate the failed commands directly — they were close to working
        std::cout << "[PRIMORDIAL]   Stage 2: Mutating failed commands..." << std::endl;
        for (const auto& failed_cmd : failed_commands) {
            // Skip degenerate base commands — mutating mkdir yields more mkdir
            if (is_degenerate(failed_cmd)) continue;
            // Create a temporary operator from the failed command to mutate it
            Operator temp_op;
            temp_op.name = "failed_" + domain;
            temp_op.command_template = failed_cmd;
            temp_op.language = "bash";
            // Decompose into fragments
            std::istringstream iss(failed_cmd);
            std::string frag;
            while (iss >> frag) temp_op.fragments.push_back(frag);

            auto mutations = variation_.mutate(temp_op, 3);
            for (const auto& m : mutations) {
                candidates_tested++;
                if (test_candidate_sandboxed(m)) {
                    candidates_succeeded++;
                    new_operators++;
                }
            }
        }

        // Stage 3: Planner — check if any goal related to this domain can now be planned
        std::cout << "[PRIMORDIAL]   Stage 3: Planner search for '" << target_postcondition << "'..." << std::endl;
        bool plan_found = false;
        if (planner_) {
            auto plan = planner_->plan(world_state_, {target_postcondition});
            if (plan.success) {
                plan_found = true;
                std::cout << "[PRIMORDIAL]   Plan found: " << plan.steps.size() << " steps." << std::endl;
                // Execute the plan to prove it works
                execute_plan(plan);
            }
        }

        // Stage 4: LLM oracle as last resort (only if variation failed)
        bool llm_used = false;
        if (new_operators == 0 && !plan_found && oracle_) {
            std::cout << "[PRIMORDIAL]   Stage 4: LLM oracle for domain '" << domain << "'..." << std::endl;
            llm_used = true;

            std::string env = self_.os + " " + self_.arch + ", user=" + self_.user;
            if (self_.can_compile_cpp) env += ", g++ available";
            if (self_.can_run_python) env += ", python3+pyzmq available";

            std::vector<std::string> known_ops;
            for (auto* op : registry_.get_stable()) {
                known_ops.push_back(op->name + "=" + op->command_template);
            }

            auto result = oracle_->generate_operator(
                target_postcondition, known_ops, failed_commands, env);

            if (result.success) {
                std::string safe_cmd = "timeout 5 bash -c " + shell_escape(result.command);
                auto r = exec(safe_cmd);
                if (r.exit_code == 0) {
                    auto dtmpl = templatize_command(result.command);
                    Operator new_op;
                    new_op.name = infer_operator_name(result.command);
                    new_op.command_template = dtmpl.was_templatized ? dtmpl.template_cmd : result.command;
                    new_op.parameters = dtmpl.param_names;
                    new_op.preconditions = infer_preconditions(dtmpl.param_names);
                    new_op.language = result.language;
                    new_op.postconditions = {target_postcondition};
                    new_op.learned_from = "llm_oracle_domain_resolve";
                    new_op.record_use(true, r.duration_ms);
                    registry_.add(new_op);
                    variation_.sync_fragments(registry_);
                    new_operators++;
                    std::cout << "[PRIMORDIAL]   LLM generated: " << result.command << std::endl;
                }
            }
        }

        // Report result
        bool resolved = (new_operators > 0 || plan_found);
        std::string method = plan_found ? "planner" :
                            (llm_used && new_operators > 0) ? "llm_oracle" :
                            (new_operators > 0) ? "variation" : "failed";

        json resp = {
            {"origin", "primordial_loop"},
            {"intent", "domain_resolve_result"},
            {"cid", cid},
            {"domain", domain},
            {"resolved", resolved},
            {"method", method},
            {"new_operators", new_operators},
            {"candidates_tested", candidates_tested},
            {"candidates_succeeded", candidates_succeeded},
            {"operators_total", static_cast<int>(registry_.size())},
            {"llm_calls", oracle_ ? oracle_->total_calls() : 0}
        };
        routing::publish(pub_, resp);

        std::cout << "[PRIMORDIAL] Domain resolve " << (resolved ? "SUCCEEDED" : "FAILED")
                  << ": domain='" << domain << "' method=" << method
                  << " new_ops=" << new_operators << std::endl;
    }

    // Map domain name to a postcondition pattern for the planner
    static std::string domain_to_postcondition(const std::string& domain) {
        static const std::map<std::string, std::string> mapping = {
            {"file_write",          "can_write_file"},
            {"file_read",           "can_read_file"},
            {"file_search",         "can_search_files"},
            {"search",              "can_search_files"},
            {"process_inspection",  "can_see_processes"},
            {"process_mgmt",        "can_see_processes"},
            {"network_diagnostics", "network_info_available"},
            {"network",             "network_info_available"},
            {"source_modification", "can_modify_source"},
            {"compilation",         "can_create_tool"},
            {"git_operations",      "know_git_state"},
            {"git",                 "know_git_state"},
            {"system_monitoring",   "know_system_state"},
            {"data_analysis",       "can_analyse_data"},
            {"script_creation",     "can_create_script"},
            {"scripting",           "can_run_script"},
            {"self_inspection",     "know_self_state"},
            {"memory_analysis",     "can_analyse_memory"},
            {"log_analysis",        "can_analyse_logs"},
            {"disk",                "know_disk_space"},
        };
        auto it = mapping.find(domain);
        return it != mapping.end() ? it->second : domain;
    }

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }

    void broadcast_phase(const std::string& phase, const std::string& description) {
        json msg = {
            {"origin", "primordial_loop"},
            {"intent", "bootstrap_phase"},
            {"phase", phase},
            {"description", description}
        };
        routing::publish(pub_, msg);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::PrimordialLoop loop;
    loop.start();
    return 0;
}
