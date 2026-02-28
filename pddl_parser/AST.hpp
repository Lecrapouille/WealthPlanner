/// @file AST.hpp
/// Abstract syntax tree types for a parsed PDDL domain and problem.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace pddl::parser
{

/// A term is either a variable (?x) or a constant (block-a).
struct Term
{
    std::string
        name; ///< Term text. For sub-expressions it may be a serialized S-expr.
    bool is_variable = false; ///< True if the term starts with '?'.
};

/// A predicate application, e.g. @c (on ?x ?y) or @c (>= (money ?a) 10000).
struct Predicate
{
    std::string
        name; ///< Predicate or operator name (e.g. "on", ">=", "increase").
    std::vector<Term> args; ///< Arguments (variables, constants, or serialized
                            ///< sub-expressions).
    int line = 0;           ///< Source line for error reporting.
};

/// A single effect entry — either adds or deletes a predicate.
struct Effect
{
    bool is_negated =
        false;           ///< True when wrapped in @c (not ...), meaning delete.
    Predicate predicate; ///< The predicate being added or removed.
};

/// A PDDL action with preconditions and effects.
///
/// Mirrors the structure used by GOAP-style planners: name, cost,
/// a conjunction of preconditions, and a list of effects.
struct Action
{
    std::string name; ///< Action identifier (e.g. "work-megacorp").
    int cost = 1;     ///< Planner cost (lower is preferred).
    std::vector<Term>
        parameters; ///< Typed parameters (types are currently ignored).
    std::vector<Predicate>
        preconditions;           ///< Conjunction of required predicates.
    std::vector<Effect> effects; ///< Resulting add/delete effects.
    int line = 0;                ///< Source line for error reporting.
};

/// A set of ground (variable-free) predicates representing the world.
///
/// Encapsulates fact storage and provides query/mutation operations.
class WorldState
{
public:

    /// Check whether a ground predicate is currently true.
    /// @param pred_name  Predicate name to look for.
    /// @param args       Expected argument names (must match exactly).
    /// @return True if the fact is present.
    bool holds(const std::string& pred_name,
               const std::vector<std::string>& args) const;

    /// Add a predicate to the state (no-op if already present).
    void add(const Predicate& p);

    /// Remove all matching facts from the state.
    void remove(const std::string& pred_name,
                const std::vector<std::string>& args);

    /// Read-only access to the internal fact list.
    const std::vector<Predicate>& get_facts() const;

    /// Number of facts currently stored.
    size_t fact_count() const;

    /// Get a numeric fluent value (returns 0 if not set).
    /// @param key  Fluent key, e.g. "money(alice)".
    int get_fluent(const std::string& key) const;

    /// Set a numeric fluent value.
    /// @param key  Fluent key, e.g. "money(alice)".
    /// @param val  The value to assign.
    void set_fluent(const std::string& key, int val);

    /// Check whether a fluent exists.
    bool has_fluent(const std::string& key) const;

    /// Read-only access to all fluents.
    const std::unordered_map<std::string, int>& get_fluents() const;

    /// Equality comparison (needed for planner visited set).
    bool operator==(const WorldState& other) const;

    /// Evaluate a single predicate (handles boolean facts and numeric comparisons).
    /// @param p  The predicate to evaluate.
    /// @return True if the predicate holds in this state.
    bool evaluates(const Predicate& p) const;

    /// Check whether all goal predicates are satisfied.
    /// @param goals  Conjunction of goal predicates.
    /// @return True if all goals are met.
    bool is_goal_reached(const std::vector<Predicate>& goals) const;

private:

    std::vector<Predicate> facts_;
    std::unordered_map<std::string, int> fluents_;

    static std::vector<std::string> to_names(const std::vector<Term>& terms);
};

/// A parsed PDDL domain: name, requirements, predicate signatures, and actions.
struct Domain
{
    std::string name; ///< Domain name from @c (domain ...).
    std::vector<std::string>
        requirements; ///< PDDL requirement flags (e.g. ":typing").
    std::vector<Predicate> predicates; ///< Declared predicate signatures.
    std::vector<Action> actions;       ///< All action definitions.
};

/// A parsed PDDL problem: initial state, objects, and goal.
struct Problem
{
    std::string name;        ///< Problem name from @c (problem ...).
    std::string domain_name; ///< Referenced domain name.
    std::vector<std::string>
        objects;     ///< Declared objects (types currently ignored).
    WorldState init; ///< Initial world state.
    std::vector<Predicate> goal; ///< Goal as a conjunction of predicates.
};

} // namespace pddl::parser
