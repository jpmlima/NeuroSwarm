#include <zmq.hpp>
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;

int main() {
    zmq::context_t ctx(1);
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");
    
    std::cout << "[TEST] Connected to Thalamus. Waiting to stabilize..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    json req = {
        {"intent", "genesis_request"},
        {"name", "DUMMY"},
        {"source_path", "tests/dummy_lobe.cpp"},
        {"output_path", "build/libdummy_lobe.so"}
    };
    
    std::string s = req.dump();
    zmq::message_t msg(s.size());
    memcpy(msg.data(), s.c_str(), s.size());
    pub.send(msg, zmq::send_flags::none);

    std::cout << "[TEST] Genesis request dispatched. MotorLobe should now compile DummyLobe and CerebralMatrix should inject it." << std::endl;
    
    // Sleep to give the system time to compile and load the lobe before exiting test
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::cout << "[TEST] Test concluded." << std::endl;
    
    return 0;
}
