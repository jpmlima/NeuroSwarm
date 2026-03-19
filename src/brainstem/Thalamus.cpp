#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <chrono>

// NeuroSwarm Thalamus — Specialised Routing via XPUB/XSUB proxy.
//
// Biological analogue: the thalamus routes sensory signals to specific
// cortical areas rather than broadcasting everything everywhere.
//
// ZMQ XPUB/XSUB enables subscription forwarding: when a lobe subscribes
// to "critic_validate ", that subscription propagates through the proxy
// to the XSUB side. Publishers only send messages that match at least
// one downstream subscriber's filter — zero-copy at the transport layer.
//
// Port 5555 (XSUB): lobes publish messages here (connect PUB → 5555)
// Port 5556 (XPUB): lobes subscribe here (connect SUB → 5556)

int main() {
    zmq::context_t ctx(1);

    // Frontend: receives published messages from all lobes
    zmq::socket_t frontend(ctx, zmq::socket_type::xsub);
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

    // Backend: distributes messages to subscribing lobes
    zmq::socket_t backend(ctx, zmq::socket_type::xpub);
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

    std::cout << "[THALAMUS] Specialised Routing active (XPUB/XSUB)." << std::endl;

    // zmq::proxy handles bidirectional forwarding:
    //   frontend→backend: data messages
    //   backend→frontend: subscription messages
    zmq::proxy(frontend, backend);

    return 0;
}
