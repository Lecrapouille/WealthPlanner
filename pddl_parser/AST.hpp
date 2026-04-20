/// ****************************************************************************
/// @file AST.hpp
/// Abstract syntax tree types for a parsed PDDL domain and problem.
/// ****************************************************************************

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace pddl::parser
{

/// *****************************************************************************
/// A reference to a numeric fluent function call, e.g. @c (money alice).
///
/// Stored in Term::numeric when the term is a fluent reference rather than a
/// plain identifier.  The function arguments are stored as resolved names so
/// that substitution (variable → object) can update them without re-parsing.
/// *****************************************************************************
struct FluentRef
{
    std::string              func; ///< Function name, e.g. "money".
    std::vector<std::string> args; ///< Resolved argument names, e.g. {"alice"}.

    /// Compute the WorldState fluent key (e.g. "money(alice)").
    std::string key() const;
};

/// Discriminated union representing the numeric value carried by a Term:
///   - @c std::monostate : plain identifier – Term::name holds the text.
///   - @c double         : numeric literal resolved at parse time.
///   - @c FluentRef      : function call that refers to a WorldState fluent.
using NumericExpr = std::variant<std::monostate, double, FluentRef>;

/// *****************************************************************************
/// A term is either a variable (?x) or a constant (block-a).
/// *****************************************************************************
struct Term
{
    std::string name;               ///< Term text (identifier or original PDDL token).
    std::string type;               ///< Declared type (e.g. "agent"). Empty string if untyped.
    bool        is_variable = false;///< True if the term starts with '?'.
    NumericExpr numeric;            ///< Non-monostate for numeric sub-expressions.
};

/// *****************************************************************************
/// A predicate application, e.g. @c (on ?x ?y) or @c (>= (money ?a) 10000).
/// *****************************************************************************
struct Predicate
{
    std::string name;       ///< Predicate or operator name (e.g. "on", ">=", "increase").
    std::vector<Term> args; ///< Arguments (variables, constants, or serialized sub-expressions).
    size_t line = 0;        ///< Source line for error reporting.
};

/// Numeric mutation kind for an effect.  @c None means the effect is a plain
/// boolean add/delete; the other values map directly to PDDL numeric keywords.
enum class NumericOp { None, Increase, Decrease, Assign };

/// *****************************************************************************
/// A single effect entry: adds/deletes a predicate, or a conditional effect.
///
/// When @c when_condition is set this is a PDDL @c (when condition consequent):
/// the effect applies only when @c when_condition holds.  @c is_negated and
/// @c predicate describe the consequent.
///
/// When @c numeric_op is not @c None, the effect mutates a numeric fluent;
/// @c predicate.args[0] holds the target @c FluentRef and @c predicate.args[1]
/// holds the operand (a @c double literal or another @c FluentRef).
/// *****************************************************************************
struct Effect
{
    bool                     is_negated     = false;          ///< True for a delete effect.
    NumericOp                numeric_op     = NumericOp::None;///< Non-None for numeric mutations.
    Predicate                predicate;                       ///< Affected predicate / fluent target.
    std::optional<Predicate> when_condition;                  ///< Non-empty for conditional effects.
};

/// *****************************************************************************
/// A PDDL action with preconditions and effects.
///
/// Mirrors the structure used by GOAP-style planners: name, cost,
/// a conjunction of preconditions, and a list of effects.
/// *****************************************************************************
struct Action
{
    std::string name;                     ///< Action identifier (e.g. "work-megacorp").
    int cost = 1;                         ///< Planner cost (lower is preferred).
    std::vector<Term> parameters;         ///< Typed parameters.
    std::vector<Predicate> preconditions; ///< Conjunction of required predicates.
    std::vector<Effect> effects;          ///< Resulting add/delete effects.
    size_t line = 0;                      ///< Source line for error reporting.
};

/// *****************************************************************************
/// A single type declaration from the @c :types section.
/// *****************************************************************************
struct TypeDef
{
    std::string name;   ///< Type name (e.g. "agent").
    std::string parent; ///< Parent type, empty means "object".
};

/// *****************************************************************************
/// A derived predicate (axiom) from the @c :derived section.
///
/// The head predicate is considered true whenever all body predicates hold.
/// *****************************************************************************
struct DerivedPredicate
{
    Predicate              head; ///< The predicate being defined.
    std::vector<Predicate> body; ///< Conjunction of conditions (may contain variables).
};

// Forward declaration needed by eval_numeric.
class WorldState;

/// Evaluate a numeric term (literal or fluent reference) against a world state.
/// Returns the literal value for @c double terms, looks up the fluent for
/// @c FluentRef terms, and returns @c 0.0 for plain-identifier (monostate) terms.
double eval_numeric(WorldState const& ws, Term const& t);

/// *****************************************************************************
/// A set of ground (variable-free) predicates representing the world.
///
/// Encapsulates fact storage and provides query/mutation operations.
/// *****************************************************************************
class WorldState
{
public:

    /// Check whether a ground predicate is currently true.
    /// @param pred_name  Predicate name to look for.
    /// @param args       Expected argument names (must match exactly).
    /// @return True if the fact is present.
    bool holds(std::string const& pred_name, std::vector<std::string> const& args) const;

    /// Add a predicate to the state (no-op if already present).
    void add(const Predicate& p);

    /// Remove all matching facts from the state.
    void remove(std::string const& pred_name, std::vector<std::string> const& args);

    /// Read-only access to the internal fact list.
    std::vector<Predicate> const& get_facts() const
    {
        return m_facts;
    }

    /// Number of facts currently stored.
    size_t fact_count() const
    {
        return m_facts.size();
    }

    /// Get a numeric fluent value (returns 0.0 if not set).
    /// @param key  Fluent key, e.g. "money(alice)".
    double get_fluent(std::string const& key) const;

    /// Set a numeric fluent value.
    /// @param key  Fluent key, e.g. "money(alice)".
    /// @param val  The value to assign.
    void set_fluent(std::string const& key, double val);

    /// Check whether a fluent exists.
    bool has_fluent(std::string const& key) const;

    /// Read-only access to all fluents.
    std::unordered_map<std::string, double> const& get_fluents() const
    {
        return m_fluents;
    }

    /// Equality comparison (needed for planner visited set).
    bool operator==(WorldState const& other) const;

    /// Evaluate a single predicate (handles boolean facts and numeric comparisons).
    /// @param p  The predicate to evaluate.
    /// @return True if the predicate holds in this state.
    bool evaluates(Predicate const& p) const;

    /// Check whether all goal predicates are satisfied.
    /// @param goals  Conjunction of goal predicates.
    /// @return True if all goals are met.
    bool is_goal_reached(std::vector<Predicate> const& goals) const;

private:

    static std::vector<std::string> to_names(const std::vector<Term>& terms);

private:

    std::vector<Predicate> m_facts;
    std::unordered_map<std::string, double> m_fluents;
};

/// *****************************************************************************
/// A parsed PDDL domain: name, requirements, type hierarchy, predicate
/// signatures, function declarations, actions, and derived predicates.
/// *****************************************************************************
struct Domain
{
    std::string name;                        ///< Domain name from @c (domain ...).
    std::vector<std::string> requirements;   ///< PDDL requirement flags (e.g. ":typing").
    std::vector<TypeDef>     types;          ///< Type hierarchy from @c :types.
    std::vector<std::string> constants;      ///< Domain constants from @c :constants.
    std::vector<Predicate>   predicates;     ///< Declared predicate signatures.
    std::vector<Predicate>   functions;      ///< Declared numeric function signatures from @c :functions.
    std::vector<Action>      actions;        ///< All action definitions.
    std::vector<DerivedPredicate> derived;   ///< Derived predicate axioms from @c :derived.
};

/// *****************************************************************************
/// A parsed PDDL problem: initial state, objects, goal, and optional metric.
/// *****************************************************************************
struct Problem
{
    std::string name;                 ///< Problem name from @c (problem ...).
    std::string domain_name;          ///< Referenced domain name.
    std::vector<std::string> objects; ///< Declared objects (names only; types stored in Term::type if needed).
    WorldState init;                  ///< Initial world state.
    std::vector<Predicate> goal;      ///< Goal as a conjunction of predicates.
    std::string metric;               ///< Serialized metric expression (e.g. "minimize total-cost"). May be empty.
};

} // namespace pddl::parser
