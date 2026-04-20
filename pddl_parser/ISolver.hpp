/// @file ISolver.hpp
/// Common solver types and abstract ISolver interface.
///
/// All concrete planners (AStarSolver, OrderedGoalsSolver, …) implement ISolver
/// and receive a SolverContext that bundles the inputs needed for planning.
#pragma once

#include "AST.hpp"
#include <string>
#include <vector>

namespace pddl::solver
{

/// *****************************************************************************
/// An instantiated (ground) action with no variables.
/// *****************************************************************************
struct GroundAction
{
    std::string name;                             ///< e.g. "work-startup(alice)"
    int cost = 1;                                 ///< Cost of the action.
    std::vector<parser::Predicate> preconditions; ///< Instantiated preconditions.
    std::vector<parser::Effect>    effects;        ///< Instantiated effects.
};

/// *****************************************************************************
/// A ground derived predicate (axiom) with no variables.
/// *****************************************************************************
struct GroundDerivedPredicate
{
    parser::Predicate              head;       ///< The predicate being defined.
    std::vector<parser::Predicate> conditions; ///< Conditions as a conjunction.
};

/// *****************************************************************************
/// Result of a planning search.
/// *****************************************************************************
struct PlanResult
{
    bool success = false;
    std::vector<std::string> plan; ///< Sequence of action names.
    parser::WorldState final_state;
    size_t iterations = 0;
};

/// *****************************************************************************
/// All inputs a solver needs, bundled in one place.
/// *****************************************************************************
struct SolverContext
{
    const parser::WorldState&                  initial;
    const std::vector<GroundAction>&           actions;
    const std::vector<parser::Predicate>&      goals;
    const std::vector<GroundDerivedPredicate>& derived; ///< Grounded derived predicates.
};

/// *****************************************************************************
/// Abstract interface implemented by every concrete planner.
/// *****************************************************************************
class ISolver
{
public:
    virtual ~ISolver() = default;

    /// Run the planner and return a PlanResult.
    virtual PlanResult solve(const SolverContext& ctx) = 0;
};

} // namespace pddl::solver
