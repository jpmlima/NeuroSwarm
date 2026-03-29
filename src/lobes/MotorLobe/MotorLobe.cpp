#include <zmq.hpp>
#include <string>
#include <iostream>
#include <memory>
#include <array>
#include <vector>
#include <regex>
#include <fstream>
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
                        std::string domain = j.value("domain", "");

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

                            // --- Safe Surgery Pipeline: backup → edit → syntax check → build → rollback on failure ---
                            std::string target_file = extract_sed_target(cmd);
                            std::string backup_path;
                            bool has_backup = false;

                            // Step 1: Backup the target file before modification
                            if (!target_file.empty() && std::filesystem::exists(target_file)) {
                                backup_path = target_file + ".surgery_backup";
                                try {
                                    std::filesystem::copy_file(target_file, backup_path,
                                        std::filesystem::copy_options::overwrite_existing);
                                    has_backup = true;
                                    std::cout << "[MOTOR] Backup created: " << backup_path << std::endl;
                                } catch (const std::exception& e) {
                                    std::cout << "[MOTOR] WARNING: Could not backup " << target_file << ": " << e.what() << std::endl;
                                }
                            }

                            // Step 2: Execute the sed/patch command (without build)
                            out = execute(cmd, exit_code);
                            if (exit_code != 0) {
                                out += "\n[MOTOR] Surgery FAILED: edit command returned non-zero.";
                                if (has_backup) rollback_file(target_file, backup_path);
                            } else if (!target_file.empty()) {
                                // Step 3: Syntax-check the modified file before full build
                                int syntax_code = 0;
                                std::string syntax_out = execute(
                                    "g++ -std=c++17 -fsyntax-only -I/home/xenomai/Documents/NeuroSwarm/include -I/home/xenomai/Documents/NeuroSwarm/src " + target_file + " 2>&1", syntax_code);
                                if (syntax_code != 0) {
                                    // Syntax error — rollback immediately, do NOT attempt build
                                    out += "\n[MOTOR] Surgery REJECTED: syntax check failed:\n" + syntax_out;
                                    exit_code = 1;
                                    if (has_backup) rollback_file(target_file, backup_path);
                                    std::cout << "[MOTOR] Syntax check FAILED. File rolled back." << std::endl;
                                } else {
                                    // Step 4: Syntax OK — proceed with full rebuild
                                    std::cout << "[MOTOR] Syntax check passed. Rebuilding..." << std::endl;
                                    int build_code = 0;
                                    std::string build_out = execute(
                                        "cd /home/xenomai/Documents/NeuroSwarm/build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) 2>&1", build_code);
                                    if (build_code != 0) {
                                        out += "\n[MOTOR] Surgery REJECTED: build failed:\n" + build_out.substr(0, 500);
                                        exit_code = 1;
                                        if (has_backup) rollback_file(target_file, backup_path);
                                        std::cout << "[MOTOR] Build FAILED. File rolled back." << std::endl;
                                    } else {
                                        out += "\n" + build_out + "\n[MOTOR] Surgery successful. Matrix recompiled.";
                                        exit_code = 0;
                                        // Clean up backup on success
                                        if (has_backup) std::filesystem::remove(backup_path);
                                    }
                                }
                            } else {
                                // No target file extracted — legacy fallback: run cmd + build
                                out = execute(cmd + " && cd /home/xenomai/Documents/NeuroSwarm/build && cmake .. && make -j$(nproc) 2>&1", exit_code);
                                if (exit_code == 0) {
                                    out += "\n[MOTOR] Surgery successful. Matrix recompiled.";
                                }
                            }
                        } else {
                            if (cmd == "whoami") {
                                out = "ERROR: whoami command is deprecated and disabled for security reasons.";
                                exit_code = 1;
                            } else if (is_destructive_write(cmd)) {
                                // SAFETY: block commands that truncate files via popen
                                out = "ERROR: blocked destructive write command (bare tee truncates files via popen).";
                                exit_code = 1;
                                std::cout << "[MOTOR] SAFETY BLOCK: " << cmd << std::endl;
                            } else {
                                out = execute(cmd, exit_code);
                            }
                        }
                        
                        json resp = {
                            {"cid", cid},
                            {"origin", "motor_cortex"},
                            {"intent", "execution_result"},
                            {"command", cmd},
                            {"proprioception", out},
                            {"exit_code", exit_code},
                            {"status", (exit_code == 0 ? "success" : "failure")},
                            {"mode", mode},
                            {"domain", domain}
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

    // Extract the target file path from a sed -i command
    // Supports: sed -i 's/.../.../g' file.cpp, sed -i'' 's/.../.../g' file.cpp
    std::string extract_sed_target(const std::string& cmd) {
        // Match .cpp or .h file paths in the command
        std::regex file_re(R"((\S+\.(?:cpp|h|hpp))\b)");
        std::sregex_iterator it(cmd.begin(), cmd.end(), file_re);
        std::sregex_iterator end;
        std::string last_match;
        // Take the last match — sed puts the file at the end
        while (it != end) {
            last_match = (*it)[1].str();
            ++it;
        }
        return last_match;
    }

    // Restore a file from its backup
    void rollback_file(const std::string& target, const std::string& backup) {
        try {
            std::filesystem::copy_file(backup, target,
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(backup);
            std::cout << "[MOTOR] ROLLBACK: Restored " << target << " from backup." << std::endl;
        } catch (const std::exception& e) {
            std::cout << "[MOTOR] ROLLBACK FAILED for " << target << ": " << e.what() << std::endl;
        }
    }

    // SAFETY: block commands that silently destroy files when run via popen
    // popen(cmd, "r") provides no stdin, so tee/tee -a get immediate EOF and truncate/no-op
    bool is_destructive_write(const std::string& cmd) {
        // Bare tee without pipe input — truncates target file
        if (cmd.find("tee ") == 0 || cmd.find("tee -") == 0) return true;
        // rm -rf on source dirs
        if (cmd.find("rm -rf src/") != std::string::npos) return true;
        if (cmd.find("rm -rf include/") != std::string::npos) return true;
        // Redirect that empties files: > src/file.cpp
        if (cmd.find("> src/") != std::string::npos && cmd.find(">>") == std::string::npos) return true;
        if (cmd.find("> include/") != std::string::npos && cmd.find(">>") == std::string::npos) return true;
        return false;
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
