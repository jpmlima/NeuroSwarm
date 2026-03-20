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
        sub_.connect("tcp://localhost:5556");
        routing::subscribe(sub_, {"operator_request", "probe_request", "goal_request", "inference_result"});

        fs::create_directories("data");
        fs::create_directories("data/sandbox");

        std::cout << "[PRIMORDIAL] Awakening. I know nothing." << std::endl;
    }

    void start() {
        // Phase 0: Existence
        std::cout << "[PRIMORDIAL] Phase 0 — I exist. I can execute." << std::endl;
        broadcast_phase("existence", "I can execute and observe.");

        // Phase 1: First Contact
        std::cout << "[PRIMORDIAL] Phase 1 — First contact with reality." << std::endl;
        phase_first_contact();

        // Phase 2: Capability Discovery
        std::cout << "[PRIMORDIAL] Phase 2 — Discovering capabilities." << std::endl;
        phase_capability_discovery();

        // Phase 3: Sense Acquisition
        std::cout << "[PRIMORDIAL] Phase 3 — Acquiring senses." << std::endl;
        phase_sense_acquisition();

        // Phase 4: Tool Discovery
        std::cout << "[PRIMORDIAL] Phase 4 — Discovering tools." << std::endl;
        phase_tool_discovery();

        // Phase 5: Active Exploration — use variation to discover new operators
        std::cout << "[PRIMORDIAL] Phase 5 — Active exploration via variation." << std::endl;
        phase_active_exploration();

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
        std::cout << "[PRIMORDIAL] Phase 6 — Planner self-test." << std::endl;
        phase_planner_selftest();

        // Phase 7: Network Expansion — discover and probe remote hosts
        if (self_.has_network) {
            std::cout << "[PRIMORDIAL] Phase 7 — Network expansion." << std::endl;
            network_ = std::make_unique<NetworkExpander>(pub_);
            network_->set_local_info(self_.user, self_.arch);
            phase_network_expansion();
        } else {
            std::cout << "[PRIMORDIAL] Phase 7 — Skipped (no network)." << std::endl;
        }

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
        bool success = (r.exit_code == 0);

        size_t output_hash = std::hash<std::string>{}(r.output);
        auto s = surprise_.compute(context, success, output_hash);

        std::string surprise_bar(static_cast<int>(s.surprise * 10), '!');
        std::cout << "[PRIMORDIAL]   " << context
                  << " → " << (success ? "OK" : "FAIL")
                  << " [" << surprise_bar << "] "
                  << r.duration_ms << "ms" << std::endl;

        // Learn operator if successful and named
        if (success && !op_name.empty()) {
            Operator op;
            op.name = op_name;
            op.command_template = cmd;
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

        std::cout << "[PRIMORDIAL]   Languages available: ";
        for (const auto& l : self_.languages) std::cout << l << " ";
        std::cout << std::endl;
    }

    // ─── Phase 5: Active Exploration ───

    void phase_active_exploration() {
        // Sync the variation engine with all known fragments
        variation_.sync_fragments(registry_);

        size_t ops_before = registry_.size();
        int candidates_tested = 0;
        int candidates_succeeded = 0;

        // Get stable operators to use as parents
        auto stable = registry_.get_stable();

        // Strategy 1: Mutate each known operator
        std::cout << "[PRIMORDIAL]   Mutating known operators..." << std::endl;
        for (auto* parent : stable) {
            auto mutations = variation_.mutate(*parent, 3);
            for (const auto& candidate : mutations) {
                if (test_candidate(candidate)) candidates_succeeded++;
                candidates_tested++;
            }
        }

        // Strategy 2: Recombine pairs of operators
        if (stable.size() >= 2) {
            std::cout << "[PRIMORDIAL]   Recombining operator pairs..." << std::endl;
            for (size_t i = 0; i < stable.size() - 1 && i < 5; i++) {
                auto crosses = variation_.recombine(*stable[i], *stable[i + 1], 2);
                for (const auto& candidate : crosses) {
                    if (test_candidate(candidate)) candidates_succeeded++;
                    candidates_tested++;
                }
            }
        }

        // Strategy 3: Explore unknown binaries from PATH
        std::cout << "[PRIMORDIAL]   Probing unknown binaries..." << std::endl;
        probe_unknown_binaries(20);

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
        // Safety: skip obviously dangerous commands
        if (is_dangerous(candidate.command)) return false;

        // Execute in sandbox context (short timeout via timeout command)
        std::string safe_cmd = "timeout 5 bash -c " + shell_escape(candidate.command);
        auto r = exec(safe_cmd);
        bool success = (r.exit_code == 0);

        std::string context = "variation_" + candidate.origin + "_" + candidate.command.substr(0, 30);
        size_t output_hash = std::hash<std::string>{}(r.output);
        auto s = surprise_.compute(context, success, output_hash);

        if (success && s.is_novel && !r.output.empty()) {
            // New successful command that produces novel output — learn it!
            std::string op_name = infer_operator_name(candidate.command);

            // Don't duplicate existing operators
            if (!registry_.find(op_name)) {
                Operator op;
                op.name = op_name;
                op.command_template = candidate.command;
                op.language = "bash";
                op.learned_from = candidate.origin;
                op.record_use(true, r.duration_ms);
                registry_.add(op);

                std::cout << "[PRIMORDIAL]   EVOLVED: \"" << candidate.command << "\""
                          << " via " << candidate.origin;
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

            // Try running with --help first (safe, informative)
            std::string cmd = bin + " --help";
            auto r = exec("timeout 3 " + cmd + " 2>&1 | head -5");

            if (r.exit_code == 0 || (r.exit_code != 0 && !r.output.empty() && r.output.size() > 10)) {
                // The binary exists and responds — learn it as a potential operator
                std::string context = "binary_probe_" + bin;
                size_t output_hash = std::hash<std::string>{}(r.output);
                surprise_.compute(context, true, output_hash);

                // Try without arguments too
                auto r2 = exec("timeout 3 " + bin + " 2>&1 | head -3");
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

    static std::string infer_operator_name(const std::string& cmd) {
        // Extract the first word as the operator base name
        std::string name;
        for (char c : cmd) {
            if (c == ' ' || c == '\t' || c == '|' || c == ';') break;
            if (c == '/') { name.clear(); continue; } // strip path prefix
            name += c;
        }
        // Append a suffix from arguments to make it unique
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
            // Resolve any remaining placeholders with defaults
            // (in a full system, bindings come from the goal context)

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
            } else {
                std::cout << "[PRIMORDIAL]     Plan execution failed at step: "
                          << step.operator_name << std::endl;
                break;
            }
        }
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
                }
            }

            // Periodic maintenance
            static int tick = 0;
            if (++tick % 600 == 0) {  // every ~60 seconds
                int pruned = registry_.prune();
                if (pruned > 0) {
                    std::cout << "[PRIMORDIAL] Pruned " << pruned << " dead operators." << std::endl;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
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

        // Learn if successful and named
        if (success && !op_name.empty() && !registry_.find(op_name)) {
            Operator op;
            op.name = op_name;
            op.command_template = cmd;
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
                                // SUCCESS — promote to operator
                                Operator new_op;
                                new_op.name = infer_operator_name(oracle_result.command);
                                new_op.command_template = oracle_result.command;
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
