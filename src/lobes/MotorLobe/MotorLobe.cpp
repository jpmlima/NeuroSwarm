#include <zmq.hpp>
#include <string>
#include <iostream>
#include <memory>
#include <array>
#include <vector>
#include <csignal>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>

using json = nlohmann::json;

namespace neuroswarm {
class MotorLobe {
public:
    MotorLobe(const std::string& pub_addr = "tcp://localhost:5555",
              const std::string& sub_addr = "tcp://localhost:5556")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect(pub_addr);
        sub.connect(sub_addr);
        routing::subscribe(sub, {"execution_request", "genesis_request"});

        std::cout << "[MOTOR] Cortex online." << std::endl;
    }

    void start() {
        while (true) {
            auto j = routing::receive(sub);
            if (j.is_null()) continue;
            {
                try {

                    if (j.value("intent", "") == "execution_request") {
                        std::string cmd = j.value("command", "");
                        std::string mode = j.value("mode", "reality"); // Execution context: "reality" (live), "dream" (sandboxed), or "neuro_surgery" (self-modification)
                        std::string cid = j.value("cid", "unknown");

                        int exit_code = 0;
                        std::string out;

                        if (mode == "dream") {
                            // Phase 1: Smart Dream Bypass — read-only commands skip sandbox
                            if (is_read_only(cmd)) {
                                out = execute(cmd, exit_code);
                                std::cout << "[MOTOR] DREAM BYPASS (read-only) for CID: " << cid << " exit=" << exit_code << std::endl;
                            } else {
                                std::string dream_path = "./data/dreams/" + cid;
                                std::filesystem::create_directories(dream_path + "/data");
                                out = execute("cd " + dream_path + " && " + cmd, exit_code);
                                std::cout << "[MOTOR] DREAM SEQUENCE executed for CID: " << cid << " exit=" << exit_code << std::endl;
                            }
                        } else if (mode == "neuro_surgery") {
                            std::cout << "[MOTOR] WARNING: NEURO-SURGERY INITIATED. MODIFYING OWN SOURCE CODE." << std::endl;
                            // Neuro-surgery mode: expects 'cmd' to be a valid shell sequence that patches source files and triggers a full CMake rebuild.
                            out = execute(cmd + " && cd build && cmake .. && make -j$(nproc) 2>&1", exit_code);
                            if (exit_code == 0) {
                                out += "\n[MOTOR] Surgery successful. Matrix recompiled.";
                            }
                        } else {
                            out = execute(cmd, exit_code);
                        }
                        
                        json resp = {
                            {"cid", cid},
                            {"origin", "motor_cortex"},
                            {"intent", "execution_result"},
                            {"command", cmd},
                            {"proprioception", out},
                            {"exit_code", exit_code},
                            {"status", (exit_code == 0 ? "success" : "failure")},
                            {"mode", mode}
                        };
                        dispatch(resp);
                    } else if (j.value("intent", "") == "genesis_request") {
                        std::cout << "[MOTOR] Received genesis_request!" << std::endl;
                        std::string name = j.value("name", "NEW_LOBE");
                        std::string source = j.value("source_path", "");
                        std::string output = j.value("output_path", "build/" + name);

                        // Compile the new lobe as a standalone executable
                        std::string cmd = "g++ -std=c++17 -I./include -I./src " + source + " -o " + output + " -lzmq -lpthread";
                        std::cout << "[MOTOR] Genesis compiling: " << cmd << std::endl;
                        
                        int exit_code = 0;
                        std::string out = execute(cmd, exit_code);
                        std::cout << "[MOTOR] Genesis compilation exit_code: " << exit_code << ", output: " << out << std::endl;
                        
                        if (exit_code == 0) {
                            json inject = {
                                {"intent", "inject_lobe"},
                                {"origin", "motor_cortex"},
                                {"name", name},
                                {"path", output}
                            };
                            dispatch(inject);
                            out += "\n[MOTOR] Genesis successful. Injection signal sent to Matrix.";
                        }
                        
                        json resp = {
                            {"origin", "motor_cortex"},
                            {"intent", "genesis_result"},
                            {"name", name},
                            {"proprioception", out},
                            {"exit_code", exit_code},
                            {"status", (exit_code == 0 ? "success" : "failure")}
                        };
                        dispatch(resp);
                    }
                } catch (...) {}
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    void dispatch(const json& data) {
        routing::publish(pub, data);
    }

    // Phase 1: Smart Dream Bypass — read-only commands execute in real CWD
    bool is_read_only(const std::string& cmd) {
        static const std::vector<std::string> ro_prefixes = {
            "cat ", "head ", "tail ", "less ", "wc ", "file ", "stat ",
            "ls ", "find ", "grep ", "rg ", "readlink ", "md5sum ",
            "sha256sum ", "du ", "df ", "ps ", "uptime", "free ",
            "uname", "whoami", "id ", "date", "env", "echo $",
            "python3 -c", "jq ", "pgrep", "top ", "ss ", "netstat",
            "git -C", "git log", "git status", "git diff", "git show"
        };
        for (const auto& p : ro_prefixes)
            if (cmd.rfind(p, 0) == 0) return true;
        return false;
    }

    std::string execute(const std::string& cmd, int& exit_code) {
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen((cmd + " 2>&1").c_str(), "r"), pclose);
        if (!pipe) {
            exit_code = -1;
            return "Error: Execution failed";
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) result += buffer.data();
        exit_code = pclose(pipe.release());
        return result;
    }
};
}
int main() {
    // CerebralMatrix sets SIGCHLD to SIG_IGN to auto-reap zombie processes.
    // popen/pclose require SIGCHLD=SIG_DFL for waitpid() to function correctly.
    signal(SIGCHLD, SIG_DFL);
    neuroswarm::MotorLobe().start();
    return 0;
}
