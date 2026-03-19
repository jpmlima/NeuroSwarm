#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <ctime>
#include <sstream>

using json = nlohmann::json;

namespace neuroswarm {

// ChronosLobe — temporal awareness substrate for the swarm.
//
// Broadcasts a time_pulse event at a configurable interval, providing
// every lobe with a shared temporal reference. This enables:
//   - FrontalExecutive: elapsed task duration in prompts
//   - Hippocampus: temporal decay weighting on engram retrieval
//   - Homeostasis: circadian scheduling (reduced activity at night)
//   - StatisticsLobe: time-series alignment of metrics

class ChronosLobe {
public:
    ChronosLobe(const std::string& thalamus_ip = "localhost",
                int pulse_interval_ms = 1000)
        : ctx(1), pub(ctx, zmq::socket_type::pub),
          pulse_interval(pulse_interval_ms),
          boot_time(std::chrono::steady_clock::now()) {

        pub.connect("tcp://" + thalamus_ip + ":5555");
        std::cout << "[CHRONOS] Temporal lobe online. Pulse interval: "
                  << pulse_interval_ms << "ms" << std::endl;
    }

    void start() {
        while (true) {
            emit_time_pulse();
            std::this_thread::sleep_for(std::chrono::milliseconds(pulse_interval));
        }
    }

private:
    zmq::context_t ctx;
    zmq::socket_t pub;
    int pulse_interval;
    std::chrono::steady_clock::time_point boot_time;

    // Map hour of day to a human-readable period label
    static std::string time_of_day(int hour) {
        if (hour >= 6  && hour < 12) return "morning";
        if (hour >= 12 && hour < 14) return "midday";
        if (hour >= 14 && hour < 18) return "afternoon";
        if (hour >= 18 && hour < 22) return "evening";
        return "night";
    }

    static std::string day_of_week(int wday) {
        static const char* days[] = {
            "sunday", "monday", "tuesday", "wednesday",
            "thursday", "friday", "saturday"
        };
        return days[wday % 7];
    }

    void emit_time_pulse() {
        auto now_wall = std::chrono::system_clock::now();
        auto now_mono = std::chrono::steady_clock::now();

        // Wall clock — ISO 8601 timestamp
        std::time_t t = std::chrono::system_clock::to_time_t(now_wall);
        std::tm* lt = std::localtime(&t);

        char iso_buf[64];
        std::strftime(iso_buf, sizeof(iso_buf), "%Y-%m-%dT%H:%M:%S%z", lt);

        // Uptime since CerebralMatrix spawned this lobe
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now_mono - boot_time);
        long uptime_s = uptime.count();

        // Format uptime as human-readable string
        int hours = uptime_s / 3600;
        int mins  = (uptime_s % 3600) / 60;
        int secs  = uptime_s % 60;
        std::ostringstream uptime_str;
        uptime_str << hours << "h" << mins << "m" << secs << "s";

        json pulse = {
            {"origin", "chronos"},
            {"intent", "time_pulse"},
            {"timestamp", std::string(iso_buf)},
            {"hour", lt->tm_hour},
            {"minute", lt->tm_min},
            {"second", lt->tm_sec},
            {"time_of_day", time_of_day(lt->tm_hour)},
            {"day_of_week", day_of_week(lt->tm_wday)},
            {"uptime_seconds", uptime_s},
            {"uptime_human", uptime_str.str()},
            {"is_night", (lt->tm_hour >= 22 || lt->tm_hour < 6)}
        };

        std::string s = pulse.dump();
        zmq::message_t m(s.size());
        memcpy(m.data(), s.c_str(), s.size());
        pub.send(m, zmq::send_flags::none);
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    int interval = 1000; // Default: 1 pulse per second

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[++i];
        if (std::string(argv[i]) == "--interval" && i + 1 < argc) interval = std::stoi(argv[++i]);
    }

    neuroswarm::ChronosLobe chronos(ip, interval);
    chronos.start();
    return 0;
}
