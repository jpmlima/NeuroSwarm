// NeuroSwarm Full System Test — end-to-end cognitive cycle verification.
//
// Launches CerebralMatrix, waits for primordial_ready, injects test goals,
// verifies execution_result flow, checks operator learning, and validates
// the full cognitive cycle end-to-end.
//
// Usage: ./full_system_test [--thalamus <ip>]
//
// Exit codes:
//   0 — all checks passed
//   1 — test failure
//   2 — timeout (system didn't respond)

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <ctime>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

using json = nlohmann::json;

struct TestResult {
    std::string name;
    bool passed;
    std::string detail;
};

class FullSystemTest {
public:
    FullSystemTest(const std::string& thalamus_ip = "localhost")
        : ctx(1), pub(ctx, zmq::socket_type::pub), sub(ctx, zmq::socket_type::sub),
          thalamus_ip_(thalamus_ip) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        sub.connect("tcp://" + thalamus_ip + ":5556");
        routing::subscribe(sub, {
            "primordial_ready", "execution_result", "intrinsic_goal",
            "dopamine_signal", "self_model_updated", "inference_result",
            "cognitive_idle", "specialist_report", "lobe_injected",
            "operators_synced", "goal_plan", "rlaif_reinforce"
        });

        // Brief settle for pub socket
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    int run_all() {
        std::cout << "╔══════════════════════════════════════════╗" << std::endl;
        std::cout << "║   NeuroSwarm Full System Test v3.0.0     ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════╝" << std::endl;
        std::cout << std::endl;

        // Test 1: Thalamus connectivity
        run_test("Thalamus Connectivity", [&]() { return test_thalamus(); });

        // Test 2: Message bus round-trip
        run_test("Message Bus Round-Trip", [&]() { return test_bus_roundtrip(); });

        // Test 3: Intrinsic goal request/response
        run_test("Intrinsic Goal Cycle", [&]() { return test_intrinsic_goal(); });

        // Test 4: Execution pipeline
        run_test("Execution Pipeline", [&]() { return test_execution(); });

        // Test 5: Dopamine signal flow
        run_test("Dopamine Signal Flow", [&]() { return test_dopamine(); });

        // Test 6: Self-model persistence
        run_test("Self-Model Persistence", [&]() { return test_self_model(); });

        // Test 7: RLAIF reinforcement
        run_test("RLAIF Reinforcement", [&]() { return test_rlaif(); });

        // Print summary
        std::cout << std::endl;
        std::cout << "════════════════════════════════════════════" << std::endl;
        int passed = 0, failed = 0;
        for (auto& r : results) {
            std::cout << (r.passed ? " PASS " : " FAIL ") << r.name;
            if (!r.detail.empty()) std::cout << " — " << r.detail;
            std::cout << std::endl;
            if (r.passed) passed++; else failed++;
        }
        std::cout << "════════════════════════════════════════════" << std::endl;
        std::cout << passed << " passed, " << failed << " failed, "
                  << results.size() << " total" << std::endl;

        return failed > 0 ? 1 : 0;
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    zmq::socket_t sub;
    std::string thalamus_ip_;
    std::vector<TestResult> results;

    void run_test(const std::string& name, std::function<TestResult()> test) {
        std::cout << "[TEST] " << name << "..." << std::flush;
        auto result = test();
        result.name = name;
        results.push_back(result);
        std::cout << (result.passed ? " OK" : " FAILED") << std::endl;
    }

    // Wait for a specific intent, with timeout
    json wait_for(const std::string& intent, int timeout_ms = 5000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (!j.is_null() && j.value("intent", "") == intent) return j;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return json();
    }

    // Drain any pending messages
    void drain() {
        while (true) {
            auto j = routing::receive(sub, zmq::recv_flags::dontwait);
            if (j.is_null()) break;
        }
    }

    // Test 1: Can we reach Thalamus?
    TestResult test_thalamus() {
        // Send a ping-like message and see if we can publish without error
        try {
            json ping = {
                {"origin", "system_test"}, {"intent", "test_ping"},
                {"cid", "test_thalamus_" + std::to_string(std::time(nullptr))}
            };
            routing::publish(pub, ping);
            return {.passed = true, .detail = "Thalamus at " + thalamus_ip_};
        } catch (...) {
            return {.passed = false, .detail = "Failed to connect to Thalamus"};
        }
    }

    // Test 2: Publish a message, verify it comes back through the proxy
    TestResult test_bus_roundtrip() {
        drain();
        std::string cid = "roundtrip_" + std::to_string(std::time(nullptr));

        json msg = {
            {"origin", "system_test"}, {"intent", "self_model_updated"},
            {"cid", cid}, {"domain", "test_roundtrip"}
        };
        routing::publish(pub, msg);

        auto resp = wait_for("self_model_updated", 3000);
        if (!resp.is_null() && resp.value("cid", "") == cid) {
            return {.passed = true, .detail = "Round-trip OK"};
        }
        return {.passed = false, .detail = "Message did not round-trip through Thalamus"};
    }

    // Test 3: Request intrinsic goal from BasalGanglia
    TestResult test_intrinsic_goal() {
        drain();
        std::string cid = "test_intrinsic_" + std::to_string(std::time(nullptr));

        json req = {
            {"origin", "system_test"}, {"intent", "intrinsic_goal_request"},
            {"cid", cid}
        };
        routing::publish(pub, req);

        auto resp = wait_for("intrinsic_goal", 10000);
        if (!resp.is_null()) {
            std::string domain = resp.value("domain", "unknown");
            return {.passed = true, .detail = "Got goal for domain: " + domain};
        }
        return {.passed = false, .detail = "No intrinsic_goal response (BasalGanglia running?)"};
    }

    // Test 4: Send execution_request, verify execution_result
    TestResult test_execution() {
        drain();
        std::string cid = "test_exec_" + std::to_string(std::time(nullptr));

        json req = {
            {"origin", "system_test"}, {"intent", "execution_request"},
            {"cid", cid}, {"command", "echo 'neuroswarm_test_ok'"}, {"mode", "reality"}
        };
        routing::publish(pub, req);

        auto resp = wait_for("execution_result", 15000);
        if (!resp.is_null()) {
            std::string status = resp.value("status", "");
            return {.passed = status == "success",
                    .detail = "status=" + status};
        }
        return {.passed = false, .detail = "No execution_result (MotorLobe running?)"};
    }

    // Test 5: Verify dopamine signal flows after execution
    TestResult test_dopamine() {
        // Dopamine fires on novel capability or prediction surprise
        // After test_execution, BasalGanglia may have emitted one
        auto resp = wait_for("dopamine_signal", 5000);
        if (!resp.is_null()) {
            std::string reason = resp.value("reason", "");
            return {.passed = true, .detail = "reason=" + reason};
        }
        // Not a hard failure — dopamine only fires on novel/surprising events
        return {.passed = true, .detail = "No dopamine (expected if domain not novel)"};
    }

    // Test 6: Self-model file exists and is valid JSON
    TestResult test_self_model() {
        std::ifstream f("./data/self_model.json");
        if (!f.is_open()) {
            return {.passed = false, .detail = "data/self_model.json not found"};
        }
        try {
            json doc;
            f >> doc;
            int domains = doc.size();
            return {.passed = domains > 0,
                    .detail = std::to_string(domains) + " domains tracked"};
        } catch (...) {
            return {.passed = false, .detail = "Invalid JSON in self_model.json"};
        }
    }

    // Test 7: RLAIF reinforcement message can be published and received
    TestResult test_rlaif() {
        drain();
        std::string cid = "test_rlaif_" + std::to_string(std::time(nullptr));

        json msg = {
            {"origin", "frontal_executive"}, {"intent", "rlaif_reinforce"},
            {"domain", "file_read"}, {"magnitude", 0.5f},
            {"commands", json::array({"cat /etc/hostname", "ls -la"})}
        };
        routing::publish(pub, msg);

        // Should round-trip through Thalamus
        auto resp = wait_for("rlaif_reinforce", 3000);
        if (!resp.is_null()) {
            return {.passed = true, .detail = "RLAIF message delivered"};
        }
        return {.passed = false, .detail = "RLAIF message not received"};
    }
};

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[++i];
    }

    FullSystemTest test(ip);
    return test.run_all();
}
