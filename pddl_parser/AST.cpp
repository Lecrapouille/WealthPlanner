#include "AST.hpp"
#include "Lexer.hpp"
#include "SExpr.hpp"
#include <algorithm>
#include <cctype>

namespace pddl::parser {

// ── Internal helpers for numeric evaluation ──────────────────────────

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

static int eval_numeric(const WorldState& ws, const std::string& expr)
{
    if (is_number(expr))
        return std::stoi(expr);

    if (expr.empty() || expr[0] != '(')
        return 0;

    Lexer lex{ expr, "<eval>" };
    auto sexpr = parse_sexpr(lex);

    if (sexpr.is_atom || sexpr.children.empty())
        return 0;

    std::string func = sexpr.children[0].atom;
    std::vector<std::string> args;
    for (size_t i = 1; i < sexpr.children.size(); ++i)
        args.push_back(sexpr.children[i].atom);

    return ws.get_fluent(make_fluent_key(func, args));
}

// ── WorldState methods ───────────────────────────────────────────────

bool WorldState::holds(const std::string& pred_name,
                       const std::vector<std::string>& args) const
{
    for (const auto& f : facts_)
    {
        if (f.name != pred_name || f.args.size() != args.size())
            continue;
        bool match = true;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (f.args[i].name != args[i])
            {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

void WorldState::add(const Predicate& p)
{
    if (!holds(p.name, to_names(p.args)))
        facts_.push_back(p);
}

void WorldState::remove(const std::string& pred_name,
                        const std::vector<std::string>& args)
{
    std::erase_if(facts_,
                   [&](const Predicate& f)
                   {
                       if (f.name != pred_name ||
                           f.args.size() != args.size())
                           return false;
                       for (size_t i = 0; i < args.size(); ++i)
                           if (f.args[i].name != args[i])
                               return false;
                       return true;
                   });
}

const std::vector<Predicate>& WorldState::get_facts() const
{
    return facts_;
}

size_t WorldState::fact_count() const
{
    return facts_.size();
}

std::vector<std::string> WorldState::to_names(const std::vector<Term>& terms)
{
    std::vector<std::string> names;
    names.reserve(terms.size());
    for (const auto& t : terms)
        names.push_back(t.name);
    return names;
}

int WorldState::get_fluent(const std::string& key) const
{
    auto it = fluents_.find(key);
    return (it != fluents_.end()) ? it->second : 0;
}

void WorldState::set_fluent(const std::string& key, int val)
{
    fluents_[key] = val;
}

bool WorldState::has_fluent(const std::string& key) const
{
    return fluents_.find(key) != fluents_.end();
}

const std::unordered_map<std::string, int>& WorldState::get_fluents() const
{
    return fluents_;
}

bool WorldState::operator==(const WorldState& other) const
{
    if (fluents_ != other.fluents_)
        return false;
    if (facts_.size() != other.facts_.size())
        return false;
    for (const auto& f : facts_)
    {
        if (!other.holds(f.name, to_names(f.args)))
            return false;
    }
    return true;
}

bool WorldState::evaluates(const Predicate& p) const
{
    const std::string& name = p.name;

    if (name.starts_with("not:"))
    {
        std::string real_name = name.substr(4);
        return !holds(real_name, to_names(p.args));
    }

    if (name == ">=")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0].name) >= eval_numeric(*this, p.args[1].name);
    }
    if (name == ">")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0].name) > eval_numeric(*this, p.args[1].name);
    }
    if (name == "<")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0].name) < eval_numeric(*this, p.args[1].name);
    }
    if (name == "<=")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0].name) <= eval_numeric(*this, p.args[1].name);
    }
    if (name == "=")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0].name) == eval_numeric(*this, p.args[1].name);
    }

    return holds(name, to_names(p.args));
}

bool WorldState::is_goal_reached(const std::vector<Predicate>& goals) const
{
    for (const auto& g : goals)
    {
        if (!evaluates(g))
            return false;
    }
    return true;
}

} // namespace pddl::parser
