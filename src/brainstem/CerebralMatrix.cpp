#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <Heartbeat.hpp>

using json = nlohmann::json;

namespace neuroswarm {

class CerebralMatrix {
public:
    CerebralMatrix()
        : ctx(1), pub(ctx, zmq::socket_type::pub) {

        pub.connect("tcp://localhost:5555");

        const std::string base = "/home/xenomai/Documents/NeuroSwarm/build/";
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
    }

    void awaken() {
        std::cout << "[CEREBRAL] Starting all lobes..." << std::endl;
        signal(SIGHUP, SIG_IGN);
        // Phase 2: Do NOT set SIGCHLD to SIG_IGN — we need waitpid() for crash detection
        signal(SIGCHLD, SIG_DFL);

        for (auto& lobe : init_lobes) {
            spawn_lobe(lobe.name, lobe.path);
        }

        std::cout << "[CEREBRAL] All lobes launched. Entering monitoring loop." << std::endl;

        // Phase 2: Self-Preservation — monitor child processes for crashes
        while (true) {
            monitor_lobes();
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
    std::vector<InitLobe> init_lobes;
    std::map<std::string, LobeRecord> registry;  // name → LobeRecord
    std::map<pid_t, std::string> pid_to_name;     // pid → lobe name (reverse lookup)

    void spawn_lobe(const std::string& name, const std::string& path) {
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
    neuroswarm::CerebralMatrix matrix;
    matrix.awaken();
    return 0;
}
