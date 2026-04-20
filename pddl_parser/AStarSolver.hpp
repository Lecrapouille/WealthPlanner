/// @file AStarSolver.hpp
/// A* planner and PDDL action execution engine.
#pragma once

#include "ISolver.hpp"
#include <functional>

namespace pddl::solver
{

/// *****************************************************************************
/// Configuration for the A* planner.
/// *****************************************************************************
struct AStarConfig
{
    size_t max_iterations = 500'000; ///< Maximum number of A* iterations.
    int fluent_bucket_size = 10;     ///< Granularity for state hashing (0 = exact).
    bool verbose = false;            ///< Print debug info during search.

    /// Custom heuristic (nullptr = default goal-count heuristic).
    std::function<float(const parser::WorldState&, const std::vector<parser::Predicate>&)> heuristic = nullptr;
};

/// *****************************************************************************
/// A* planner implementation.
///
/// Groups all functions needed to execute a PDDL domain:
/// state initialization, action instantiation, applicability check,
/// effect application, derived-predicate expansion, and A* search.
/// *****************************************************************************
class AStarSolver: public ISolver
{
public:

    explicit AStarSolver(AStarConfig cfg = {}) : m_config(std::move(cfg)) {}

    /// @copydoc ISolver::solve
    PlanResult solve(const SolverContext& ctx) override;

    /// Build the initial WorldState from parsed problem data.
    /// Converts @c (= (money alice) 7000) to fluents; keeps regular predicates.
    static parser::WorldState build_initial_state(const parser::Problem& p);

    /// Instantiate all domain actions with concrete objects from the problem.
    /// @return One GroundAction per valid (action, object-combination).
    static std::vector<GroundAction> instantiate_actions(const parser::Domain& d, const parser::Problem& p);

    /// Instantiate all derived predicates with concrete objects.
    static std::vector<GroundDerivedPredicate> instantiate_derived(const parser::Domain& d, const parser::Problem& p);

    /// Expand derived predicates to a fixed point.
    /// Called after build_initial_state and after each apply_action.
    static parser::WorldState expand_derived(parser::WorldState ws, const std::vector<GroundDerivedPredicate>& derived);

    /// Check if all preconditions of an action hold in the given state.
    static bool is_applicable(const GroundAction& action, const parser::WorldState& ws);

    /// Apply all effects of an action and return the resulting state.
    /// Automatically runs expand_derived if @p derived is non-empty.
    static parser::WorldState apply_action(const GroundAction& action,
                                           parser::WorldState ws,
                                           const std::vector<GroundDerivedPredicate>& derived = {});

private:

    AStarConfig m_config;
};

} // namespace pddl::solver
