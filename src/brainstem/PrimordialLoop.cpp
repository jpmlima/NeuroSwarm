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
        routing::subscribe(sub_, {"operator_request", "probe_request"});

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

        // Report
        report_self_model();

        std::cout << "[PRIMORDIAL] Bootstrap complete. "
                  << registry_.size() << " operators learned. "
                  << "Global surprise: " << surprise_.global_surprise()
                  << std::endl;

        broadcast_phase("bootstrap_complete", "Learned " + std::to_string(registry_.size()) + " operators.");

        // Enter service loop — respond to operator requests from other lobes
        service_loop();
    }

private:
    zmq::context_t ctx_;
    zmq::socket_t pub_;
    zmq::socket_t sub_;
    SurpriseEngine surprise_;
    OperatorRegistry registry_;

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
            {"contexts_known", static_cast<int>(surprise_.contexts_known())}
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
