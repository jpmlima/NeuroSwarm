#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/prctl.h>
#include <chrono>
#include <thread>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <array>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <Heartbeat.hpp>

using json = nlohmann::json;

namespace neuroswarm {

class CerebralMatrix {
public:
    CerebralMatrix()
        : ctx(1), pub(ctx, zmq::socket_type::pub),
          inject_sub(ctx, zmq::socket_type::sub) {

        pub.connect("tcp://localhost:5555");
        inject_sub.connect("tcp://localhost:5556");
        routing::subscribe(inject_sub, {"inject_lobe", "lobe_terminate"});

        const std::string base = "/home/xenomai/Documents/NeuroSwarm/build/";
        // PrimordialLoop bootstraps first — discovers environment, learns operators
        init_lobes.push_back({"PRIMORDIAL", base + "primordial_loop"});
        init_lobes.push_back({"THALAMUS", base + "thalamus"});
        init_lobes.push_back({"SYNAPTIC", base + "synaptic_controller"});
        init_lobes.push_back({"HIPPOCAMPUS", base + "hippocampus"});
        init_lobes.push_back({"MOTOR", base + "motor_lobe"});
        init_lobes.push_back({"EXECUTIVE", base + "frontal_executive"});
        init_lobes.push_back({"AMYGDALA", base + "amygdala"});
        init_lobes.push_back({"WERNICKE", base + "wernicke_lobe"});
        init_lobes.push_back({"VISUAL", base + "visual_lobe"});
        init_lobes.push_back({"HOMEOSTASIS", base + "homeostasis"});
        init_lobes.push_back({"METACOGNITION", base + "metacognition"});
        init_lobes.push_back({"CRITIC", base + "critic_lobe"});
        init_lobes.push_back({"VISUALIZER", base + "visualizer"});
        init_lobes.push_back({"REM_ENGINE", base + "rem_engine"});
        init_lobes.push_back({"CHRONOS", base + "chronos_lobe"});
        init_lobes.push_back({"STATISTICS", base + "statistics_lobe"});
        init_lobes.push_back({"BASAL_GANGLIA", base + "basal_ganglia"});
        init_lobes.push_back({"CONCEPT", base + "concept_lobe"});
    }

    void awaken() {
        // Prevent systemd from wrapping fork'd children in transient units (polkit dialog)
        unsetenv("DBUS_SESSION_BUS_ADDRESS");

        std::cout << "[CEREBRAL] Starting all lobes..." << std::endl;
        signal(SIGHUP, SIG_IGN);
        // Phase 2: Do NOT set SIGCHLD to SIG_IGN — we need waitpid() for crash detection
        signal(SIGCHLD, SIG_DFL);

        // Kill orphaned processes from previous sessions
        kill_orphans();

        for (auto& lobe : init_lobes) {
            spawn_lobe(lobe.name, lobe.path);
        }

        std::cout << "[CEREBRAL] All lobes launched. Entering monitoring loop." << std::endl;

        // Phase 2: Self-Preservation — monitor child processes for crashes
        // Phase 6: Neurogenesis — check for lobe injection requests
        while (true) {
            monitor_lobes();
            check_injection();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

private:
    struct InitLobe {
        std::string name;
        std::string path;
    };

    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t inject_sub;
    std::vector<InitLobe> init_lobes;
    std::map<std::string, LobeRecord> registry;  // name → LobeRecord
    std::map<pid_t, std::string> pid_to_name;     // pid → lobe name (reverse lookup)

    // Kill orphaned processes from previous CerebralMatrix sessions.
    // Scans /proc for processes whose executable lives in our build/ directory.
    void kill_orphans() {
        pid_t my_pid = getpid();
        const std::string build_dir = "/home/xenomai/Documents/NeuroSwarm/build/";
        int killed = 0;

        DIR* proc = opendir("/proc");
        if (!proc) return;

        struct dirent* entry;
        while ((entry = readdir(proc)) != nullptr) {
            // Only numeric dirs (PIDs)
            if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;

            pid_t pid = std::atoi(entry->d_name);
            if (pid == my_pid || pid <= 1) continue;

            // Read /proc/PID/exe symlink
            std::string exe_link = "/proc/" + std::string(entry->d_name) + "/exe";
            char buf[512];
            ssize_t len = readlink(exe_link.c_str(), buf, sizeof(buf) - 1);
            if (len <= 0) continue;
            buf[len] = '\0';

            std::string exe_path(buf);
            // Check if this process is from our build directory
            if (exe_path.find(build_dir) == 0 || exe_path.find("build/") != std::string::npos) {
                std::cout << "[CEREBRAL] Killing orphan: " << exe_path
                          << " (PID: " << pid << ")" << std::endl;
                kill(pid, SIGKILL);
                killed++;
            }
        }
        closedir(proc);

        if (killed > 0) {
            std::cout << "[CEREBRAL] Cleaned up " << killed << " orphaned processes." << std::endl;
            // Brief pause to let processes die
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void spawn_lobe(const std::string& name, const std::string& path) {
        // Dedup guard — skip if lobe is already alive
        if (registry.count(name) && registry[name].alive) {
            std::cout << "[CEREBRAL] Skipping spawn for " << name
                      << " — already alive (PID: " << registry[name].pid << ")" << std::endl;
            return;
        }

        // Check backoff cooldown
        if (registry.count(name)) {
            auto& rec = registry[name];
            if (!rec.alive && rec.crash_count >= LobeRecord::MAX_RESTARTS) {
                return; // Marked DEAD — do not restart
            }
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - rec.last_restart).count();
            if (elapsed < rec.restart_cooldown_sec()) {
                return; // Still in cooldown
            }
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child: reset signal handlers
            signal(SIGCHLD, SIG_DFL);
            // Prevent systemd from wrapping children in transient units (avoids polkit dialog)
            unsetenv("DBUS_SESSION_BUS_ADDRESS");
            execl(path.c_str(), path.c_str(), (char*)NULL);
            _exit(1);
        } else if (pid > 0) {
            LobeRecord& rec = registry[name];
            rec.pid = pid;
            rec.name = name;
            rec.path = path;
            rec.alive = true;
            rec.last_restart = std::chrono::steady_clock::now();

            pid_to_name[pid] = name;

            std::cout << "[CEREBRAL] Launched " << name << " (PID: " << pid << ")";
            if (rec.crash_count > 0)
                std::cout << " [restart #" << rec.crash_count << "]";
            std::cout << std::endl;
        } else {
            std::cerr << "[CEREBRAL] fork() failed for " << name << std::endl;
        }
    }

    // Phase 6: Neurogenesis & Apoptosis — check for inject/terminate messages (non-blocking)
    void check_injection() {
        while (true) {
            auto j = routing::receive(inject_sub, zmq::recv_flags::dontwait);
            if (j.is_null()) break;

            std::string intent = j.value("intent", "");

            if (intent == "inject_lobe") {
                std::string name = j.value("name", "");
                std::string path = j.value("path", "");

                if (name.empty() || path.empty()) {
                    std::cerr << "[CEREBRAL] inject_lobe: missing name or path" << std::endl;
                    continue;
                }

                // Dedup — reject if already running
                if (registry.count(name) && registry[name].alive) {
                    std::cout << "[CEREBRAL] inject_lobe: " << name
                              << " already running (PID: " << registry[name].pid
                              << "). Ignoring." << std::endl;
                    continue;
                }

                // Validate binary exists and is executable
                struct stat st;
                if (::stat(path.c_str(), &st) != 0) {
                    std::cerr << "[CEREBRAL] inject_lobe: binary not found: " << path << std::endl;
                    continue;
                }
                if (!(st.st_mode & S_IXUSR)) {
                    std::cerr << "[CEREBRAL] inject_lobe: binary not executable: " << path << std::endl;
                    continue;
                }

                std::cout << "[CEREBRAL] NEUROGENESIS: Injecting new lobe " << name
                          << " from " << path << std::endl;

                spawn_lobe(name, path);

                json inject_event = {
                    {"origin", "cerebral_matrix"},
                    {"intent", "lobe_injected"},
                    {"lobe_name", name},
                    {"path", path}
                };
                routing::publish(pub, inject_event);

                std::cout << "[CEREBRAL] NEUROGENESIS: Lobe " << name
                          << " injected and running." << std::endl;

            } else if (intent == "lobe_terminate") {
                std::string target = j.value("lobe_name", "");
                if (target.empty()) continue;

                if (registry.count(target) && registry[target].alive) {
                    pid_t target_pid = registry[target].pid;
                    std::cout << "[CEREBRAL] APOPTOSIS: Terminating " << target
                              << " (PID: " << target_pid << ")" << std::endl;

                    kill(target_pid, SIGTERM);
                    std::this_thread::sleep_for(std::chrono::seconds(2));

                    // Force kill if still alive
                    if (waitpid(target_pid, nullptr, WNOHANG) == 0) {
                        kill(target_pid, SIGKILL);
                        waitpid(target_pid, nullptr, 0);
                    }

                    registry[target].alive = false;
                    pid_to_name.erase(target_pid);

                    json term_event = {
                        {"origin", "cerebral_matrix"},
                        {"intent", "lobe_terminated"},
                        {"lobe_name", target},
                        {"reason", j.value("reason", "apoptosis")}
                    };
                    routing::publish(pub, term_event);

                    std::cout << "[CEREBRAL] APOPTOSIS: " << target
                              << " terminated." << std::endl;
                } else {
                    std::cout << "[CEREBRAL] APOPTOSIS: " << target
                              << " not found or already dead." << std::endl;
                }
            }
        }
    }

    void monitor_lobes() {
        int status = 0;
        pid_t pid;

        // Reap all terminated children (non-blocking)
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            auto it = pid_to_name.find(pid);
            if (it == pid_to_name.end()) continue;

            std::string name = it->second;
            pid_to_name.erase(it);

            auto& rec = registry[name];
            rec.alive = false;
            rec.crash_count++;

            // Determine exit reason
            std::string reason;
            if (WIFSIGNALED(status)) {
                reason = "signal " + std::to_string(WTERMSIG(status));
            } else if (WIFEXITED(status)) {
                reason = "exit code " + std::to_string(WEXITSTATUS(status));
            } else {
                reason = "unknown";
            }

            std::cout << "[CEREBRAL] Lobe " << name << " crashed (" << reason
                      << ") — crash #" << rec.crash_count << "/" << LobeRecord::MAX_RESTARTS << std::endl;

            // Broadcast crash event on the bus
            json crash_event = {
                {"origin", "cerebral_matrix"},
                {"intent", "lobe_crash"},
                {"lobe_name", name},
                {"crash_count", rec.crash_count},
                {"reason", reason}
            };
            routing::publish(pub, crash_event);

            if (rec.crash_count >= LobeRecord::MAX_RESTARTS) {
                std::cout << "[CEREBRAL] Lobe " << name << " exceeded max restarts. Marked DEAD." << std::endl;

                json death_event = {
                    {"origin", "cerebral_matrix"},
                    {"intent", "lobe_death"},
                    {"lobe_name", name},
                    {"total_crashes", rec.crash_count}
                };
                routing::publish(pub, death_event);
            } else {
                // Attempt restart with backoff
                std::cout << "[CEREBRAL] Scheduling restart for " << name
                          << " (cooldown: " << rec.restart_cooldown_sec() << "s)" << std::endl;
                spawn_lobe(name, rec.path);
            }
        }
    }
};

} // namespace neuroswarm

int main() {
    // Strip desktop session variables — prevents polkit auth dialogs
    // and GVFS "browsing network" windows when fork()'ing child lobes
    unsetenv("DBUS_SESSION_BUS_ADDRESS");
    unsetenv("DBUS_SYSTEM_BUS_ADDRESS");
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    setenv("GIO_USE_VFS", "local", 1);
    setenv("SSH_ASKPASS_REQUIRE", "never", 1);

    // Detach from desktop session so systemd doesn't track our forks
    setsid();

    // Become subreaper — adopted orphans reparent to us, not systemd
    prctl(PR_SET_CHILD_SUBREAPER, 1);

    neuroswarm::CerebralMatrix matrix;
    matrix.awaken();
    return 0;
}
