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

class Planner {
public:
    explicit Planner(OperatorRegistry& registry) : registry_(registry) {}

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
            result.failure_reason = "No operators found for: ";
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

    bool solve_goal(const std::string& goal,
                    const WorldState& current_state,
                    std::vector<PlanStep>& plan,
                    std::unordered_set<std::string>& visited,
                    int& nodes_explored,
                    int depth) {

        if (depth <= 0) return false;
        if (state_satisfies(current_state, goal)) return true;
        if (visited.count(goal)) return false;

        visited.insert(goal);
        nodes_explored++;

        auto candidates = registry_.find_by_postcondition(goal);

        std::sort(candidates.begin(), candidates.end(),
            [](const Operator* a, const Operator* b) {
                return a->success_rate > b->success_rate;
            });

        for (auto* op : candidates) {
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
