#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

int main() {
    zmq::context_t ctx(1);
    
    // Pub to send execution requests
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");

    // Sub to receive results
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    sub.set(zmq::sockopt::subscribe, "");

    std::cout << "[TEST] Starting Dream Sandbox Verification..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::string cid = "test_dream_456";
    std::string test_file = "dream_test_file.tmp";
    
    // 1. Send Dream Execution Request
    json dream_req = {
        {"cid", cid},
        {"origin", "test_suite"},
        {"intent", "execution_request"},
        {"command", "touch " + test_file},
        {"mode", "dream"}
    };
    
    std::string s = dream_req.dump();
    zmq::message_t m(s.size()); memcpy(m.data(), s.c_str(), s.size());
    pub.send(m, zmq::send_flags::none);
    std::cout << "[TEST] Step 1: Sent 'touch' command in DREAM mode." << std::endl;

    // 2. Wait for result
    bool success = false;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::dontwait)) {
            auto j = json::parse(static_cast<char*>(msg.data()), static_cast<char*>(msg.data()) + msg.size());
            if (j.value("cid", "") == cid && j.value("intent", "") == "execution_result") {
                std::cout << "[TEST] Received execution result from Motor Lobe." << std::endl;
                success = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!success) {
        std::cerr << "[TEST] FAILED: Motor Lobe did not respond." << std::endl;
        return 1;
    }

    // 3. Verify Isolation
    fs::path dream_file_path = fs::path("./data/dreams") / cid / test_file;
    fs::path root_file_path = fs::path(".") / test_file;

    if (fs::exists(dream_file_path)) {
        std::cout << "[TEST] SUCCESS: File found in Dream Sandbox: " << dream_file_path << std::endl;
    } else {
        std::cerr << "[TEST] FAILED: File NOT found in Dream Sandbox." << std::endl;
        return 1;
    }

    if (fs::exists(root_file_path)) {
        std::cerr << "[TEST] FAILED: File leaked into Reality (Root directory)!" << std::endl;
        fs::remove(root_file_path); // Cleanup
        return 1;
    } else {
        std::cout << "[TEST] SUCCESS: Reality is clean. Isolation verified." << std::endl;
    }

    // Cleanup dream
    fs::remove_all("./data/dreams/" + cid);
    std::cout << "[TEST] Cleanup complete. Dream Sandbox is functional." << std::endl;

    return 0;
}
