#pragma once
// Autopoiesis Axiom 4 — Variation: mutation, recombination, template filling.
//
// Biological analogue: genetic variation. DNA replication errors (mutation)
// and meiotic crossover (recombination) generate novel genotypes. Most are
// lethal. Rarely, one is adaptive. Natural selection does the rest.
//
// The VariationEngine generates candidate operators from existing ones.
// The DreamSandbox tests them. Survivors are promoted to the OperatorRegistry.

#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <OperatorRegistry.hpp>

namespace neuroswarm {

class VariationEngine {
public:
    VariationEngine() : rng_(std::random_device{}()) {}

    // ─── Strategy 1: Template Filling ───
    // Take a known operator template and substitute parameters.
    // e.g., "ls {path}" + path="/etc" → "ls /etc"
    std::string fill_template(const Operator& op,
                              const std::unordered_map<std::string, std::string>& params) {
        std::string cmd = op.command_template;
        for (const auto& [key, value] : params) {
            std::string placeholder = "{" + key + "}";
            size_t pos;
            while ((pos = cmd.find(placeholder)) != std::string::npos) {
                cmd.replace(pos, placeholder.size(), value);
            }
        }
        return cmd;
    }

    // ─── Strategy 2: Recombination (crossover) ───
    // Take the structure of one operator and the content of another.
    // Like biological crossover: two parents → novel offspring.
    struct Candidate {
        std::string command;
        std::string origin;  // "template", "recombination", "mutation", "fragment_assembly"
        std::string parent_a;
        std::string parent_b;
    };

    std::vector<Candidate> recombine(const Operator& parent_a,
                                     const Operator& parent_b,
                                     int count = 3) {
        std::vector<Candidate> candidates;

        auto frags_a = parent_a.fragments;
        auto frags_b = parent_b.fragments;
        if (frags_a.empty() || frags_b.empty()) return candidates;

        for (int i = 0; i < count; i++) {
            // Crossover point
            size_t cut_a = 1 + (rng_() % std::max<size_t>(1, frags_a.size() - 1));
            size_t cut_b = rng_() % frags_b.size();

            std::string cmd;
            // Head from parent A
            for (size_t j = 0; j < cut_a && j < frags_a.size(); j++) {
                if (!cmd.empty()) cmd += " ";
                cmd += frags_a[j];
            }
            // Tail from parent B
            for (size_t j = cut_b; j < frags_b.size(); j++) {
                if (!cmd.empty()) cmd += " ";
                cmd += frags_b[j];
            }

            candidates.push_back({cmd, "recombination", parent_a.name, parent_b.name});
        }

        return candidates;
    }

    // ─── Strategy 3: Mutation ───
    // Small random perturbations to an existing operator.
    std::vector<Candidate> mutate(const Operator& parent, int count = 5) {
        std::vector<Candidate> candidates;
        auto frags = parent.fragments;
        if (frags.empty()) return candidates;

        for (int i = 0; i < count; i++) {
            auto mutated = frags;
            int mutation_type = rng_() % 5;

            switch (mutation_type) {
                case 0: // Add a common flag
                    mutated.push_back(random_flag());
                    break;

                case 1: // Remove a random fragment (keep at least 1)
                    if (mutated.size() > 1) {
                        size_t idx = rng_() % mutated.size();
                        mutated.erase(mutated.begin() + idx);
                    }
                    break;

                case 2: // Swap two fragments
                    if (mutated.size() > 1) {
                        size_t a = rng_() % mutated.size();
                        size_t b = rng_() % mutated.size();
                        std::swap(mutated[a], mutated[b]);
                    }
                    break;

                case 3: // Replace a fragment with one from the global pool
                    if (!global_fragments_.empty()) {
                        size_t idx = rng_() % mutated.size();
                        size_t pool_idx = rng_() % global_fragments_.size();
                        mutated[idx] = global_fragments_[pool_idx];
                    }
                    break;

                case 4: // Duplicate a fragment with different argument
                    if (!mutated.empty()) {
                        std::string base = mutated[0]; // the command
                        mutated.push_back(random_path());
                    }
                    break;
            }

            std::string cmd;
            for (const auto& f : mutated) {
                if (!cmd.empty()) cmd += " ";
                cmd += f;
            }

            candidates.push_back({cmd, "mutation", parent.name, ""});
        }

        return candidates;
    }

    // ─── Strategy 4: Fragment Assembly ───
    // Assemble a command from unrelated fragments in the global pool.
    // Pure exploration — most will fail, but some may discover new operators.
    std::vector<Candidate> assemble_from_fragments(int count = 5) {
        std::vector<Candidate> candidates;
        if (global_fragments_.size() < 2) return candidates;

        for (int i = 0; i < count; i++) {
            // Pick 1-3 random fragments
            int n_frags = 1 + (rng_() % 3);
            std::string cmd;
            for (int j = 0; j < n_frags; j++) {
                size_t idx = rng_() % global_fragments_.size();
                if (!cmd.empty()) cmd += " ";
                cmd += global_fragments_[idx];
            }
            candidates.push_back({cmd, "fragment_assembly", "", ""});
        }

        return candidates;
    }

    // ─── Strategy 5: Targeted Generation ───
    // Given a goal (postcondition we want to achieve), find similar operators
    // and generate variations specifically targeting that goal.
    std::vector<Candidate> targeted_variation(const std::string& goal_postcondition,
                                              OperatorRegistry& registry,
                                              int count = 10) {
        std::vector<Candidate> candidates;

        // Find operators with related postconditions
        auto related = registry.find_by_postcondition(goal_postcondition);

        if (!related.empty()) {
            // Mutate the best related operator
            for (auto* op : related) {
                auto mutations = mutate(*op, count / 2);
                candidates.insert(candidates.end(), mutations.begin(), mutations.end());
                if (candidates.size() >= static_cast<size_t>(count)) break;
            }
        }

        // Also try recombination between related operators
        if (related.size() >= 2) {
            for (size_t i = 0; i < related.size() - 1 && candidates.size() < static_cast<size_t>(count); i++) {
                auto crosses = recombine(*related[i], *related[i + 1], 2);
                candidates.insert(candidates.end(), crosses.begin(), crosses.end());
            }
        }

        // Fill remaining with fragment assembly
        while (candidates.size() < static_cast<size_t>(count)) {
            auto assembled = assemble_from_fragments(1);
            candidates.insert(candidates.end(), assembled.begin(), assembled.end());
        }

        return candidates;
    }

    // Update the global fragment pool from the registry
    void sync_fragments(const OperatorRegistry& registry) {
        global_fragments_ = registry.all_fragments();
        // Also add common paths and arguments
        add_exploration_fragments();
    }

private:
    std::mt19937 rng_;
    std::vector<std::string> global_fragments_;

    void add_exploration_fragments() {
        // Common paths worth exploring
        static const std::vector<std::string> paths = {
            "/tmp", "/home", "/etc", "/var", "/proc", "/sys", "/dev",
            "/usr/bin", "/usr/local/bin", ".", ".."
        };
        // Common flags
        static const std::vector<std::string> flags = {
            "-l", "-a", "-h", "-r", "-v", "--help", "--version",
            "-n", "-c", "-f", "-d", "-s"
        };

        for (const auto& p : paths) {
            if (std::find(global_fragments_.begin(), global_fragments_.end(), p) == global_fragments_.end())
                global_fragments_.push_back(p);
        }
        for (const auto& f : flags) {
            if (std::find(global_fragments_.begin(), global_fragments_.end(), f) == global_fragments_.end())
                global_fragments_.push_back(f);
        }
    }

    std::string random_flag() {
        static const std::vector<std::string> flags = {
            "-l", "-a", "-h", "-r", "-v", "-n", "-1",
            "--help", "--version", "--all", "--recursive"
        };
        return flags[rng_() % flags.size()];
    }

    std::string random_path() {
        static const std::vector<std::string> paths = {
            "/tmp", "/home", "/etc", "/proc", "/dev", "/var/log",
            "/usr/bin", "/usr/share", ".", ".."
        };
        return paths[rng_() % paths.size()];
    }
};

} // namespace neuroswarm
