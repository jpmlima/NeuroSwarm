#pragma once
// Phase 2: Self-Preservation — Shared heartbeat protocol for lobe lifecycle management.
//
// Biological analogue: immune system — detect cellular damage, attempt repair,
// mark necrotic tissue when repair fails.

#include <string>
#include <chrono>
#include <sys/types.h>

namespace neuroswarm {

struct LobeRecord {
    pid_t pid = 0;
    std::string name;
    std::string path;
    int crash_count = 0;
    bool alive = true;
    std::chrono::steady_clock::time_point last_restart;

    // Backoff cooldown: 5s, 15s, 60s, 60s, 60s
    int restart_cooldown_sec() const {
        switch (crash_count) {
            case 0: return 0;
            case 1: return 5;
            case 2: return 15;
            default: return 60;
        }
    }

    static constexpr int MAX_RESTARTS = 5;
};

} // namespace neuroswarm
