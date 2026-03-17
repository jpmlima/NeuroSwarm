#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <map>
#include <signal.h>
#include <chrono>
#include <thread>

namespace neuroswarm {

struct LobeProcess {
    std::string name;
    std::string path;
    pid_t pid = 0;
    bool active = false;
};

class CerebralMatrix {
public:
    CerebralMatrix() {
        lobes["THALAMUS"] = {"THALAMUS", "./thalamus"};
        lobes["SYNAPTIC"] = {"SYNAPTIC", "./synaptic_controller"};
        lobes["AMYGDALA"] = {"AMYGDALA", "./amygdala"};
        lobes["MOTOR"] = {"MOTOR", "./motor_lobe"};
        lobes["HIPPOCAMPUS"] = {"HIPPOCAMPUS", "./hippocampus"};
        lobes["WERNICKE"] = {"WERNICKE", "./wernicke_lobe"};
        lobes["VISUAL"] = {"VISUAL", "./visual_lobe"};
        lobes["EXECUTIVE"] = {"EXECUTIVE", "./frontal_executive"};
    }

    void awaken() {
        std::cout << "[MATRIX] Initializing Neural Fabric..." << std::endl;
        
        for (auto& pair : lobes) {
            start_lobe(pair.second);
            if (pair.first == "THALAMUS") std::this_thread::sleep_for(std::chrono::seconds(2));
            if (pair.first == "SYNAPTIC") std::this_thread::sleep_for(std::chrono::seconds(40)); // VRAM Buffer
        }

        monitor_loop();
    }

private:
    std::map<std::string, LobeProcess> lobes;

    void start_lobe(LobeProcess& lobe) {
        pid_t pid = fork();

        if (pid == 0) { // Child process
            std::cout << "[MATRIX] Lobe " << lobe.name << " firing..." << std::endl;
            execl(lobe.path.c_str(), lobe.path.c_str(), (char*)NULL);
            exit(1);
        } else if (pid > 0) {
            lobe.pid = pid;
            lobe.active = true;
        }
    }

    void monitor_loop() {
        while (true) {
            int status;
            pid_t exited_pid = waitpid(-1, &status, WNOHANG);

            if (exited_pid > 0) {
                for (auto& pair : lobes) {
                    if (pair.second.pid == exited_pid) {
                        std::cerr << "[MATRIX] ALERT: Lobe " << pair.first << " suffered a lesion! Restarting..." << std::endl;
                        start_lobe(pair.second);
                    }
                }
            }

            // Homeostatic Pulse
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--awaken") {
        neuroswarm::CerebralMatrix matrix;
        matrix.awaken();
    } else {
        std::cout << "Usage: ./CerebralMatrix --awaken" << std::endl;
    }
    return 0;
}
