#pragma once
// Autopoiesis — Goal-Oriented Action Planner (GOAP)
//
// Biological analogue: prefrontal cortex planning circuits.
// Given a desired world state, search backwards through known operators
// to find a sequence that transforms current state → goal state.
//
// No LLM involved. Pure graph search over learned operators.
// When no operator exists for a sub-goal, the planner signals a GAP —
// the system must generate a new operator (via variation or LLM).

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <OperatorRegistry.hpp>

namespace neuroswarm {

// A fact about the world: "file_exists(/tmp/test.txt)", "has_network", etc.
using WorldState = std::unordered_set<std::string>;

struct PlanStep {
    std::string operator_id;
    std::string operator_name;
    std::string command;           // fully resolved command
    std::vector<std::string> preconditions;
    std::vector<std::string> postconditions;
};

struct PlanResult {
    bool success;
    std::vector<PlanStep> steps;
    std::vector<std::string> gaps;  // postconditions no operator can satisfy
    int nodes_explored;
    std::string failure_reason;
};

// Runtime precondition verifier — fast filesystem/system probes before plan execution.
// Converts symbolic preconditions into concrete checks, preventing invalid plans
// from executing when world state is stale.
class PreconditionVerifier {
public:
    struct VerifyResult {
        bool passed;
        std::string precondition;
        std::string reason;  // empty if passed
    };

    // Verify a list of preconditions. Returns results for each.
    static std::vector<VerifyResult> verify(const std::vector<std::string>& preconditions) {
        std::vector<VerifyResult> results;
        for (const auto& pre : preconditions) {
            results.push_back(verify_one(pre));
        }
        return results;
    }

    // Check if a world state fact is transient (can become stale) vs capability (persistent)
    static bool is_transient_fact(const std::string& fact) {
        return fact.find("file_exists(") == 0 ||
               fact.find("path_exists(") == 0 ||
               fact.find("binary_exists(") == 0;
    }

    // Re-verify a transient world state fact. Returns true if still valid.
    static bool reverify_fact(const std::string& fact) {
        std::string path = extract_path(fact);
        if (path.empty()) return true;  // can't verify, assume valid

        if (fact.find("file_exists(") == 0) {
            return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
        }
        if (fact.find("path_exists(") == 0) {
            return std::filesystem::exists(path);
        }
        if (fact.find("binary_exists(") == 0) {
            return std::filesystem::exists(path) &&
                   (std::filesystem::status(path).permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
        }
        return true;
    }

    // Classify execution error output into a precondition failure type.
    // Returns the precondition predicate that was violated, or empty string.
    static std::string classify_failure(const std::string& error_output) {
        if (error_output.find("No such file or directory") != std::string::npos)
            return "path_exists";
        if (error_output.find("Permission denied") != std::string::npos)
            return "has_permissions";
        if (error_output.find("command not found") != std::string::npos)
            return "tool_in_path";
        if (error_output.find("Is a directory") != std::string::npos)
            return "file_exists";  // expected file, got directory
        if (error_output.find("not a directory") != std::string::npos)
            return "path_exists";
        return "";
    }

private:
    // Extract path from predicates like "path_exists(/home/x)" or "file_exists({file})"
    static std::string extract_path(const std::string& predicate) {
        auto open = predicate.find('(');
        auto close = predicate.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open + 1)
            return "";
        std::string path = predicate.substr(open + 1, close - open - 1);
        // Skip unresolved templates like {path}
        if (!path.empty() && path[0] == '{') return "";
        return path;
    }

    static VerifyResult verify_one(const std::string& precondition) {
        VerifyResult r;
        r.precondition = precondition;
        r.passed = true;

        std::string path = extract_path(precondition);

        // Unresolved template — can't verify at runtime, pass optimistically
        if (path.empty() && precondition.find('{') != std::string::npos) {
            return r;
        }

        if (precondition.find("path_exists(") == 0 && !path.empty()) {
            if (!std::filesystem::exists(path)) {
                r.passed = false;
                r.reason = "path does not exist: " + path;
            }
        }
        else if (precondition.find("file_exists(") == 0 && !path.empty()) {
            if (!std::filesystem::exists(path)) {
                r.passed = false;
                r.reason = "file does not exist: " + path;
            }
        }
        else if (precondition.find("binary_exists(") == 0 && !path.empty()) {
            if (!std::filesystem::exists(path)) {
                r.passed = false;
                r.reason = "binary does not exist: " + path;
            }
        }
        // Soft preconditions — always pass (capability-level, not path-level)
        // source_modified, dependencies_met, has_network, tool_in_path, etc.

        return r;
    }
};

class Planner {
public:
    explicit Planner(OperatorRegistry& registry) : registry_(registry) {}

    static constexpr int MAX_NODES = 500;     // hard cap on nodes explored per plan() call
    static constexpr int TOP_K_CANDIDATES = 5; // max operators to try per sub-goal

    // Plan a sequence of operators to achieve goal_conditions from current_state.
    PlanResult plan(const WorldState& current_state,
                    const std::vector<std::string>& goal_conditions,
                    int max_depth = 10) {

        PlanResult result;
        result.success = false;
        result.nodes_explored = 0;

        // Check which goals are already satisfied
        std::vector<std::string> unsatisfied;
        for (const auto& goal : goal_conditions) {
            if (!state_satisfies(current_state, goal)) {
                unsatisfied.push_back(goal);
            }
        }

        if (unsatisfied.empty()) {
            result.success = true;
            return result;
        }

        // Backward chaining: for each unsatisfied goal, find operators
        std::vector<PlanStep> plan;
        WorldState projected_state = current_state;

        for (const auto& goal : unsatisfied) {
            std::vector<PlanStep> sub_plan;
            std::unordered_set<std::string> visited;

            if (solve_goal(goal, projected_state, sub_plan, visited,
                           result.nodes_explored, max_depth)) {
                // Apply postconditions to projected state
                for (const auto& step : sub_plan) {
                    for (const auto& post : step.postconditions) {
                        projected_state.insert(post);
                    }
                }
                plan.insert(plan.end(), sub_plan.begin(), sub_plan.end());
            } else {
                result.gaps.push_back(goal);
            }
        }

        // Deduplicate plan steps
        deduplicate_plan(plan);

        result.steps = plan;
        result.success = result.gaps.empty();

        if (!result.success) {
            if (result.nodes_explored >= MAX_NODES) {
                result.failure_reason = "Node budget exhausted (" + std::to_string(MAX_NODES) + " nodes). ";
            }
            result.failure_reason += "No operators found for: ";
            for (const auto& g : result.gaps) result.failure_reason += g + ", ";
        }

        return result;
    }

    // Resolve parameter placeholders in an operator template.
    static std::string resolve_command(const Operator& op,
                                       const std::unordered_map<std::string, std::string>& bindings) {
        std::string cmd = op.command_template;
        for (const auto& [param, value] : bindings) {
            std::string placeholder = "{" + param + "}";
            size_t pos;
            while ((pos = cmd.find(placeholder)) != std::string::npos) {
                cmd.replace(pos, placeholder.size(), value);
            }
        }
        return cmd;
    }

private:
    OperatorRegistry& registry_;

    static bool state_satisfies(const WorldState& state, const std::string& condition) {
        if (state.count(condition)) return true;

        // Pattern match: "file_exists(/tmp/x)" satisfies "file_exists({path})"
        auto paren = condition.find('(');
        if (paren != std::string::npos) {
            std::string prefix = condition.substr(0, paren);
            for (const auto& s : state) {
                if (s.find(prefix) == 0) return true;
            }
        }

        return false;
    }

    // Wilson lower-bound score: favors operators with high success AND sufficient evidence.
    // An operator with 5/5 successes scores higher than one with 1/1.
    static double wilson_score(const Operator* op) {
        double n = op->times_used + 2.0;  // +2 Laplace smoothing
        double p = (op->successes + 1.0) / n;
        // Wilson lower bound with z=1.0 (68% confidence)
        double z = 1.0;
        double denom = 1.0 + z * z / n;
        double centre = p + z * z / (2.0 * n);
        double spread = z * std::sqrt(p * (1.0 - p) / n + z * z / (4.0 * n * n));
        return (centre - spread) / denom;
    }

    bool solve_goal(const std::string& goal,
                    const WorldState& current_state,
                    std::vector<PlanStep>& plan,
                    std::unordered_set<std::string>& visited,
                    int& nodes_explored,
                    int depth) {

        if (depth <= 0) return false;
        if (nodes_explored >= MAX_NODES) return false;  // budget exhausted
        if (state_satisfies(current_state, goal)) return true;
        if (visited.count(goal)) return false;

        visited.insert(goal);
        nodes_explored++;

        auto candidates = registry_.find_by_postcondition(goal);

        // Sort by Wilson score (proven quality over raw success_rate)
        std::sort(candidates.begin(), candidates.end(),
            [](const Operator* a, const Operator* b) {
                return wilson_score(a) > wilson_score(b);
            });

        // Top-K pruning: only try the best candidates to prevent branching explosion
        if ((int)candidates.size() > TOP_K_CANDIDATES) {
            candidates.resize(TOP_K_CANDIDATES);
        }

        for (auto* op : candidates) {
            if (nodes_explored >= MAX_NODES) return false;  // check budget inside loop

            bool all_preconds_met = true;
            std::vector<PlanStep> sub_plan;
            WorldState extended_state = current_state;

            for (const auto& pre : op->preconditions) {
                if (!state_satisfies(extended_state, pre)) {
                    std::vector<PlanStep> pre_plan;
                    if (solve_goal(pre, extended_state, pre_plan, visited,
                                   nodes_explored, depth - 1)) {
                        sub_plan.insert(sub_plan.end(), pre_plan.begin(), pre_plan.end());
                        for (const auto& step : pre_plan) {
                            for (const auto& post : step.postconditions) {
                                extended_state.insert(post);
                            }
                        }
                    } else {
                        all_preconds_met = false;
                        break;
                    }
                }
            }

            if (all_preconds_met) {
                plan.insert(plan.end(), sub_plan.begin(), sub_plan.end());

                PlanStep step;
                step.operator_id = op->id;
                step.operator_name = op->name;
                step.command = op->command_template;
                step.preconditions = op->preconditions;
                step.postconditions = op->postconditions;
                plan.push_back(step);

                return true;
            }
        }

        return false;
    }

    static void deduplicate_plan(std::vector<PlanStep>& plan) {
        std::unordered_set<std::string> seen;
        auto it = plan.begin();
        while (it != plan.end()) {
            if (seen.count(it->operator_id)) {
                it = plan.erase(it);
            } else {
                seen.insert(it->operator_id);
                ++it;
            }
        }
    }
};

} // namespace neuroswarm
