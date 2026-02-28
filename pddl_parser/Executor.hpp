/// @file Executor.hpp
/// Generic PDDL action execution engine with A* planner.
#pragma once
#include "AST.hpp"
#include <functional>
#include <vector>

namespace pddl::solver
{

/// An instantiated (ground) action with no variables.
/// Pure data structure mirroring WealthPlanner's Action.
struct GroundAction
{
    std::string name; ///< e.g. "work-startup(alice)"
    int cost = 1;
    std::vector<parser::Predicate> preconditions; ///< Instantiated preconditions.
    std::vector<parser::Effect> effects;          ///< Instantiated effects.
};

/// Configuration for the A* planner.
struct PlannerConfig
{
    size_t max_iterations = 500'000;
    int fluent_bucket_size = 10; ///< Granularity for state hashing (0 = exact).
    bool verbose = false;        ///< Print debug info during search.
    /// Custom heuristic (nullptr = default goal-count heuristic).
    std::function<float(const parser::WorldState&,
                        const std::vector<parser::Predicate>&)>
        heuristic = nullptr;
};

/// Result of the A* planning search.
struct PlanResult
{
    bool success = false;
    std::vector<std::string> plan; ///< Sequence of action names.
    parser::WorldState final_state;
    size_t iterations = 0;
};

/// Execution engine for PDDL actions.
///
/// Groups all functions needed to run a PDDL domain:
/// state initialization, action instantiation, applicability check, effect application, planning.
class Executor
{
public:

    /// Build the initial WorldState from parsed problem data.
    /// Converts `(= (money alice) 7000)` to fluents and keeps regular predicates.
    static parser::WorldState build_initial_state(const parser::Problem& p);

    /// Instantiate all domain actions with concrete objects from the problem.
    /// @return One GroundAction per (action, object-combination).
    static std::vector<GroundAction> instantiate_actions(const parser::Domain& d,
                                                         const parser::Problem& p);

    /// Check if all preconditions of an action hold in the given state.
    static bool is_applicable(const GroundAction& action,
                              const parser::WorldState& ws);

    /// Apply all effects of an action and return the resulting state.
    static parser::WorldState apply_action(const GroundAction& action,
                                           parser::WorldState ws);

    /// Find an optimal plan using A* search.
    /// @param initial  Starting world state.
    /// @param actions  Available ground actions.
    /// @param goals    Goal predicates to satisfy.
    /// @param config   Planner configuration.
    /// @return PlanResult with success flag, plan, final state, and iteration count.
    static PlanResult plan(const parser::WorldState& initial,
                           const std::vector<GroundAction>& actions,
                           const std::vector<parser::Predicate>& goals,
                           const PlannerConfig& config = {});
};

} // namespace pddl::solver
