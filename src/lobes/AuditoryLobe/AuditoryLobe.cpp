#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using json = nlohmann::json;

namespace neuroswarm {

class AuditoryLobe {
public:
    AuditoryLobe(const std::string& thalamus_ip = "localhost") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {
        
        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, ""); 

        std::cout << "[AUDITORY] Cochlear processor online. Awaiting soundwaves." << std::endl;
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    if (raw.empty() || raw[0] != '{') continue;
                    auto j = json::parse(raw);
                    
                    if (j.value("intent", "") == "sensory_audio_input") {
                        std::string audio_path = j.value("audio_path", "");
                        std::string cid = j.value("cid", "audio_" + std::to_string(std::time(nullptr)));
                        
                        std::cout << "[AUDITORY] Processing soundwave from: " << audio_path << std::endl;
                        
                        // Here, the system would interface with Whisper.cpp
                        // For architectural completeness, we simulate the transcription result.
                        // In a real scenario, this blocks while whisper decodes.
                        
                        std::string transcription = "Simulated transcription: User is asking to check system status.";

                        json req = {
                            {"cid", cid},
                            {"origin", "auditory_lobe"},
                            {"intent", "user_input"},
                            {"text", transcription}
                        };
                        dispatch(req);
                    }
                } catch (...) {}
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    void dispatch(const json& data) {
        std::string s = data.dump();
        zmq::message_t m(s.size()); memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[i+1];
    }
    neuroswarm::AuditoryLobe auditory(ip);
    auditory.start();
    return 0;
}
