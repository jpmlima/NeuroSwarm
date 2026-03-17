#include "ModelManager.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;

int main() {
    try {
        neuroswarm::ModelManager brain("/home/xenomai/Documents/NeuroSwarm/models/qwen2.5-1.5b-instruct-q4_k_m.gguf");
        
        zmq::context_t ctx(1);
        zmq::socket_t sub(ctx, zmq::socket_type::sub);
        sub.connect("tcp://localhost:5556");
        sub.set(zmq::sockopt::subscribe, ""); 

        zmq::socket_t pub(ctx, zmq::socket_type::pub);
        pub.connect("tcp://localhost:5555");

        std::cout << "[BRAIN] Synaptic Controller ready. LoRA Multiplexing Enabled." << std::endl;

        while (true) {
            zmq::message_t msg;
            if (!sub.recv(msg, zmq::recv_flags::none)) continue;

            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                if (raw.empty() || raw[0] != '{') continue;
                auto j = json::parse(raw);
                
                if (j.value("origin", "") != "synaptic_controller" && j.contains("text")) {
                    std::string prompt = j["text"];
                    std::string cid = j.value("cid", "unknown");
                    
                    // Dynamic Adapter Selection: Use specific adapter if provided
                    std::string adapter = j.value("adapter", "default");
                    
                    std::string response = brain.fire(adapter, prompt);
                    
                    json resp = {
                        {"cid", cid},
                        {"origin", "synaptic_controller"},
                        {"intent", "inference_result"},
                        {"text", response}
                    };
                    
                    std::string s_resp = resp.dump();
                    zmq::message_t z_resp(s_resp.size());
                    memcpy(z_resp.data(), s_resp.c_str(), s_resp.size());
                    pub.send(z_resp, zmq::send_flags::none);
                }
            } catch (...) {}
        }
    } catch (const std::exception& e) {
        std::cerr << "FATAL BRAIN ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
