/**
 * @file Amygdala.cpp
 * @brief Emotional Gating and Priority Engine.
 * 
 * Analyzes incoming stimuli and assigns priority tags. 
 * Can trigger an Amygdala Hijack (P5) to halt background tasks.
 */

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>
#include <algorithm>

using json = nlohmann::json;

namespace neuroswarm {

class Amygdala {
public:
    Amygdala(const std::string& bus_addr = "tcp://localhost:5555") 
        : ctx(1), bus(ctx, zmq::socket_type::dealer) {
        
        bus.set(zmq::sockopt::routing_id, "amygdala");
        bus.connect(bus_addr);
        std::cout << "[AMYGDALA] Priority Gating online. Connected to Nervous Bus." << std::endl;
    }

    void start_gating() {
        while (true) {
            zmq::message_t msg;
            auto res = bus.recv(msg, zmq::recv_flags::none);
            if (res) {
                std::string payload(static_cast<char*>(msg.data()), msg.size());
                gate_stimulus(payload);
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t bus;

    void gate_stimulus(const std::string& raw_data) {
        try {
            json stimulus = json::parse(raw_data);
            std::string content = stimulus.dump();
            std::transform(content.begin(), content.end(), content.begin(), ::tolower);

            int priority = 1; // Default: P1 Normal

            // Heuristic Gating Engine (Hardcore speed)
            if (content.find("error") != std::string::npos || content.find("fail") != std::string::npos) {
                priority = 3; // P3 Alert
            }
            if (content.find("critical") != std::string::npos || content.find("emergency") != std::string::npos) {
                priority = 4; // P4 Critical
            }
            if (content.find("terminate") != std::string::npos || content.find("overwrite") != std::string::npos) {
                priority = 5; // P5 Hijack
            }

            // Tag and re-broadcast
            stimulus["priority"] = priority;
            stimulus["origin"] = "amygdala";
            
            if (priority == 5) {
                std::cout << "[AMYGDALA] !!! HIJACK DETECTED !!!" << std::endl;
                stimulus["intent"] = "emergency_halt";
            }

            std::string tagged = stimulus.dump();
            zmq::message_t z_msg(tagged.size());
            memcpy(z_msg.data(), tagged.c_str(), tagged.size());
            bus.send(z_msg, zmq::send_flags::none);

        } catch (std::exception& e) {
            std::cerr << "[AMYGDALA] Gating Error: " << e.what() << std::endl;
        }
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::Amygdala amygdala;
    amygdala.start_gating();
    return 0;
}
