#pragma once
// Autopoiesis Phase E — Network Expansion
//
// Biological analogue: colonisation. When an organism has fully adapted
// to its niche, growth requires a new environment. Spores, seeds, larvae
// — life's strategy is always the same: send a minimal viable copy to
// a new substrate and let it adapt locally.
//
// The NetworkExpander discovers reachable machines, deploys a minimal
// kernel, and establishes communication. Each remote instance bootstraps
// independently — discovering its own environment, learning its own
// operators, developing its own capabilities.
//
// Two instances running for a month on different machines become
// different organisms. Same DNA, different phenotype.

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <iostream>
#include <array>
#include <memory>
#include <filesystem>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>

namespace neuroswarm {

struct RemoteHost {
    std::string hostname;
    std::string user;
    std::string address;       // IP or hostname
    int port = 22;
    bool reachable = false;
    bool authenticated = false;
    bool has_kernel = false;    // kernel deployed?
    bool kernel_running = false;
    std::string arch;
    std::string os;
    int operators_learned = 0;
    std::string last_contact;
    std::string deploy_path;   // where the kernel lives on remote
};

class NetworkExpander {
public:
    NetworkExpander(zmq::socket_t& pub) : pub_(pub) {}

    // Phase 1: Discover reachable hosts from SSH known_hosts + config
    std::vector<RemoteHost> discover_hosts() {
        std::vector<RemoteHost> hosts;

        // Read ~/.ssh/known_hosts
        auto known = parse_known_hosts();
        for (const auto& addr : known) {
            RemoteHost h;
            h.address = addr;
            h.hostname = addr;
            h.user = local_user_;
            hosts.push_back(h);
        }

        // Read ~/.ssh/config for named hosts
        auto configured = parse_ssh_config();
        for (auto& h : configured) {
            // Deduplicate
            bool exists = false;
            for (const auto& existing : hosts) {
                if (existing.address == h.address || existing.hostname == h.hostname) {
                    exists = true;
                    break;
                }
            }
            if (!exists) hosts.push_back(h);
        }

        std::cout << "[NETWORK] Discovered " << hosts.size()
                  << " potential hosts from SSH config." << std::endl;

        return hosts;
    }

    // Phase 2: Probe hosts for reachability and authentication
    void probe_hosts(std::vector<RemoteHost>& hosts, int timeout_sec = 5) {
        for (auto& host : hosts) {
            std::cout << "[NETWORK] Probing " << host.address << "..." << std::endl;

            // Test SSH connection (non-interactive, with timeout)
            // Test SSH connection (non-interactive, with timeout)
            std::string cmd = "ssh -o BatchMode=yes -o ConnectTimeout="
                + std::to_string(timeout_sec)
                + " -o StrictHostKeyChecking=no"
                + (host.port != 22 ? " -p " + std::to_string(host.port) : "")
                + " " + host.user + "@" + host.address
                + " 'echo __NEUROSWARM_PROBE__ && uname -sm' 2>/dev/null";

            auto r = exec(cmd);

            if (r.exit_code == 0 && r.output.find("__NEUROSWARM_PROBE__") != std::string::npos) {
                host.reachable = true;
                host.authenticated = true;

                // Parse uname output
                auto lines = split_lines(r.output);
                if (lines.size() >= 2) {
                    auto parts = split(lines[1], ' ');
                    if (parts.size() >= 2) {
                        host.os = parts[0];
                        host.arch = parts[1];
                    }
                }

                std::cout << "[NETWORK]   " << host.address << " — REACHABLE ("
                          << host.os << " " << host.arch << ")" << std::endl;

                // Check if kernel is already deployed
                std::string check_cmd = "ssh -o BatchMode=yes -o ConnectTimeout="
                    + std::to_string(timeout_sec)
                    + " " + host.user + "@" + host.address
                    + " 'test -x ~/neuroswarm/primordial_loop && echo EXISTS' 2>/dev/null";
                auto check = exec(check_cmd);
                if (check.output.find("EXISTS") != std::string::npos) {
                    host.has_kernel = true;
                    host.deploy_path = "~/neuroswarm/";
                    std::cout << "[NETWORK]   Kernel already deployed." << std::endl;
                }
            } else {
                host.reachable = false;
                std::cout << "[NETWORK]   " << host.address << " — unreachable" << std::endl;
            }
        }

        int reachable = 0;
        for (const auto& h : hosts) if (h.reachable) reachable++;
        std::cout << "[NETWORK] " << reachable << "/" << hosts.size()
                  << " hosts reachable." << std::endl;
    }

    // Phase 3: Deploy kernel to a remote host
    bool deploy_kernel(RemoteHost& host, const std::string& local_kernel_path) {
        if (!host.reachable || !host.authenticated) {
            std::cout << "[NETWORK] Cannot deploy to " << host.address
                      << " — not reachable/authenticated." << std::endl;
            return false;
        }

        // Check architecture compatibility — attempt cross-compilation if needed
        if (!host.arch.empty() && host.arch != local_arch_) {
            std::cout << "[NETWORK] Architecture mismatch: local=" << local_arch_
                      << " remote=" << host.arch
                      << ". Attempting cross-compilation..." << std::endl;

            std::string cross_binary = cross_compile(local_kernel_path, host.arch);
            if (cross_binary.empty()) {
                std::cout << "[NETWORK] Cross-compilation failed for " << host.arch
                          << ". Skipping deployment." << std::endl;
                return false;
            }

            // Deploy the cross-compiled binary instead
            std::cout << "[NETWORK] Cross-compiled kernel for " << host.arch << std::endl;
            return deploy_binary(host, cross_binary);
        }

        std::cout << "[NETWORK] Deploying kernel to " << host.address << "..." << std::endl;

        // Create remote directory
        std::string mkdir_cmd = "ssh -o BatchMode=yes "
            + host.user + "@" + host.address
            + " 'mkdir -p ~/neuroswarm/data' 2>/dev/null";
        auto r1 = exec(mkdir_cmd);
        if (r1.exit_code != 0) {
            std::cout << "[NETWORK] Failed to create remote directory." << std::endl;
            return false;
        }

        // Copy kernel binary
        std::string scp_cmd = "scp -o BatchMode=yes "
            + local_kernel_path + " "
            + host.user + "@" + host.address + ":~/neuroswarm/primordial_loop"
            + " 2>/dev/null";
        auto r2 = exec(scp_cmd);
        if (r2.exit_code != 0) {
            std::cout << "[NETWORK] Failed to copy kernel binary." << std::endl;
            return false;
        }

        // Copy shared libraries (routing.hpp is compiled-in, but we need ZMQ)
        // The remote machine must have libzmq installed
        std::string check_zmq = "ssh -o BatchMode=yes "
            + host.user + "@" + host.address
            + " 'ldconfig -p 2>/dev/null | grep libzmq || echo MISSING' 2>/dev/null";
        auto r3 = exec(check_zmq);
        if (r3.output.find("MISSING") != std::string::npos) {
            std::cout << "[NETWORK] WARNING: libzmq not found on remote. "
                      << "Kernel may not run." << std::endl;
        }

        // Make executable
        std::string chmod_cmd = "ssh -o BatchMode=yes "
            + host.user + "@" + host.address
            + " 'chmod +x ~/neuroswarm/primordial_loop' 2>/dev/null";
        exec(chmod_cmd);

        host.has_kernel = true;
        host.deploy_path = "~/neuroswarm/";

        std::cout << "[NETWORK] Kernel deployed to " << host.address << "." << std::endl;

        // Broadcast deployment event
        nlohmann::json event = {
            {"origin", "network_expander"},
            {"intent", "kernel_deployed"},
            {"remote_host", host.address},
            {"remote_arch", host.arch},
            {"remote_os", host.os}
        };
        routing::publish(pub_, event);

        return true;
    }

    // Phase 4: Start kernel on remote host
    bool start_remote_kernel(RemoteHost& host, int thalamus_port = 5555) {
        if (!host.has_kernel) {
            std::cout << "[NETWORK] No kernel deployed on " << host.address << std::endl;
            return false;
        }

        std::cout << "[NETWORK] Starting kernel on " << host.address << "..." << std::endl;

        // Start in background with nohup, redirect output to log
        std::string start_cmd = "ssh -o BatchMode=yes "
            + host.user + "@" + host.address
            + " 'cd ~/neuroswarm && nohup ./primordial_loop"
            + " > data/bootstrap.log 2>&1 & echo $!'"
            + " 2>/dev/null";

        auto r = exec(start_cmd);
        if (r.exit_code == 0 && !r.output.empty()) {
            host.kernel_running = true;
            std::cout << "[NETWORK] Remote kernel started on " << host.address
                      << " (PID: " << trim(r.output) << ")" << std::endl;

            // Broadcast
            nlohmann::json event = {
                {"origin", "network_expander"},
                {"intent", "remote_kernel_started"},
                {"remote_host", host.address},
                {"remote_pid", trim(r.output)}
            };
            routing::publish(pub_, event);

            return true;
        }

        std::cout << "[NETWORK] Failed to start remote kernel." << std::endl;
        return false;
    }

    // Phase 5: Check remote kernel status and retrieve its self-model
    nlohmann::json query_remote(RemoteHost& host) {
        if (!host.kernel_running) return {};

        std::string cmd = "ssh -o BatchMode=yes "
            + host.user + "@" + host.address
            + " 'cat ~/neuroswarm/data/self_model_primordial.json 2>/dev/null'"
            + " 2>/dev/null";

        auto r = exec(cmd);
        if (r.exit_code == 0 && !r.output.empty()) {
            try {
                auto model = nlohmann::json::parse(r.output);
                host.operators_learned = model.value("operators_learned", 0);
                host.last_contact = model.value("last_update", "");
                return model;
            } catch (...) {}
        }
        return {};
    }

    // Phase 6: Exchange operators between local and remote
    int sync_operators(RemoteHost& host,
                       const std::string& local_operators_path,
                       const std::string& remote_operators_path = "~/neuroswarm/data/operators.jsonl") {
        if (!host.kernel_running) return 0;

        // Download remote operators
        std::string cmd = "ssh -o BatchMode=yes "
            + host.user + "@" + host.address
            + " 'cat " + remote_operators_path + " 2>/dev/null'"
            + " 2>/dev/null";

        auto r = exec(cmd);
        if (r.exit_code != 0 || r.output.empty()) return 0;

        // Parse remote operators and find ones we don't have
        int imported = 0;
        std::ifstream local_file(local_operators_path);
        std::unordered_set<std::string> local_names;
        std::string line;
        while (std::getline(local_file, line)) {
            try {
                auto j = nlohmann::json::parse(line);
                local_names.insert(j.value("name", ""));
            } catch (...) {}
        }

        std::istringstream remote_stream(r.output);
        std::ofstream append_file(local_operators_path, std::ios::app);
        while (std::getline(remote_stream, line)) {
            try {
                auto j = nlohmann::json::parse(line);
                std::string name = j.value("name", "");
                if (!name.empty() && !local_names.count(name)) {
                    // New operator from remote — import it
                    // Mark origin as remote
                    j["learned_from"] = "remote_sync:" + host.address;
                    append_file << j.dump() << "\n";
                    imported++;
                }
            } catch (...) {}
        }

        if (imported > 0) {
            std::cout << "[NETWORK] Imported " << imported
                      << " new operators from " << host.address << std::endl;

            nlohmann::json event = {
                {"origin", "network_expander"},
                {"intent", "operators_synced"},
                {"remote_host", host.address},
                {"operators_imported", imported}
            };
            routing::publish(pub_, event);
        }

        return imported;
    }

    void set_local_info(const std::string& user, const std::string& arch) {
        local_user_ = user;
        local_arch_ = arch;
    }

private:
    zmq::socket_t& pub_;
    std::string local_user_ = "root";
    std::string local_arch_ = "x86_64";

    struct ExecResult {
        std::string output;
        int exit_code;
    };

    ExecResult exec(const std::string& cmd) {
        std::array<char, 256> buffer;
        std::string result;
        // Suppress GUI askpass dialogs — force non-interactive SSH
        std::string wrapped = "SSH_ASKPASS='' DISPLAY='' " + cmd;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(
            popen((wrapped + " 2>&1").c_str(), "r"), pclose);
        int exit_code = -1;
        if (pipe) {
            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                result += buffer.data();
                if (result.size() > 16384) break;
            }
            exit_code = pclose(pipe.release());
        }
        return {result, exit_code};
    }

    std::vector<std::string> parse_known_hosts() {
        std::vector<std::string> hosts;
        std::string path = std::string(getenv("HOME") ? getenv("HOME") : "") + "/.ssh/known_hosts";
        std::ifstream f(path);
        if (!f.is_open()) return hosts;

        std::string line;
        std::unordered_set<std::string> seen;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#' || line[0] == '|') continue;
            // Format: hostname,ip ssh-type key
            auto space = line.find(' ');
            if (space == std::string::npos) continue;
            std::string host_part = line.substr(0, space);
            // May contain multiple hosts separated by comma
            std::istringstream hs(host_part);
            std::string h;
            while (std::getline(hs, h, ',')) {
                // Skip [host]:port format for now, just extract host
                if (h[0] == '[') {
                    auto bracket = h.find(']');
                    if (bracket != std::string::npos) h = h.substr(1, bracket - 1);
                }
                // Skip IP addresses that look like localhost
                if (h == "127.0.0.1" || h == "::1" || h == "localhost") continue;
                if (seen.count(h)) continue;
                seen.insert(h);
                hosts.push_back(h);
            }
        }
        return hosts;
    }

    std::vector<RemoteHost> parse_ssh_config() {
        std::vector<RemoteHost> hosts;
        std::string path = std::string(getenv("HOME") ? getenv("HOME") : "") + "/.ssh/config";
        std::ifstream f(path);
        if (!f.is_open()) return hosts;

        RemoteHost current;
        bool in_host = false;
        std::string line;
        while (std::getline(f, line)) {
            // Trim
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            if (line.empty() || line[0] == '#') continue;

            if (line.substr(0, 5) == "Host " && line.find('*') == std::string::npos) {
                if (in_host && !current.hostname.empty()) {
                    if (current.address.empty()) current.address = current.hostname;
                    hosts.push_back(current);
                }
                current = RemoteHost{};
                current.hostname = line.substr(5);
                // Trim hostname
                start = current.hostname.find_first_not_of(" \t");
                if (start != std::string::npos) current.hostname = current.hostname.substr(start);
                in_host = true;
            } else if (in_host) {
                if (line.substr(0, 9) == "HostName " || line.substr(0, 9) == "Hostname ") {
                    current.address = trim(line.substr(9));
                } else if (line.substr(0, 5) == "User ") {
                    current.user = trim(line.substr(5));
                } else if (line.substr(0, 5) == "Port ") {
                    try { current.port = std::stoi(trim(line.substr(5))); } catch (...) {}
                }
            }
        }
        if (in_host && !current.hostname.empty()) {
            if (current.address.empty()) current.address = current.hostname;
            hosts.push_back(current);
        }
        return hosts;
    }

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }

    static std::vector<std::string> split_lines(const std::string& s) {
        std::vector<std::string> lines;
        std::istringstream stream(s);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) lines.push_back(line);
        }
        return lines;
    }

    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> parts;
        std::istringstream stream(s);
        std::string part;
        while (std::getline(stream, part, delim)) {
            if (!part.empty()) parts.push_back(part);
        }
        return parts;
    }

    std::unordered_set<std::string> local_names_; // for dedup during sync

    // Cross-compilation: map remote arch to toolchain prefix
    static std::string get_cross_toolchain(const std::string& arch) {
        static const std::map<std::string, std::string> toolchains = {
            {"aarch64",  "aarch64-linux-gnu-g++"},
            {"armv7l",   "arm-linux-gnueabihf-g++"},
            {"riscv64",  "riscv64-linux-gnu-g++"},
            {"i686",     "i686-linux-gnu-g++"},
            {"mips",     "mips-linux-gnu-g++"},
            {"mips64",   "mips64-linux-gnu-g++"},
            {"ppc64le",  "powerpc64le-linux-gnu-g++"},
            {"s390x",    "s390x-linux-gnu-g++"}
        };
        auto it = toolchains.find(arch);
        return it != toolchains.end() ? it->second : "";
    }

    // Cross-compile the kernel source for a different architecture
    std::string cross_compile(const std::string& source_dir, const std::string& target_arch) {
        std::string compiler = get_cross_toolchain(target_arch);
        if (compiler.empty()) {
            std::cout << "[NETWORK] No cross-compiler known for arch: " << target_arch << std::endl;
            return "";
        }

        // Check if cross-compiler is installed
        auto check = exec("which " + compiler + " 2>/dev/null");
        if (check.exit_code != 0) {
            std::cout << "[NETWORK] Cross-compiler not installed: " << compiler << std::endl;
            return "";
        }

        std::string output = "/tmp/neuroswarm_kernel_" + target_arch;
        std::string src = "src/brainstem/PrimordialLoop.cpp";

        std::string cmd = compiler + " -std=c++17 -O2"
            " -I./include -I./src"
            " -o " + output +
            " " + src +
            " -lzmq -lpthread"
            " 2>&1";

        std::cout << "[NETWORK] Cross-compiling for " << target_arch
                  << " with " << compiler << "..." << std::endl;

        auto r = exec(cmd);
        if (r.exit_code != 0) {
            std::cout << "[NETWORK] Cross-compilation failed:\n"
                      << r.output.substr(0, 500) << std::endl;
            return "";
        }

        std::cout << "[NETWORK] Cross-compilation succeeded: " << output << std::endl;
        return output;
    }

    // Deploy a pre-compiled binary to remote host
    bool deploy_binary(RemoteHost& host, const std::string& binary_path) {
        // Create remote directory
        std::string mkdir_cmd = "ssh -o BatchMode=yes "
            + host.user + "@" + host.address
            + " 'mkdir -p ~/neuroswarm/data' 2>/dev/null";
        auto r1 = exec(mkdir_cmd);
        if (r1.exit_code != 0) return false;

        // Copy binary
        std::string scp_cmd = "scp -o BatchMode=yes "
            + binary_path + " "
            + host.user + "@" + host.address + ":~/neuroswarm/primordial_loop"
            + " 2>/dev/null";
        auto r2 = exec(scp_cmd);
        if (r2.exit_code != 0) return false;

        // Make executable
        exec("ssh -o BatchMode=yes " + host.user + "@" + host.address
             + " 'chmod +x ~/neuroswarm/primordial_loop' 2>/dev/null");

        host.has_kernel = true;
        host.deploy_path = "~/neuroswarm/";

        std::cout << "[NETWORK] Cross-compiled kernel deployed to "
                  << host.address << " (" << host.arch << ")" << std::endl;

        nlohmann::json event = {
            {"origin", "network_expander"},
            {"intent", "kernel_deployed"},
            {"remote_host", host.address},
            {"remote_arch", host.arch},
            {"cross_compiled", true}
        };
        routing::publish(pub_, event);

        return true;
    }
};

} // namespace neuroswarm
