#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using json = nlohmann::json;

int main() {
    zmq::context_t ctx(1);
    
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, "");

    std::cout << "[TEST] Waiting 35 seconds for Executive to get bored and trigger Epistemic Drive..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    bool curiosity_triggered = false;

    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(45)) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::dontwait)) {
            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                auto j = json::parse(raw);
                std::string origin = j.value("origin", "");
                std::string intent = j.value("intent", "");
                std::string cid = j.value("cid", "");

                if (origin == "frontal_executive" && cid.find("epistemic_") == 0) {
                    std::cout << "\n[TEST] SUCCESS! Epistemic Drive triggered!" << std::endl;
                    std::cout << "[TEST] Self-Assigned CID: " << cid << std::endl;
                    curiosity_triggered = true;
                    break;
                }
            } catch (...) {}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!curiosity_triggered) {
        std::cerr << "\n[TEST] FAILED: System did not generate an intrinsic goal." << std::endl;
    }

    return curiosity_triggered ? 0 : 1;
}
