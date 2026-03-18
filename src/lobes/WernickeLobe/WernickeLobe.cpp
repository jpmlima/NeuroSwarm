#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <vector>

using json = nlohmann::json;

namespace neuroswarm {

class WernickeLobe {
public:
    WernickeLobe(const std::string& pub_addr = "tcp://localhost:5555", 
                 const std::string& sub_addr = "tcp://localhost:5556") 
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {
        
        pub.connect(pub_addr);
        sub.connect(sub_addr);
        sub.set(zmq::sockopt::subscribe, ""); // Subscribe to all neural bus events

        std::cout << "[WERNICKE] Semantic Interpreter Online." << std::endl;
    }

    void start() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                std::string raw(static_cast<char*>(msg.data()), msg.size());
                try {
                    if (raw.empty() || raw[0] != '{') continue;
                    auto j = json::parse(raw);
                    
                    std::string origin = j.value("origin", "");
                    std::string intent = j.value("intent", "");

                    // Intercept raw user input for semantic processing
                    if (origin == "broca_lobe" && intent == "inference_request") {
                        // If the message is already a direct inference request, bypass NLU processing
                        continue;
                    }
                    
                    if (origin == "broca_lobe" && intent == "user_input") {
                        process_semantics(j);
                    }
                } catch (...) {}
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    void process_semantics(const json& data) {
        std::string cid = data.value("cid", "global");
        std::string text = data.value("text", "");

        std::cout << "[WERNICKE] Interpreting: '" << text << "'" << std::endl;

        // Specialised NLU prompt for intent classification and entity extraction
        std::string nlu_prompt =
            "<|system|>\n"
            "NeuroSwarm Wernicke Lobe (NLU Core).\n"
            "Extract intent and entities from user input. Respond ONLY with raw JSON:\n"
            "{\n"
            "  \"intent\": \"STATED_INTENT\",\n"
            "  \"target\": \"OBJECT_OF_ACTION\",\n"
            "  \"urgency\": \"LOW|MEDIUM|HIGH\",\n"
            "  \"summary\": \"Concise semantic summary\"\n"
            "}\n"
            "<|end|>\n"
            "<|user|>\n" + text + "<|end|>\n"
            "<|assistant|>\n";

        json req = {
            {"cid", cid},
            {"origin", "wernicke_lobe"},
            {"intent", "inference_request"},
            {"adapter", "nlu_specialist"},
            {"text", nlu_prompt},
            {"is_internal", true} // Flag consumed by the Synaptic Controller to suppress echo routing
        };

        dispatch(req);
    }

    void dispatch(const json& data) {
        std::string s = data.dump();
        zmq::message_t msg(s.size());
        memcpy(msg.data(), s.c_str(), s.size());
        pub.send(msg, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main() {
    neuroswarm::WernickeLobe wernicke;
    wernicke.start();
    return 0;
}
