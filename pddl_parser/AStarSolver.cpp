#include "AStarSolver.hpp"
#include <algorithm>
#include <iostream>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace pddl::solver
{

/// *****************************************************************************
/// A* Node
/// *****************************************************************************
struct Node
{
    float estimated_cost; ///< f = g + h (must never overestimate)
    float real_cost;      ///< g = cost so far
    parser::WorldState state;
    std::vector<std::string> plan;

    /// Compare nodes by estimated cost.
    bool operator>(const Node& o) const
    {
        return estimated_cost > o.estimated_cost;
    }
};

/// *****************************************************************************
/// State hashing with bucketization
///
/// Bucketization quantises numeric fluent values before hashing: a fluent with
/// value @p v is snapped to @c floor(v / bucket_size) * bucket_size.  Two states
/// whose fluents differ by less than @p bucket_size are therefore treated as
/// identical, which prevents the search from exploring an exponential number of
/// nearly-equal numeric states.  The trade-off is that the resulting plan may
/// not be perfectly optimal.  Set @p bucket_size to 0 for exact (but slower)
/// hashing.
///
/// Returns a @c size_t hash rather than a string to avoid heap allocations on
/// every A* expansion.  A good 64-bit mixing function makes collisions
/// statistically negligible for realistic planning problems.
/// *****************************************************************************

/// *****************************************************************************
/// Combine @p v into hash @p seed using boost::hash_combine mixing.
/// *****************************************************************************
static void hash_combine(size_t& seed, size_t v)
{
    seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

/// *****************************************************************************
/// State key for hashing
/// *****************************************************************************
static size_t state_key(const parser::WorldState& ws, int bucket_size)
{
    size_t h = 0;

    // Hash numeric fluents in sorted order (determinism requires consistent ordering).
    std::vector<std::pair<std::string, double>> fluents(ws.get_fluents().begin(), ws.get_fluents().end());
    std::sort(fluents.begin(), fluents.end());
    for (const auto& [name, val] : fluents)
    {
        const double bucketed =
            (bucket_size > 0) ? static_cast<double>(static_cast<long long>(val / bucket_size)) : val;
        hash_combine(h, std::hash<std::string>{}(name));
        hash_combine(h, std::hash<double>{}(bucketed));
    }

    // Hash boolean facts in sorted order.
    std::vector<std::string> fact_keys;
    fact_keys.reserve(ws.get_facts().size());
    for (const auto& f : ws.get_facts())
    {
        std::string s = f.name;
        for (const auto& a : f.args)
        {
            s += ',';
            s += a.name;
        }
        fact_keys.push_back(std::move(s));
    }
    std::sort(fact_keys.begin(), fact_keys.end());
    for (const auto& s : fact_keys)
        hash_combine(h, std::hash<std::string>{}(s));

    return h;
}

/// *****************************************************************************
/// Default heuristic: count unsatisfied goals
/// *****************************************************************************
static float default_heuristic(const parser::WorldState& ws, const std::vector<parser::Predicate>& goals)
{
    float count = 0;
    for (const auto& g : goals)
    {
        if (!ws.evaluates(g))
            ++count;
    }
    return count;
}

/// *****************************************************************************
/// Extract the @c name field from each Term into a plain string vector.
/// Used to call WorldState::remove / holds which accept string argument lists.
/// *****************************************************************************
static std::vector<std::string> term_names(const std::vector<parser::Term>& terms)
{
    std::vector<std::string> names;
    names.reserve(terms.size());
    for (const auto& t : terms)
        names.push_back(t.name);
    return names;
}

/// *****************************************************************************
/// Apply one effect to a world state and return the updated state.
/// *****************************************************************************
static parser::WorldState apply_single_effect(parser::WorldState ws, const parser::Effect& eff)
{
    // Conditional (when ...) effect: recurse only when the guard holds.
    if (eff.when_condition.has_value())
    {
        if (ws.evaluates(*eff.when_condition))
        {
            parser::Effect consequent;
            consequent.is_negated = eff.is_negated;
            consequent.numeric_op = eff.numeric_op; ///< Must propagate so numeric when-effects work.
            consequent.predicate = eff.predicate;
            return apply_single_effect(std::move(ws), consequent);
        }
        return ws;
    }

    const parser::Predicate& p = eff.predicate;

    if (eff.is_negated)
    {
        ws.remove(p.name, term_names(p.args));
        return ws;
    }

    // Numeric mutation: increase / decrease / assign a fluent.
    if (eff.numeric_op != parser::NumericOp::None && p.args.size() >= 2)
    {
        const auto* ref = std::get_if<parser::FluentRef>(&p.args[0].numeric);
        if (ref)
        {
            const std::string key = ref->key();
            const double delta = parser::eval_numeric(ws, p.args[1]);
            switch (eff.numeric_op)
            {
                case parser::NumericOp::Increase:
                    ws.set_fluent(key, ws.get_fluent(key) + delta);
                    break;
                case parser::NumericOp::Decrease:
                    ws.set_fluent(key, ws.get_fluent(key) - delta);
                    break;
                case parser::NumericOp::Assign:
                    ws.set_fluent(key, delta);
                    break;
                default:
                    break;
            }
        }
        return ws;
    }

    // Boolean add effect.
    parser::Predicate fact;
    fact.name = p.name;
    for (const auto& arg : p.args)
        fact.args.push_back({ arg.name, /*type=*/"", /*is_variable=*/false });
    ws.add(fact);
    return ws;
}

/// *****************************************************************************
/// Replace every word-boundary occurrence of a variable name with its bound
/// object in @p s.  The word-boundary check prevents partial matches (e.g.
/// @c ?agent must not match inside @c ?agent-type).
/// *****************************************************************************
static std::string substitute(const std::string& s, const std::unordered_map<std::string, std::string>& subst)
{
    std::string result = s;
    for (const auto& [var, obj] : subst)
    {
        size_t pos = 0;
        while ((pos = result.find(var, pos)) != std::string::npos)
        {
            bool at_word_boundary = (pos == 0 || !std::isalnum(static_cast<unsigned char>(result[pos - 1]))) &&
                                    (pos + var.size() >= result.size() ||
                                     !std::isalnum(static_cast<unsigned char>(result[pos + var.size()])));
            if (at_word_boundary)
            {
                result.replace(pos, var.size(), obj);
                pos += obj.size();
            }
            else
            {
                pos += var.size();
            }
        }
    }
    return result;
}

/// *****************************************************************************
/// Substitute variables in a single Term, including inside FluentRef::args.
/// *****************************************************************************
static parser::Term substitute_term(const parser::Term& t, const std::unordered_map<std::string, std::string>& subst)
{
    parser::Term result;
    result.name = substitute(t.name, subst);
    result.type = t.type;
    result.is_variable = false;

    if (const auto* ref = std::get_if<parser::FluentRef>(&t.numeric))
    {
        parser::FluentRef new_ref;
        new_ref.func = ref->func;
        for (const auto& a : ref->args)
        {
            auto it = subst.find(a);
            new_ref.args.push_back(it != subst.end() ? it->second : a);
        }
        result.numeric = std::move(new_ref);
    }
    else
    {
        result.numeric = t.numeric; // copy double or monostate
    }
    return result;
}

/// *****************************************************************************
/// Substitute variables in all arguments of a Predicate.
/// *****************************************************************************
static parser::Predicate substitute_predicate(const parser::Predicate& p,
                                              const std::unordered_map<std::string, std::string>& subst)
{
    parser::Predicate result;
    result.name = p.name;
    result.line = p.line;
    for (const auto& arg : p.args)
        result.args.push_back(substitute_term(arg, subst));
    return result;
}

/// *****************************************************************************
/// Substitute variables in an Effect (predicate, optional when-condition, numeric_op is unchanged).
/// *****************************************************************************
static parser::Effect substitute_effect(const parser::Effect& e,
                                        const std::unordered_map<std::string, std::string>& subst)
{
    parser::Effect result;
    result.is_negated = e.is_negated;
    result.numeric_op = e.numeric_op;
    result.predicate = substitute_predicate(e.predicate, subst);
    if (e.when_condition.has_value())
        result.when_condition = substitute_predicate(*e.when_condition, subst);
    return result;
}

/// *****************************************************************************
/// Build every possible variable→object mapping for @p params over @p objects.
/// Returns the Cartesian product: one map per combination.
/// *****************************************************************************
static std::vector<std::unordered_map<std::string, std::string>>
build_substitutions(const std::vector<parser::Term>& params, const std::vector<std::string>& objects)
{
    std::vector<std::unordered_map<std::string, std::string>> substitutions;
    substitutions.push_back({});

    for (const auto& param : params)
    {
        std::vector<std::unordered_map<std::string, std::string>> new_subs;
        for (const auto& subst : substitutions)
        {
            for (const auto& obj : objects)
            {
                auto ns = subst;
                ns[param.name] = obj;
                new_subs.push_back(std::move(ns));
            }
        }
        substitutions = std::move(new_subs);
    }
    return substitutions;
}

/// *****************************************************************************
/// Build the initial WorldState from parsed problem data.
/// *****************************************************************************
parser::WorldState AStarSolver::build_initial_state(const parser::Problem& p)
{
    parser::WorldState ws;

    for (const auto& fact : p.init.get_facts())
    {
        if (fact.name == "=" && fact.args.size() == 2)
        {
            const auto* ref = std::get_if<parser::FluentRef>(&fact.args[0].numeric);
            if (ref)
                ws.set_fluent(ref->key(), parser::eval_numeric(ws, fact.args[1]));
        }
        else
        {
            ws.add(fact);
        }
    }

    return ws;
}

/// *****************************************************************************
/// Instantiate all domain actions with concrete objects from the problem.
/// @return One GroundAction per valid (action, object-combination).
/// *****************************************************************************
std::vector<GroundAction> AStarSolver::instantiate_actions(const parser::Domain& d, const parser::Problem& p)
{
    // Gather all objects: problem objects + domain constants
    std::vector<std::string> all_objects = p.objects;
    for (const auto& c : d.constants)
        all_objects.push_back(c);

    std::vector<GroundAction> actions;

    for (const auto& action : d.actions)
    {
        // No parameters: instantiate a plain action without grounding.
        if (action.parameters.empty())
        {
            GroundAction ga;
            ga.name = action.name;
            ga.cost = action.cost;
            ga.preconditions = action.preconditions;
            ga.effects = action.effects;
            actions.push_back(ga);
            continue;
        }

        // Ground the action by substituting each parameter with all possible objects.
        for (const auto& subst : build_substitutions(action.parameters, all_objects))
        {
            GroundAction ga;

            std::ostringstream name_ss;
            name_ss << action.name << "(";
            bool first = true;
            for (const auto& param : action.parameters)
            {
                if (!first)
                    name_ss << ",";
                name_ss << subst.at(param.name);
                first = false;
            }
            name_ss << ")";
            ga.name = name_ss.str();

            ga.cost = action.cost;

            for (const auto& prec : action.preconditions)
                ga.preconditions.push_back(substitute_predicate(prec, subst));

            for (const auto& eff : action.effects)
                ga.effects.push_back(substitute_effect(eff, subst));

            actions.push_back(ga);
        }
    }

    return actions;
}

/// *****************************************************************************
/// Instantiate all derived predicates with concrete objects.
/// @return One GroundDerivedPredicate per valid (derived, object-combination).
/// *****************************************************************************
std::vector<GroundDerivedPredicate> AStarSolver::instantiate_derived(const parser::Domain& d, const parser::Problem& p)
{
    std::vector<std::string> all_objects = p.objects;
    for (const auto& c : d.constants)
        all_objects.push_back(c);

    std::vector<GroundDerivedPredicate> result;

    for (const auto& dp : d.derived)
    {
        // Collect variable parameters from the head predicate
        std::vector<parser::Term> params;
        for (const auto& arg : dp.head.args)
            if (arg.is_variable)
                params.push_back(arg);

        if (params.empty())
        {
            GroundDerivedPredicate gdp;
            gdp.head = dp.head;
            gdp.conditions = dp.body;
            result.push_back(std::move(gdp));
            continue;
        }

        for (const auto& subst : build_substitutions(params, all_objects))
        {
            GroundDerivedPredicate gdp;
            gdp.head = substitute_predicate(dp.head, subst);
            for (const auto& cond : dp.body)
                gdp.conditions.push_back(substitute_predicate(cond, subst));
            result.push_back(std::move(gdp));
        }
    }

    return result;
}

/// *****************************************************************************
/// Expand derived predicates to a fixed point.
/// Called after build_initial_state and after each apply_action.
/// *****************************************************************************
parser::WorldState AStarSolver::expand_derived(parser::WorldState ws,
                                               const std::vector<GroundDerivedPredicate>& derived)
{
    if (derived.empty())
        return ws;

    // Fixed-point iteration: keep updating until no change occurs
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& gdp : derived)
        {
            // Check whether all conditions hold
            bool holds = true;
            for (const auto& cond : gdp.conditions)
            {
                if (!ws.evaluates(cond))
                {
                    holds = false;
                    break;
                }
            }

            std::vector<std::string> head_args;
            for (const auto& a : gdp.head.args)
                head_args.push_back(a.name);

            const bool currently_present = ws.holds(gdp.head.name, head_args);

            if (holds && !currently_present)
            {
                ws.add(gdp.head);
                changed = true;
            }
            else if (!holds && currently_present)
            {
                ws.remove(gdp.head.name, head_args);
                changed = true;
            }
        }
    }
    return ws;
}

/// *****************************************************************************
/// Check if all preconditions of an action hold in the given state.
/// *****************************************************************************
bool AStarSolver::is_applicable(const GroundAction& action, const parser::WorldState& ws)
{
    for (const auto& p : action.preconditions)
    {
        if (!ws.evaluates(p))
            return false;
    }
    return true;
}

/// *****************************************************************************
/// Apply all effects of an action and return the resulting state.
/// Automatically runs expand_derived if @p derived is non-empty.
/// *****************************************************************************
parser::WorldState AStarSolver::apply_action(const GroundAction& action,
                                             parser::WorldState ws,
                                             const std::vector<GroundDerivedPredicate>& derived)
{
    for (const auto& eff : action.effects)
        ws = apply_single_effect(ws, eff);
    return expand_derived(std::move(ws), derived);
}

/// *****************************************************************************
/// ISolver::solve implementation
/// *****************************************************************************
PlanResult AStarSolver::solve(const SolverContext& ctx)
{
    const auto& initial = ctx.initial;
    const auto& actions = ctx.actions;
    const auto& goals = ctx.goals;
    const auto& derived = ctx.derived;
    const auto& cfg = m_config;

    auto h = cfg.heuristic ? cfg.heuristic : [](const parser::WorldState& ws, const std::vector<parser::Predicate>& g)
    { return default_heuristic(ws, g); };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<size_t, float> best_cost; ///< Maps state hash → best g-cost seen so far.

    Node start;
    start.real_cost = 0;
    start.estimated_cost = h(initial, goals);
    start.state = initial;
    open.push(start);

    size_t iterations = 0;

    while (!open.empty() && iterations < cfg.max_iterations)
    {
        ++iterations;
        Node current = open.top();
        open.pop();

        if (current.state.is_goal_reached(goals))
        {
            if (cfg.verbose)
                std::cerr << "[astar] Goal reached after " << iterations << " iterations\n";
            return { true, current.plan, current.state, iterations };
        }

        size_t key = state_key(current.state, cfg.fluent_bucket_size);
        if (best_cost.count(key) && best_cost[key] <= current.real_cost)
            continue;
        best_cost[key] = current.real_cost;

        if (cfg.verbose && iterations % 1000 == 0)
            std::cerr << "[astar] " << iterations << " iterations, " << open.size() << " open, " << best_cost.size()
                      << " visited, best plan=" << current.plan.size() << "\n";

        for (const auto& action : actions)
        {
            if (!is_applicable(action, current.state))
                continue;

            parser::WorldState new_state = apply_action(action, current.state, derived);
            float ng = current.real_cost + static_cast<float>(action.cost);

            size_t new_key = state_key(new_state, cfg.fluent_bucket_size);
            if (best_cost.count(new_key) && best_cost[new_key] <= ng)
                continue;

            Node next;
            next.real_cost = ng;
            next.estimated_cost = ng + h(new_state, goals);
            next.state = new_state;
            next.plan = current.plan;
            next.plan.push_back(action.name);
            open.push(next);
        }
    }

    if (cfg.verbose)
        std::cerr << "[astar] No plan found after " << iterations << " iterations\n";
    return { false, {}, initial, iterations };
}

} // namespace pddl::solver
