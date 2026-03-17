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

    std::cout << "[TEST] Initiating Sensory Expansion Test (Vision & Hearing)..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 1. Simulate Visual Input (e.g., a webcam frame or screenshot)
    json visual_req = {
        {"cid", "visual_test_123"},
        {"origin", "test_suite"},
        {"intent", "sensory_visual_input"},
        {"image_path", "/tmp/screenshot.png"}
    };
    
    std::string v_req = visual_req.dump();
    zmq::message_t z_v_req(v_req.size());
    memcpy(z_v_req.data(), v_req.c_str(), v_req.size());
    pub.send(z_v_req, zmq::send_flags::none);
    std::cout << "[TEST] Sent simulated image path to Visual Lobe." << std::endl;

    // 2. Simulate Auditory Input (e.g., a microphone recording)
    json audio_req = {
        {"cid", "audio_test_123"},
        {"origin", "test_suite"},
        {"intent", "sensory_audio_input"},
        {"audio_path", "/tmp/voice_command.wav"}
    };
    
    std::string a_req = audio_req.dump();
    zmq::message_t z_a_req(a_req.size());
    memcpy(z_a_req.data(), a_req.c_str(), a_req.size());
    pub.send(z_a_req, zmq::send_flags::none);
    std::cout << "[TEST] Sent simulated audio path to Auditory Lobe." << std::endl;

    int success_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        zmq::message_t msg;
        if (sub.recv(msg, zmq::recv_flags::dontwait)) {
            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                auto j = json::parse(raw);
                if (j.value("origin", "") == "visual_lobe" && j.value("intent", "") == "user_input") {
                    std::cout << "[TEST] SUCCESS! Visual Lobe decoded image into: " << j.value("text", "") << std::endl;
                    success_count++;
                }
                if (j.value("origin", "") == "auditory_lobe" && j.value("intent", "") == "user_input") {
                    std::cout << "[TEST] SUCCESS! Auditory Lobe transcribed audio into: " << j.value("text", "") << std::endl;
                    success_count++;
                }
                
                if (success_count == 2) break;
            } catch (...) {}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (success_count != 2) {
        std::cerr << "[TEST] FAILED: Sensory lobes did not process inputs correctly." << std::endl;
        return 1;
    }

    return 0;
}
