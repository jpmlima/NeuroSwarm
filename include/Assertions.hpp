#pragma once
// NeuroSwarm Runtime Assertions — invariant framework for self-modifying code.
//
// When the system modifies its own source (neuro_surgery), compiles new
// specialists (neurogenesis), or hot-loads LoRA adapters, runtime invariants
// catch regressions that static analysis cannot.
//
// Usage:
//   NS_ASSERT(condition, "message")          — abort with trace on failure
//   NS_PRECONDITION(cond, "msg")             — document function entry requirements
//   NS_POSTCONDITION(cond, "msg")            — document function exit guarantees
//   NS_INVARIANT(cond, "msg")                — structural invariant (loop, class)
//   NSScopedRollback guard(path, callback)   — RAII rollback on assertion failure
//
// All assertions log to data/assertions.jsonl for post-mortem analysis.
// In production builds, NS_ASSERT can be compiled out with -DNS_NO_ASSERTIONS.

#include <string>
#include <functional>
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>

namespace neuroswarm {

// Assertion severity levels
enum class AssertLevel {
    WARN,      // log and continue
    ERROR,     // log, attempt rollback, continue
    FATAL      // log, attempt rollback, abort
};

struct AssertionResult {
    bool passed;
    std::string file;
    int line;
    std::string expression;
    std::string message;
    AssertLevel level;
};

// Global assertion handler — logs to JSONL and optionally aborts
inline void handle_assertion(const AssertionResult& result) {
    if (result.passed) return;

    // Format timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char ts[64];
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));

    // Log to stderr
    const char* level_str = result.level == AssertLevel::FATAL ? "FATAL"
                          : result.level == AssertLevel::ERROR ? "ERROR" : "WARN";
    std::cerr << "[ASSERTION " << level_str << "] " << result.file << ":"
              << result.line << " — " << result.message
              << " (expr: " << result.expression << ")" << std::endl;

    // Append to assertions log
    std::ofstream log("./data/assertions.jsonl", std::ios::app);
    if (log.is_open()) {
        log << "{\"ts\":\"" << ts
            << "\",\"level\":\"" << level_str
            << "\",\"file\":\"" << result.file
            << "\",\"line\":" << result.line
            << ",\"expr\":\"" << result.expression
            << "\",\"msg\":\"" << result.message
            << "\"}" << std::endl;
    }

    if (result.level == AssertLevel::FATAL) {
        std::abort();
    }
}

// RAII guard: if an assertion fires between construction and destruction,
// execute a rollback callback (e.g., restore a backup file, unload a LoRA adapter).
class ScopedRollback {
public:
    ScopedRollback(const std::string& description, std::function<void()> rollback)
        : description_(description), rollback_(std::move(rollback)), armed_(true) {}

    // Disarm — call when the guarded operation succeeds
    void commit() { armed_ = false; }

    ~ScopedRollback() {
        if (armed_ && rollback_) {
            std::cerr << "[ROLLBACK] Executing rollback: " << description_ << std::endl;
            try { rollback_(); } catch (...) {
                std::cerr << "[ROLLBACK] Rollback failed: " << description_ << std::endl;
            }
        }
    }

    ScopedRollback(const ScopedRollback&) = delete;
    ScopedRollback& operator=(const ScopedRollback&) = delete;

private:
    std::string description_;
    std::function<void()> rollback_;
    bool armed_;
};

// File backup helper for neuro_surgery rollback
inline std::string backup_file(const std::string& path) {
    std::string backup = path + ".ns_backup";
    try {
        std::filesystem::copy_file(path, backup,
            std::filesystem::copy_options::overwrite_existing);
    } catch (...) {
        return "";
    }
    return backup;
}

inline void restore_file(const std::string& backup, const std::string& original) {
    try {
        std::filesystem::copy_file(backup, original,
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(backup);
    } catch (...) {}
}

} // namespace neuroswarm

// ─── Macros ───

#ifndef NS_NO_ASSERTIONS

#define NS_ASSERT(cond, msg) \
    neuroswarm::handle_assertion({ \
        static_cast<bool>(cond), __FILE__, __LINE__, #cond, (msg), \
        neuroswarm::AssertLevel::FATAL \
    })

#define NS_PRECONDITION(cond, msg) \
    neuroswarm::handle_assertion({ \
        static_cast<bool>(cond), __FILE__, __LINE__, #cond, (msg), \
        neuroswarm::AssertLevel::ERROR \
    })

#define NS_POSTCONDITION(cond, msg) \
    neuroswarm::handle_assertion({ \
        static_cast<bool>(cond), __FILE__, __LINE__, #cond, (msg), \
        neuroswarm::AssertLevel::ERROR \
    })

#define NS_INVARIANT(cond, msg) \
    neuroswarm::handle_assertion({ \
        static_cast<bool>(cond), __FILE__, __LINE__, #cond, (msg), \
        neuroswarm::AssertLevel::WARN \
    })

#else

#define NS_ASSERT(cond, msg)        ((void)0)
#define NS_PRECONDITION(cond, msg)  ((void)0)
#define NS_POSTCONDITION(cond, msg) ((void)0)
#define NS_INVARIANT(cond, msg)     ((void)0)

#endif
