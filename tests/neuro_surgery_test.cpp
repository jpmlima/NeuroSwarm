#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using json = nlohmann::json;

int main() {
    zmq::context_t ctx(1);
    
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");

    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, "");

    std::cout << "[TEST] Initiating Neuro-Surgery (Self-Modification) Test..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::string cid = "test_surgery_789";
    
    // Send Neuro-Surgery Execution Request
    // We will just do a harmless echo to a temp file, but the mode will force a recompilation.
    json req = {
        {"cid", cid},
        {"origin", "test_suite"},
        {"intent", "execution_request"},
        {"command", "echo '// surgery test' > src/surgery_test.tmp"},
        {"mode", "neuro_surgery"}
    };
    
    std::string s_req = req.dump();
    zmq::message_t z_req(s_req.size());
    memcpy(z_req.data(), s_req.c_str(), s_req.size());
    pub.send(z_req, zmq::send_flags::none);

    bool success = false;
    auto start_time = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(30)) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::dontwait)) {
            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                auto j = json::parse(raw);
                if (j.value("cid", "") == cid && j.value("intent", "") == "execution_result") {
                    std::string out = j.value("proprioception", "");
                    std::cout << "[TEST] Result received from Motor Lobe:\n" << out << std::endl;
                    
                    if (out.find("Surgery successful. Matrix recompiled.") != std::string::npos) {
                        std::cout << "[TEST] SUCCESS! The system successfully recompiled its own matrix." << std::endl;
                        success = true;
                    }
                    break;
                }
            } catch (...) {}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup
    system("rm -f src/surgery_test.tmp");

    if (!success) {
        std::cerr << "[TEST] FAILED: System failed to modify and recompile itself." << std::endl;
    }

    return success ? 0 : 1;
}
