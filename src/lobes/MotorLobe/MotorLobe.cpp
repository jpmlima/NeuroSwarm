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
                try {
                    if (raw[0] != '{') continue;
                    auto j = json::parse(raw);
                    
                    if (j.value("intent", "") == "execution_request") {
                        std::string cmd = j.value("command", "");
                        int exit_code = 0;
                        std::string out = execute(cmd, exit_code);
                        
                        json resp = {
                            {"cid", j.value("cid", "unknown")},
                            {"origin", "motor_cortex"},
                            {"intent", "execution_result"},
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
