#include <iostream>
#include <string>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;

int main() {
    zmq::context_t ctx(1);
    
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.connect("tcp://localhost:5555");

    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("tcp://localhost:5556");
    routing::subscribe(sub, {"inference_result", "execution_result"});

    std::cout << "\033[1;32m" << "=== NEUROSWARM AGI TERMINAL ===" << "\033[0m" << std::endl;
    std::cout << "Connected to Thalamus. Ready for neural stimulus." << std::endl;
    std::cout << "Type your message and press Enter (or 'exit' to quit)." << std::endl;

    std::string input;
    while (true) {
        std::cout << "\n\033[1;34mYOU > \033[0m";
        if (!std::getline(std::cin, input) || input == "exit") break;
        if (input.empty()) continue;

        std::string cid = "user_" + std::to_string(std::time(nullptr));

        // Send stimulus to the brain
        json req = {
            {"cid", cid},
            {"origin", "broca_terminal"},
            {"intent", "stimulus"},
            {"text", input}
        };

        routing::publish(pub, req);

        std::cout << "\033[1;33m[BRAIN IS THINKING...]\033[0m" << std::flush;

        bool answered = false;
        auto start_time = std::chrono::steady_clock::now();

        while (!answered) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (!j.is_null()) {
                try {
                    std::string intent = j.value("intent", "");
                    std::string origin = j.value("origin", "");
                    std::string text = j.value("text", "");

                    if (intent == "inference_result") {
                        std::cout << "\r\033[K"; // Clear the "Thinking" line
                        std::cout << "\033[1;35m[" << origin << "]: \033[0m" << text << std::endl;
                        if (j.value("cid", "") == cid) answered = true;
                    }
                    else if (intent == "execution_result") {
                        std::cout << "\033[1;32m[MOTOR]: \033[0m" << text << std::endl;
                    }
                } catch (...) {}
            }

            if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(60)) {
                std::cout << "\r\033[K" << "\033[1;31m[TIMEOUT]: The Brain is too busy or silent.\033[0m" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    return 0;
}
