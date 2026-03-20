
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>

using json = nlohmann::json;

// Auto-generated specialist lobe for domain: compilation
// Created by BasalGanglia neurogenesis engine.

class CompilationSpecialist {
public:
    CompilationSpecialist(const std::string& pub_addr = "tcp://localhost:5555",
                const std::string& sub_addr = "tcp://localhost:5556")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub) {

        pub.connect(pub_addr);
        sub.connect(sub_addr);
        routing::subscribe(sub, {"execution_request", "execution_result"});

        mkdir("./data", 0755);
        load_cache();

        std::cout << "[COMPILATION_SPECIALIST] Specialist lobe online for domain: compilation" << std::endl;
    }

    void start() {
        auto last_report = std::chrono::steady_clock::now();

        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else {
                try {
                    std::string intent = j.value("intent", "");

                    if (intent == "execution_request") {
                        handle_request(j);
                    } else if (intent == "execution_result") {
                        handle_result(j);
                    }
                } catch (...) {}
            }

            // Publish report every 5 minutes
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_report).count();
            if (elapsed >= 5) {
                publish_report();
                last_report = now;
            }
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;

    int handled = 0;
    int domain_success = 0;
    int domain_failure = 0;
    std::vector<std::string> cached_commands;
    static constexpr const char* CACHE_PATH = "./data/specialist_compilation.json";

    // Domain keywords for filtering
    const std::vector<std::string> domain_keywords = {"cmake", "make ", "gcc", "g++"};

    bool is_my_domain(const std::string& cmd) {
        std::string lower = cmd;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (auto& kw : domain_keywords) {
            if (lower.find(kw) != std::string::npos) return true;
        }
        return false;
    }

    void handle_request(const json& j) {
        std::string cmd = j.value("command", "");
        if (!is_my_domain(cmd)) return;

        handled++;

        // Publish pre-validation advice
        json advice = {
            {"origin", "COMPILATION_SPECIALIST"},
            {"intent", "specialist_advice"},
            {"domain", "compilation"},
            {"command", cmd},
            {"pre_validations", json::array({"test -d build || echo 'NO_BUILD_DIR'", "which g++ || echo 'NO_COMPILER'"})},
            {"cached_alternatives", get_cached_alternatives()}
        };
        routing::publish(pub, advice);
    }

    void handle_result(const json& j) {
        std::string cmd = j.value("command", "");
        if (!is_my_domain(cmd)) return;

        std::string status = j.value("status", "");
        if (status == "success") {
            domain_success++;
            // Cache novel successful commands
            if (cmd.size() <= 200 && std::find(cached_commands.begin(), cached_commands.end(), cmd) == cached_commands.end()) {
                cached_commands.push_back(cmd);
                if (cached_commands.size() > 50) cached_commands.erase(cached_commands.begin());
                save_cache();
            }
        } else {
            domain_failure++;
        }
    }

    json get_cached_alternatives() {
        json alts = json::array();
        // Return up to 3 most recent cached successes
        int start = std::max(0, (int)cached_commands.size() - 3);
        for (int i = start; i < (int)cached_commands.size(); i++) {
            alts.push_back(cached_commands[i]);
        }
        return alts;
    }

    void publish_report() {
        int total = domain_success + domain_failure;
        float rate = total > 0 ? (float)domain_success / (float)total : 0.0f;

        json report = {
            {"origin", "COMPILATION_SPECIALIST"},
            {"intent", "specialist_report"},
            {"domain", "compilation"},
            {"success_rate", rate},
            {"handled", handled},
            {"cached_count", (int)cached_commands.size()},
            {"total_tracked", total}
        };
        routing::publish(pub, report);

        std::cout << "[COMPILATION_SPECIALIST] Report: rate=" << (int)(rate * 100)
                  << "% handled=" << handled << " cached=" << cached_commands.size() << std::endl;
    }

    void load_cache() {
        std::ifstream f(CACHE_PATH);
        if (!f.is_open()) return;
        try {
            json doc;
            f >> doc;
            if (doc.contains("commands") && doc["commands"].is_array()) {
                for (auto& c : doc["commands"]) cached_commands.push_back(c.get<std::string>());
            }
        } catch (...) {}
    }

    void save_cache() {
        json doc = {{"commands", cached_commands}};
        std::ofstream f(CACHE_PATH);
        if (f.is_open()) f << doc.dump(2);
    }
};

int main() {
    CompilationSpecialist lobe;
    lobe.start();
    return 0;
}
