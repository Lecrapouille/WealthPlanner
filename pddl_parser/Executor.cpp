#include "Executor.hpp"
#include "Lexer.hpp"
#include "SExpr.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace pddl::solver
{

// ── A* Node ───────────────────────────────────────────────────────────

struct Node
{
    float estimated_cost; ///< f = g + h (must never overestimate)
    float real_cost;      ///< g = cost so far
    parser::WorldState state;
    std::vector<std::string> plan;

    bool operator>(const Node& o) const
    {
        return estimated_cost > o.estimated_cost;
    }
};

// ── State hashing with bucketization ──────────────────────────────────

static std::string state_key(const parser::WorldState& ws, int bucket_size)
{
    std::ostringstream ss;

    // Hash fluents (with optional bucketization)
    std::vector<std::pair<std::string, int>> fluents(ws.get_fluents().begin(),
                                                     ws.get_fluents().end());
    std::sort(fluents.begin(), fluents.end());
    for (const auto& [key, val] : fluents)
    {
        int bucketed = (bucket_size > 0) ? (val / bucket_size) : val;
        ss << key << "=" << bucketed << ";";
    }

    // Hash facts
    std::vector<std::string> facts;
    for (const auto& f : ws.get_facts())
    {
        std::ostringstream fs;
        fs << f.name;
        for (const auto& a : f.args)
            fs << "," << a.name;
        facts.push_back(fs.str());
    }
    std::sort(facts.begin(), facts.end());
    for (const auto& f : facts)
        ss << f << ";";

    return ss.str();
}

// ── Default heuristic: count unsatisfied goals ────────────────────────

static float default_heuristic(const parser::WorldState& ws,
                               const std::vector<parser::Predicate>& goals)
{
    float count = 0;
    for (const auto& g : goals)
    {
        if (!ws.evaluates(g))
            ++count;
    }
    return count;
}

// ── Internal helpers ─────────────────────────────────────────────────

static bool is_number(const std::string& s)
{
    if (s.empty())
        return false;
    size_t start = (s[0] == '-') ? 1 : 0;
    if (start >= s.size())
        return false;
    for (size_t i = start; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

static std::string make_fluent_key(const std::string& func_name,
                                   const std::vector<std::string>& args)
{
    std::string key = func_name + "(";
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            key += ",";
        key += args[i];
    }
    key += ")";
    return key;
}

static int eval_numeric(const parser::WorldState& ws, const std::string& expr)
{
    if (is_number(expr))
        return std::stoi(expr);

    if (expr.empty() || expr[0] != '(')
        throw std::runtime_error("eval_numeric: unknown expression: " + expr);

    parser::Lexer lex{ expr, "<eval>" };
    auto sexpr = parser::parse_sexpr(lex);

    if (sexpr.is_atom || sexpr.children.empty())
        throw std::runtime_error("eval_numeric: malformed expression: " + expr);

    std::string func = sexpr.children[0].atom;
    std::vector<std::string> args;
    for (size_t i = 1; i < sexpr.children.size(); ++i)
        args.push_back(sexpr.children[i].atom);

    return ws.get_fluent(make_fluent_key(func, args));
}

static std::string sexpr_to_string(const parser::SExpr& e)
{
    if (e.is_atom)
        return e.atom;
    std::string s = "(";
    for (size_t i = 0; i < e.children.size(); ++i)
    {
        if (i > 0)
            s += " ";
        s += sexpr_to_string(e.children[i]);
    }
    s += ")";
    return s;
}

static parser::Predicate parse_string_to_predicate(const std::string& str)
{
    parser::Predicate p;
    if (str.empty())
        return p;

    if (str[0] != '(')
    {
        p.name = str;
        return p;
    }

    parser::Lexer lex{ str, "<when>" };
    auto sexpr = parser::parse_sexpr(lex);

    if (sexpr.is_atom)
    {
        p.name = sexpr.atom;
        return p;
    }

    if (sexpr.children.empty())
        return p;

    p.name = sexpr.children[0].is_atom ? sexpr.children[0].atom
                                       : sexpr_to_string(sexpr.children[0]);
    for (size_t i = 1; i < sexpr.children.size(); ++i)
    {
        parser::Term t;
        t.name = sexpr.children[i].is_atom ? sexpr.children[i].atom
                                           : sexpr_to_string(sexpr.children[i]);
        t.is_variable = false;
        p.args.push_back(t);
    }
    return p;
}

static std::vector<std::string>
term_names(const std::vector<parser::Term>& terms)
{
    std::vector<std::string> names;
    names.reserve(terms.size());
    for (const auto& t : terms)
        names.push_back(t.name);
    return names;
}

static parser::WorldState apply_single_effect(parser::WorldState ws,
                                              const parser::Effect& eff)
{
    const parser::Predicate& p = eff.predicate;
    const std::string& name = p.name;

    if (eff.is_negated)
    {
        ws.remove(name, term_names(p.args));
        return ws;
    }

    if (name == "increase" && p.args.size() >= 2)
    {
        std::string fluent_expr = p.args[0].name;
        int delta = eval_numeric(ws, p.args[1].name);

        parser::Lexer lex{ fluent_expr, "<effect>" };
        auto sexpr = parser::parse_sexpr(lex);
        if (!sexpr.is_atom && !sexpr.children.empty())
        {
            std::string func = sexpr.children[0].atom;
            std::vector<std::string> args;
            for (size_t i = 1; i < sexpr.children.size(); ++i)
                args.push_back(sexpr.children[i].atom);
            std::string key = make_fluent_key(func, args);
            ws.set_fluent(key, ws.get_fluent(key) + delta);
        }
        return ws;
    }

    if (name == "decrease" && p.args.size() >= 2)
    {
        std::string fluent_expr = p.args[0].name;
        int delta = eval_numeric(ws, p.args[1].name);

        parser::Lexer lex{ fluent_expr, "<effect>" };
        auto sexpr = parser::parse_sexpr(lex);
        if (!sexpr.is_atom && !sexpr.children.empty())
        {
            std::string func = sexpr.children[0].atom;
            std::vector<std::string> args;
            for (size_t i = 1; i < sexpr.children.size(); ++i)
                args.push_back(sexpr.children[i].atom);
            std::string key = make_fluent_key(func, args);
            ws.set_fluent(key, ws.get_fluent(key) - delta);
        }
        return ws;
    }

    if (name == "assign" && p.args.size() >= 2)
    {
        std::string fluent_expr = p.args[0].name;
        int val = eval_numeric(ws, p.args[1].name);

        parser::Lexer lex{ fluent_expr, "<effect>" };
        auto sexpr = parser::parse_sexpr(lex);
        if (!sexpr.is_atom && !sexpr.children.empty())
        {
            std::string func = sexpr.children[0].atom;
            std::vector<std::string> args;
            for (size_t i = 1; i < sexpr.children.size(); ++i)
                args.push_back(sexpr.children[i].atom);
            std::string key = make_fluent_key(func, args);
            ws.set_fluent(key, val);
        }
        return ws;
    }

    if (name == "when" && p.args.size() >= 2)
    {
        parser::Predicate cond = parse_string_to_predicate(p.args[0].name);
        if (ws.evaluates(cond))
        {
            parser::Effect consequent;
            consequent.is_negated = false;
            consequent.predicate = parse_string_to_predicate(p.args[1].name);
            return apply_single_effect(ws, consequent);
        }
        return ws;
    }

    parser::Predicate fact;
    fact.name = name;
    for (const auto& arg : p.args)
        fact.args.push_back({ arg.name, false });
    ws.add(fact);
    return ws;
}

// ── Substitution helpers ─────────────────────────────────────────────

static std::string
substitute(const std::string& s,
           const std::unordered_map<std::string, std::string>& subst)
{
    std::string result = s;
    for (const auto& [var, obj] : subst)
    {
        size_t pos = 0;
        while ((pos = result.find(var, pos)) != std::string::npos)
        {
            bool at_word_boundary =
                (pos == 0 ||
                 !std::isalnum(static_cast<unsigned char>(result[pos - 1]))) &&
                (pos + var.size() >= result.size() ||
                 !std::isalnum(
                     static_cast<unsigned char>(result[pos + var.size()])));
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

static parser::Term
substitute_term(const parser::Term& t,
                const std::unordered_map<std::string, std::string>& subst)
{
    parser::Term result;
    result.name = substitute(t.name, subst);
    result.is_variable = false;
    return result;
}

static parser::Predicate
substitute_predicate(const parser::Predicate& p,
                     const std::unordered_map<std::string, std::string>& subst)
{
    parser::Predicate result;
    result.name = p.name;
    result.line = p.line;
    for (const auto& arg : p.args)
        result.args.push_back(substitute_term(arg, subst));
    return result;
}

static parser::Effect
substitute_effect(const parser::Effect& e,
                  const std::unordered_map<std::string, std::string>& subst)
{
    parser::Effect result;
    result.is_negated = e.is_negated;
    result.predicate = substitute_predicate(e.predicate, subst);
    return result;
}

// ── Executor class methods ───────────────────────────────────────────

parser::WorldState Executor::build_initial_state(const parser::Problem& p)
{
    parser::WorldState ws;

    for (const auto& fact : p.init.get_facts())
    {
        if (fact.name == "=" && fact.args.size() == 2)
        {
            std::string fluent_expr = fact.args[0].name;
            int val = 0;
            if (is_number(fact.args[1].name))
                val = std::stoi(fact.args[1].name);

            parser::Lexer lex{ fluent_expr, "<init>" };
            auto sexpr = parser::parse_sexpr(lex);
            if (!sexpr.is_atom && !sexpr.children.empty())
            {
                std::string func = sexpr.children[0].atom;
                std::vector<std::string> args;
                for (size_t i = 1; i < sexpr.children.size(); ++i)
                    args.push_back(sexpr.children[i].atom);
                ws.set_fluent(make_fluent_key(func, args), val);
            }
        }
        else
        {
            ws.add(fact);
        }
    }

    return ws;
}

std::vector<GroundAction>
Executor::instantiate_actions(const parser::Domain& d, const parser::Problem& p)
{
    std::vector<GroundAction> actions;

    for (const auto& action : d.actions)
    {
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

        std::vector<std::unordered_map<std::string, std::string>> substitutions;
        substitutions.push_back({});

        for (const auto& param : action.parameters)
        {
            std::vector<std::unordered_map<std::string, std::string>>
                new_substitutions;
            for (const auto& subst : substitutions)
            {
                for (const auto& obj : p.objects)
                {
                    auto new_subst = subst;
                    new_subst[param.name] = obj;
                    new_substitutions.push_back(new_subst);
                }
            }
            substitutions = std::move(new_substitutions);
        }

        for (const auto& subst : substitutions)
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

bool Executor::is_applicable(const GroundAction& action,
                             const parser::WorldState& ws)
{
    for (const auto& p : action.preconditions)
    {
        if (!ws.evaluates(p))
            return false;
    }
    return true;
}

parser::WorldState Executor::apply_action(const GroundAction& action,
                                          parser::WorldState ws)
{
    for (const auto& eff : action.effects)
        ws = apply_single_effect(ws, eff);
    return ws;
}

// ── A* Planner ────────────────────────────────────────────────────────

PlanResult Executor::plan(const parser::WorldState& initial,
                          const std::vector<GroundAction>& actions,
                          const std::vector<parser::Predicate>& goals,
                          const PlannerConfig& config)
{
    auto h = config.heuristic ? config.heuristic
                              : [](const parser::WorldState& ws,
                                   const std::vector<parser::Predicate>& g)
    { return default_heuristic(ws, g); };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<std::string, float> best_cost;

    Node start;
    start.real_cost = 0;
    start.estimated_cost = h(initial, goals);
    start.state = initial;
    open.push(start);

    size_t iterations = 0;

    while (!open.empty() && iterations < config.max_iterations)
    {
        ++iterations;
        Node current = open.top();
        open.pop();

        if (current.state.is_goal_reached(goals))
        {
            if (config.verbose)
                std::cerr << "[planner] Goal reached after " << iterations
                          << " iterations\n";
            return { true, current.plan, current.state, iterations };
        }

        std::string key = state_key(current.state, config.fluent_bucket_size);
        if (best_cost.count(key) && best_cost[key] <= current.real_cost)
            continue;
        best_cost[key] = current.real_cost;

        if (config.verbose && iterations % 1000 == 0)
            std::cerr << "[planner] " << iterations << " iterations, "
                      << open.size() << " open, " << best_cost.size()
                      << " visited, best plan=" << current.plan.size() << "\n";

        for (const auto& action : actions)
        {
            if (!is_applicable(action, current.state))
                continue;

            parser::WorldState new_state = apply_action(action, current.state);
            float ng = current.real_cost + action.cost;

            std::string new_key =
                state_key(new_state, config.fluent_bucket_size);
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

    if (config.verbose)
        std::cerr << "[planner] No plan found after " << iterations
                  << " iterations\n";
    return { false, {}, initial, iterations };
}

} // namespace pddl::solver
