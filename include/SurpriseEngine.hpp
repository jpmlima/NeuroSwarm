#pragma once
// Autopoiesis Axiom 2 — Surprise as the primary drive.
//
// Biological analogue: dopaminergic prediction error signal.
// The system acts because reality differs from expectation.
// High surprise → explore. Low surprise → exploit.

#include <string>
#include <cmath>
#include <unordered_map>
#include <deque>

namespace neuroswarm {

struct SurpriseResult {
    double surprise;        // 0.0 = perfectly predicted, 1.0 = completely unexpected
    bool is_novel;          // true if this context has never been seen before
    double trend;           // positive = getting more surprising, negative = stabilising
};

class SurpriseEngine {
public:
    // Compute surprise for an action outcome.
    // context: hash of the situation (e.g., "ls_/tmp")
    // success: did the action succeed?
    // output_hash: hash of the output (to detect identical results)
    SurpriseResult compute(const std::string& context, bool success, size_t output_hash) {
        SurpriseResult result;

        auto it = history.find(context);
        if (it == history.end()) {
            // Never seen this context — maximum surprise
            result.surprise = 1.0;
            result.is_novel = true;
            result.trend = 0.0;

            ContextHistory h;
            h.attempts = 1;
            h.successes = success ? 1 : 0;
            h.last_output_hash = output_hash;
            h.surprise_window.push_back(1.0);
            history[context] = h;
            return result;
        }

        auto& h = it->second;
        result.is_novel = false;
        h.attempts++;
        if (success) h.successes++;

        // Surprise from success/failure prediction
        double expected_success_rate = (h.attempts > 1)
            ? static_cast<double>(h.successes - (success ? 1 : 0)) / (h.attempts - 1)
            : 0.5;
        double outcome = success ? 1.0 : 0.0;
        double prediction_error = std::abs(expected_success_rate - outcome);

        // Surprise from output novelty (did we get a different result than usual?)
        double output_surprise = (output_hash != h.last_output_hash) ? 0.5 : 0.0;
        h.last_output_hash = output_hash;

        // Combined surprise (weighted)
        result.surprise = std::min(1.0, prediction_error * 0.6 + output_surprise * 0.4);

        // Trend: compare recent surprise to older surprise
        h.surprise_window.push_back(result.surprise);
        if (h.surprise_window.size() > WINDOW_SIZE) {
            h.surprise_window.pop_front();
        }

        if (h.surprise_window.size() >= 4) {
            size_t mid = h.surprise_window.size() / 2;
            double recent = 0, older = 0;
            for (size_t i = 0; i < mid; i++) older += h.surprise_window[i];
            for (size_t i = mid; i < h.surprise_window.size(); i++) recent += h.surprise_window[i];
            older /= mid;
            recent /= (h.surprise_window.size() - mid);
            result.trend = recent - older;
        } else {
            result.trend = 0.0;
        }

        return result;
    }

    // Global average surprise across all contexts (0.0 = fully mapped, 1.0 = total chaos)
    double global_surprise() const {
        if (history.empty()) return 1.0;
        double sum = 0;
        for (const auto& [ctx, h] : history) {
            if (!h.surprise_window.empty()) {
                sum += h.surprise_window.back();
            }
        }
        return sum / history.size();
    }

    size_t contexts_known() const { return history.size(); }

private:
    static constexpr size_t WINDOW_SIZE = 20;

    struct ContextHistory {
        int attempts = 0;
        int successes = 0;
        size_t last_output_hash = 0;
        std::deque<double> surprise_window;
    };

    std::unordered_map<std::string, ContextHistory> history;
};

} // namespace neuroswarm
