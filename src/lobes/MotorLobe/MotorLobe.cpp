#include <zmq.hpp>
#include <string>
#include <iostream>
#include <memory>
#include <array>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace neuroswarm {
class MotorLobe {
public:
    MotorLobe(const std::string& pub_addr = "tcp://localhost:5555", 
              const std::string& sub_addr = "tcp://localhost:5556") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {
        
        pub.connect(pub_addr);
        sub.connect(sub_addr);
        sub.set(zmq::sockopt::subscribe, ""); 

        std::cout << "[MOTOR] Cortex online." << std::endl;
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                if (raw.find("genesis_request") != std::string::npos) {
                    std::cout << "[MOTOR] Raw genesis message: " << raw << std::endl;
                }
                try {
                    if (raw[0] != '{') continue;
                    auto j = json::parse(raw);
                    
                    if (j.value("intent", "") == "execution_request") {
                        std::string cmd = j.value("command", "");
                        std::string mode = j.value("mode", "reality"); // "reality" or "dream"
                        std::string cid = j.value("cid", "unknown");
                        
                        int exit_code = 0;
                        std::string out;

                        if (mode == "dream") {
                            std::string dream_path = "./data/dreams/" + cid;
                            std::filesystem::create_directories(dream_path);
                            out = execute("cd " + dream_path + " && " + cmd, exit_code);
                            std::cout << "[MOTOR] DREAM SEQUENCE executed for CID: " << cid << std::endl;
                        } else if (mode == "neuro_surgery") {
                            std::cout << "[MOTOR] WARNING: NEURO-SURGERY INITIATED. MODIFYING OWN SOURCE CODE." << std::endl;
                            // Neuro-surgery command expects 'cmd' to be a valid bash sequence that writes to src/lobes and compiles.
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
                        std::string output = j.value("output_path", "build/lib" + name + ".so");
                        
                        // ABSOLUTE ROBUSTNESS: Include root paths
                        std::string cmd = "g++ -shared -fPIC -std=c++17 -I./include -I./external/llama.cpp/vendor/ " + source + " -o " + output + " -lzmq";
                        std::cout << "[MOTOR] Genesis compiling: " << cmd << std::endl;
                        
                        int exit_code = 0;
                        std::string out = execute(cmd, exit_code);
                        std::cout << "[MOTOR] Genesis compilation exit_code: " << exit_code << ", output: " << out << std::endl;
                        
                        if (exit_code == 0) {
                            json inject = {
                                {"intent", "inject_lobe"},
                                {"name", name},
                                {"path", output}
                            };
                            dispatch(inject);
                            out += "\n[MOTOR] Genesis successful. Injection signal sent to Matrix.";
                        }
                        
                        json resp = {
                            {"origin", "motor_cortex"},
                            {"intent", "genesis_result"},
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
        std::string s = data.dump();
        zmq::message_t m(s.size()); memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);
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
int main() { neuroswarm::MotorLobe().start(); return 0; }
