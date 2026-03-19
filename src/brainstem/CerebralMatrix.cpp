#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>

namespace neuroswarm {

struct Lobe {
    std::string name;
    std::string path;
};

class CerebralMatrix {
public:
    CerebralMatrix() {
        const std::string base = "/home/xenomai/Documents/NeuroSwarm/build/";
        lobes.push_back({"THALAMUS", base + "thalamus"});
        lobes.push_back({"SYNAPTIC", base + "synaptic_controller"});
        lobes.push_back({"MOTOR", base + "motor_lobe"});
        lobes.push_back({"EXECUTIVE", base + "frontal_executive"});
        lobes.push_back({"AMYGDALA", base + "amygdala"});
        lobes.push_back({"WERNICKE", base + "wernicke_lobe"});
        lobes.push_back({"VISUAL", base + "visual_lobe"});
        lobes.push_back({"HOMEOSTASIS", base + "homeostasis"});
        lobes.push_back({"METACOGNITION", base + "metacognition"});
        lobes.push_back({"CRITIC", base + "critic_lobe"});
        lobes.push_back({"VISUALIZER", base + "visualizer"});
        lobes.push_back({"REM_ENGINE", base + "rem_engine"});
        lobes.push_back({"CHRONOS", base + "chronos_lobe"});
        lobes.push_back({"STATISTICS", base + "statistics_lobe"});
    }

    void awaken() {
        std::cout << "[MATRIX] Starting all lobes..." << std::endl;
        signal(SIGHUP, SIG_IGN);
        signal(SIGCHLD, SIG_IGN); // Automatically reap zombie child processes; avoids manual waitpid bookkeeping

        for (auto& lobe : lobes) {
            pid_t pid = fork();
            if (pid == 0) {
                execl(lobe.path.c_str(), lobe.path.c_str(), (char*)NULL);
                exit(1);
            } else {
                std::cout << "[MATRIX] Launched " << lobe.name << " (PID: " << pid << ")" << std::endl;
            }
        }

        // Keep the supervisor process alive; child lobes run independently as detached processes
        while (true) {
            std::this_thread::sleep_for(std::chrono::hours(24));
        }
    }

private:
    std::vector<Lobe> lobes;
};

} // namespace neuroswarm

int main() {
    neuroswarm::CerebralMatrix matrix;
    matrix.awaken();
    return 0;
}
