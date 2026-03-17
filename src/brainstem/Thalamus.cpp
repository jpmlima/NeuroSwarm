#include <zmq.hpp>
#include <iostream>

int main() {
    zmq::context_t ctx(1);
    
    zmq::socket_t frontend(ctx, zmq::socket_type::sub); 
    frontend.bind("tcp://*:5555");
    frontend.set(zmq::sockopt::subscribe, "");

    zmq::socket_t backend(ctx, zmq::socket_type::pub);  
    backend.bind("tcp://*:5556");
    
    std::cout << "[THALAMUS] Neural Bus active." << std::endl;
    
    while (true) {
        zmq::message_t msg;
        if (frontend.recv(msg, zmq::recv_flags::none)) {
            backend.send(msg, zmq::send_flags::none);
        }
    }
    
    return 0;
}
