#include "ModelManager.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;

int main(int argc, char** argv) {
    std::string thalamus_ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) {
            thalamus_ip = argv[i+1];
        }
    }

    try {
        neuroswarm::ModelManager brain("/home/xenomai/Documents/NeuroSwarm/models/qwen2.5-1.5b-instruct-q4_k_m.gguf");
        
        zmq::context_t ctx(1);
        zmq::socket_t sub(ctx, zmq::socket_type::sub);
        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, ""); 

        zmq::socket_t pub(ctx, zmq::socket_type::pub);
        pub.connect("tcp://" + thalamus_ip + ":5555");

        std::cout << "[BRAIN] Synaptic Controller connected to Thalamus at " << thalamus_ip << std::endl;

        while (true) {
            zmq::message_t msg;
            if (!sub.recv(msg, zmq::recv_flags::none)) continue;

            std::string raw(static_cast<char*>(msg.data()), msg.size());
            try {
                if (raw.empty() || raw[0] != '{') continue;
                auto j = json::parse(raw);
                
                std::string intent = j.value("intent", "");
                std::string cid = j.value("cid", "unknown");

                if (j.value("origin", "") != "synaptic_controller" && intent == "embedding_request") {
                    std::string text = j.value("text", "");
                    auto vec = brain.get_embeddings(text);
                    
                    json resp = {
                        {"cid", cid},
                        {"origin", "synaptic_controller"},
                        {"intent", "embedding_result"},
                        {"embedding", vec}
                    };
                    std::string s = resp.dump();
                    zmq::message_t m(s.size()); memcpy(m.data(), s.c_str(), s.size());
                    pub.send(m, zmq::send_flags::none);
                }
                else if (j.value("origin", "") != "synaptic_controller" && j.contains("text")) {
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
