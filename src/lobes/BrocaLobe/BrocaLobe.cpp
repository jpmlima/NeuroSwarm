/**
 * @file BrocaLobe.cpp
 * @brief Linguistic Synthesis Engine for the NeuroSwarm Architecture.
 * 
 * Transforms structured internal Engram Traces into natural language.
 */

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace neuroswarm {

class BrocaLobe {
public:
    BrocaLobe(const std::string& bus_addr = "tcp://localhost:5555") 
        : ctx(1), synapse(ctx, zmq::socket_type::dealer) {
        
        synapse.set(zmq::sockopt::routing_id, "broca_lobe");
        synapse.connect(bus_addr);
        std::cout << "[BROCA LOBE] Linguistic Synthesis online. Connected to Nervous Bus." << std::endl;
    }

    void start_resonance() {
        while (true) {
            zmq::message_t msg;
            auto res = synapse.recv(msg, zmq::recv_flags::none);
            if (res) {
                std::string payload(static_cast<char*>(msg.data()), msg.size());
                articulate(payload);
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t synapse;

    void articulate(const std::string& raw_stimulus) {
        try {
            json stimulus = json::parse(raw_stimulus);
            std::string intent = stimulus.value("intent", "idle");

            if (intent == "articulate_response") {
                std::cout << "[BROCA LOBE] Synthesizing speech from Engram..." << std::endl;
                
                // Logic Flow:
                // 1. Send stimulus to ModelManager (via Thalamus or direct API)
                // 2. Apply 'Personality' LoRA adapter
                // 3. Generate human-readable string
                
                std::string final_text = "Simulated high-fidelity response based on internal state.";
                
                json output = {
                    {"cid", stimulus.value("cid", "unknown")},
                    {"origin", "broca_lobe"},
                    {"target", "vocal_tract"},
                    {"text", final_text}
                };

                broadcast(output);
            }
        } catch (std::exception& e) {
            std::cerr << "[BROCA LOBE] Synthesis Error: " << e.what() << std::endl;
        }
    }

    void broadcast(const json& data) {
        std::string payload = data.dump();
        zmq::message_t z_msg(payload.size());
        memcpy(z_msg.data(), payload.c_str(), payload.size());
        synapse.send(z_msg, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::BrocaLobe broca;
    broca.start_resonance();
    return 0;
}
