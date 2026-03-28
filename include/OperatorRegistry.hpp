#pragma once
// Autopoiesis Axiom 3+4 — Learned operators (procedural memory).
//
// Biological analogue: motor programs in the cerebellum / basal ganglia.
// Once you learn to ride a bike, you don't think about it — you just do it.
// Operators are the system's "muscle memory".

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace neuroswarm {

struct Operator {
    std::string id;
    std::string name;
    std::string command_template;   // e.g., "ls {path}" or "gcc -o {output} {source}"
    std::string language;           // "bash", "python", "c++"
    std::vector<std::string> parameters;
    std::vector<std::string> preconditions;   // e.g., "path_exists({path})"
    std::vector<std::string> postconditions;  // e.g., "output_is_file_list"

    int times_used = 0;
    int successes = 0;
    double success_rate = 0.0;
    double avg_duration_ms = 0.0;

    std::string learned_from;       // "bootstrap", "mutation", "recombination", "llm"
    std::string learned_at;         // ISO timestamp
    std::vector<std::string> fragments;  // decomposed parts for recombination

    // Update stats after use
    void record_use(bool success, double duration_ms) {
        times_used++;
        if (success) successes++;
        success_rate = static_cast<double>(successes) / times_used;
        // Running average
        avg_duration_ms = avg_duration_ms + (duration_ms - avg_duration_ms) / times_used;
    }

    bool is_stable() const { return times_used >= 5 && success_rate >= 0.8; }
    bool is_dying() const { return times_used >= 20 && success_rate < 0.1; }

    nlohmann::json to_json() const {
        return {
            {"id", id}, {"name", name},
            {"command_template", command_template},
            {"language", language},
            {"parameters", parameters},
            {"preconditions", preconditions},
            {"postconditions", postconditions},
            {"times_used", times_used},
            {"successes", successes},
            {"success_rate", success_rate},
            {"avg_duration_ms", avg_duration_ms},
            {"learned_from", learned_from},
            {"learned_at", learned_at},
            {"fragments", fragments}
        };
    }

    static Operator from_json(const nlohmann::json& j) {
        Operator op;
        op.id = j.value("id", "");
        op.name = j.value("name", "");
        op.command_template = j.value("command_template", "");
        op.language = j.value("language", "bash");
        op.parameters = j.value("parameters", std::vector<std::string>{});
        op.preconditions = j.value("preconditions", std::vector<std::string>{});
        op.postconditions = j.value("postconditions", std::vector<std::string>{});
        op.times_used = j.value("times_used", 0);
        op.successes = j.value("successes", 0);
        op.success_rate = j.value("success_rate", 0.0);
        op.avg_duration_ms = j.value("avg_duration_ms", 0.0);
        op.learned_from = j.value("learned_from", "");
        op.learned_at = j.value("learned_at", "");
        op.fragments = j.value("fragments", std::vector<std::string>{});
        return op;
    }
};

class OperatorRegistry {
public:
    explicit OperatorRegistry(const std::string& persist_path = "data/operators.jsonl")
        : persist_path_(persist_path) {
        load();
    }

    // Register a new operator learned from experience
    std::string add(Operator op) {
        // Hard cap: evict lowest-fitness operator if at limit
        static constexpr size_t MAX_OPERATORS = 2000;
        if (operators_.size() >= MAX_OPERATORS) {
            evict_weakest();
        }

        if (op.id.empty()) {
            op.id = "op_" + std::to_string(next_id_++);
        }
        if (op.learned_at.empty()) {
            op.learned_at = now_iso();
        }
        // Extract fragments from command template
        if (op.fragments.empty()) {
            op.fragments = extract_fragments(op.command_template);
        }
        operators_[op.id] = op;
        by_name_[op.name] = op.id;
        index_operator(operators_[op.id]);
        save_append(op);
        return op.id;
    }

    // Find operator by name
    Operator* find(const std::string& name) {
        auto it = by_name_.find(name);
        if (it == by_name_.end()) return nullptr;
        auto oit = operators_.find(it->second);
        if (oit == operators_.end()) return nullptr;
        return &oit->second;
    }

    // Find operators whose postconditions match a goal — O(1) lookup via inverted index
    std::vector<Operator*> find_by_postcondition(const std::string& postcondition) {
        // 1. Exact match in index
        auto it = postcond_index_.find(postcondition);
        if (it != postcond_index_.end() && !it->second.empty()) {
            auto results = it->second;
            std::sort(results.begin(), results.end(),
                [](const Operator* a, const Operator* b) {
                    return a->success_rate > b->success_rate;
                });
            return results;
        }

        // 2. Prefix match fallback: extract predicate prefix before '('
        //    e.g. "file_exists(/tmp/x)" matches index key "file_exists"
        std::string prefix = postcondition;
        auto paren = prefix.find('(');
        if (paren != std::string::npos) {
            prefix = prefix.substr(0, paren);
            // Search all index keys that start with this prefix
            std::vector<Operator*> results;
            for (auto& [key, ops] : postcond_index_) {
                if (key.find(prefix) == 0) {
                    for (auto* op : ops) results.push_back(op);
                }
            }
            if (!results.empty()) {
                // Deduplicate (an operator may appear under multiple keys)
                std::sort(results.begin(), results.end());
                results.erase(std::unique(results.begin(), results.end()), results.end());
                std::sort(results.begin(), results.end(),
                    [](const Operator* a, const Operator* b) {
                        return a->success_rate > b->success_rate;
                    });
                return results;
            }
        }

        // 3. Substring fallback for unusual patterns (legacy compat, still O(N) but rare)
        std::vector<Operator*> results;
        for (auto& [id, op] : operators_) {
            for (const auto& post : op.postconditions) {
                if (post.find(postcondition) != std::string::npos) {
                    results.push_back(&op);
                    break;
                }
            }
        }
        std::sort(results.begin(), results.end(),
            [](const Operator* a, const Operator* b) {
                return a->success_rate > b->success_rate;
            });
        return results;
    }

    // Get all stable operators (for recombination)
    std::vector<Operator*> get_stable() {
        std::vector<Operator*> results;
        for (auto& [id, op] : operators_) {
            if (op.is_stable()) results.push_back(&op);
        }
        return results;
    }

    // Get all unique fragments across all operators (for genetic programming)
    std::vector<std::string> all_fragments() const {
        std::vector<std::string> frags;
        for (const auto& [id, op] : operators_) {
            for (const auto& f : op.fragments) {
                if (std::find(frags.begin(), frags.end(), f) == frags.end()) {
                    frags.push_back(f);
                }
            }
        }
        return frags;
    }

    // Evict the weakest operator to make room when at capacity
    void evict_weakest() {
        if (operators_.empty()) return;
        std::string worst_id;
        double worst_fitness = 1e9;
        for (const auto& [id, op] : operators_) {
            double fitness = (op.times_used > 0)
                ? static_cast<double>(op.successes) / op.times_used
                : 0.0;
            // Penalize rarely-used operators
            if (op.times_used < 3) fitness -= 1.0;
            if (fitness < worst_fitness) {
                worst_fitness = fitness;
                worst_id = id;
            }
        }
        if (!worst_id.empty()) {
            by_name_.erase(operators_[worst_id].name);
            operators_.erase(worst_id);
            rebuild_postcond_index();
        }
    }

    // Prune dead operators (apoptosis)
    int prune() {
        int pruned = 0;
        for (auto it = operators_.begin(); it != operators_.end(); ) {
            if (it->second.is_dying()) {
                by_name_.erase(it->second.name);
                it = operators_.erase(it);
                pruned++;
            } else {
                ++it;
            }
        }
        if (pruned > 0) {
            rebuild_postcond_index();
            save_full();
        }
        return pruned;
    }

    // Purge degenerate operators (trivial commands that pollute the genome)
    int purge_degenerate() {
        int purged = 0;
        for (auto it = operators_.begin(); it != operators_.end(); ) {
            const std::string& cmd = it->second.command_template;
            std::string trimmed = cmd;
            while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
            while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());

            bool degen = false;
            // Trivial single commands
            static const std::vector<std::string> trivials = {
                "whoami", "id", "hostname", "pwd", "uname", "uname -a",
                "date", "uptime", "true", "false"
            };
            for (const auto& t : trivials) {
                if (trimmed == t) { degen = true; break; }
            }
            // mkdir spam
            if (trimmed.find("mkdir") == 0 && trimmed.find("&&") == std::string::npos) degen = true;
            // Pure echo
            if (trimmed.find("echo ") == 0 && trimmed.find("&&") == std::string::npos
                && trimmed.find("|") == std::string::npos && trimmed.find(">") == std::string::npos) degen = true;
            // Corrupted fragments
            if (!trimmed.empty() && trimmed[0] == '-') degen = true;

            if (degen) {
                by_name_.erase(it->second.name);
                it = operators_.erase(it);
                purged++;
            } else {
                ++it;
            }
        }
        if (purged > 0) {
            rebuild_postcond_index();
            save_full();
        }
        return purged;
    }

    size_t size() const { return operators_.size(); }

    // Persist full registry to disk
    void save_full() const {
        FILE* fp = fopen(persist_path_.c_str(), "w");
        if (!fp) {
            fprintf(stderr, "[OPERATOR_REGISTRY] save_full fopen FAILED '%s' errno=%d\n",
                    persist_path_.c_str(), errno);
            return;
        }
        for (const auto& [id, op] : operators_) {
            std::string line = op.to_json().dump() + "\n";
            fwrite(line.c_str(), 1, line.size(), fp);
        }
        fflush(fp);
        fclose(fp);
        fprintf(stderr, "[OPERATOR_REGISTRY] save_full wrote %zu operators to '%s'\n",
                operators_.size(), persist_path_.c_str());
    }

private:
    std::string persist_path_;
    std::unordered_map<std::string, Operator> operators_;
    std::unordered_map<std::string, std::string> by_name_;  // name → id
    std::unordered_map<std::string, std::vector<Operator*>> postcond_index_; // postcondition → operators
    int next_id_ = 1;

    // Rebuild the postcondition inverted index from scratch
    void rebuild_postcond_index() {
        postcond_index_.clear();
        for (auto& [id, op] : operators_) {
            if (op.postconditions.empty()) continue; // skip invisible operators
            for (const auto& post : op.postconditions) {
                postcond_index_[post].push_back(&op);
            }
        }
    }

    // Incrementally add a single operator to the postcondition index
    void index_operator(Operator& op) {
        for (const auto& post : op.postconditions) {
            postcond_index_[post].push_back(&op);
        }
    }

    void load() {
        std::ifstream f(persist_path_);
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try {
                auto j = nlohmann::json::parse(line);
                auto op = Operator::from_json(j);
                if (!op.id.empty()) {
                    operators_[op.id] = op;
                    by_name_[op.name] = op.id;
                    if (op.id.substr(0, 3) == "op_") {
                        int num = std::stoi(op.id.substr(3));
                        if (num >= next_id_) next_id_ = num + 1;
                    }
                }
            } catch (...) {}
        }
        rebuild_postcond_index();
    }

    void save_append(const Operator& op) const {
        // Debug: use C FILE* instead of ofstream to rule out C++ stream issues
        FILE* fp = fopen(persist_path_.c_str(), "a");
        if (!fp) {
            fprintf(stderr, "[OPERATOR_REGISTRY] fopen FAILED '%s' errno=%d\n",
                    persist_path_.c_str(), errno);
            return;
        }
        std::string line = op.to_json().dump() + "\n";
        size_t written = fwrite(line.c_str(), 1, line.size(), fp);
        fflush(fp);
        if (written != line.size()) {
            fprintf(stderr, "[OPERATOR_REGISTRY] fwrite incomplete: %zu/%zu\n",
                    written, line.size());
        }
        fclose(fp);
    }

    static std::vector<std::string> extract_fragments(const std::string& cmd) {
        std::vector<std::string> frags;
        std::string current;
        for (char c : cmd) {
            if (c == ' ' || c == '|' || c == '&' || c == ';') {
                if (!current.empty() && current[0] != '{') {
                    frags.push_back(current);
                }
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty() && current[0] != '{') {
            frags.push_back(current);
        }
        return frags;
    }

    static std::string now_iso() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }
};

} // namespace neuroswarm
