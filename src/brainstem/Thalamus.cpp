#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    zmq::context_t ctx(1);
    
    zmq::socket_t frontend(ctx, zmq::socket_type::sub); 
    frontend.set(zmq::sockopt::linger, 0);
    
    while (true) {
        try {
            frontend.bind("tcp://0.0.0.0:5555");
            break;
        } catch (const zmq::error_t& e) {
            std::cerr << "[THALAMUS] Port 5555 busy, retrying in 2s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    frontend.set(zmq::sockopt::subscribe, "");

    zmq::socket_t backend(ctx, zmq::socket_type::pub);  
    backend.set(zmq::sockopt::linger, 0);
    while (true) {
        try {
            backend.bind("tcp://0.0.0.0:5556");
            break;
        } catch (const zmq::error_t& e) {
            std::cerr << "[THALAMUS] Port 5556 busy, retrying in 2s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    std::cout << "[THALAMUS] Neural Bus active." << std::endl;
    
    while (true) {
        zmq::message_t msg;
        if (frontend.recv(msg, zmq::recv_flags::none)) {
            backend.send(msg, zmq::send_flags::none);
        }
    }
    
    return 0;
}
