#include "AST.hpp"

namespace pddl::parser
{

//---------------------------------------------------------------------------------------------------------------------
std::string FluentRef::key() const
{
    std::string k = func + "(";
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            k += ",";
        k += args[i];
    }
    k += ")";
    return k;
}

//---------------------------------------------------------------------------------------------------------------------
double eval_numeric(WorldState const& ws, Term const& t)
{
    if (const double* d = std::get_if<double>(&t.numeric))
        return *d;
    if (const FluentRef* ref = std::get_if<FluentRef>(&t.numeric))
        return ws.get_fluent(ref->key());
    return 0.0;
}

//---------------------------------------------------------------------------------------------------------------------
bool WorldState::holds(std::string const& pred_name, std::vector<std::string> const& args) const
{
    for (const auto& f : m_facts)
    {
        if (f.name != pred_name || f.args.size() != args.size())
        {
            continue;
        }
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
        {
            return true;
        }
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
void WorldState::add(Predicate const& p)
{
    if (!holds(p.name, to_names(p.args)))
    {
        m_facts.push_back(p);
    }
}

//---------------------------------------------------------------------------------------------------------------------
void WorldState::remove(std::string const& pred_name, std::vector<std::string> const& args)
{
    std::erase_if(m_facts,
                  [&](const Predicate& f)
                  {
                      if (f.name != pred_name || f.args.size() != args.size())
                          return false;
                      for (size_t i = 0; i < args.size(); ++i)
                          if (f.args[i].name != args[i])
                              return false;
                      return true;
                  });
}

//---------------------------------------------------------------------------------------------------------------------
std::vector<std::string> WorldState::to_names(std::vector<Term> const& terms)
{
    std::vector<std::string> names;
    names.reserve(terms.size());
    for (const auto& t : terms)
        names.push_back(t.name);
    return names;
}

//---------------------------------------------------------------------------------------------------------------------
double WorldState::get_fluent(std::string const& key) const
{
    auto it = m_fluents.find(key);
    return (it != m_fluents.end()) ? it->second : 0.0;
}

//---------------------------------------------------------------------------------------------------------------------
void WorldState::set_fluent(std::string const& key, double val)
{
    m_fluents[key] = val;
}

//---------------------------------------------------------------------------------------------------------------------
bool WorldState::has_fluent(std::string const& key) const
{
    return m_fluents.find(key) != m_fluents.end();
}

//---------------------------------------------------------------------------------------------------------------------
bool WorldState::operator==(WorldState const& other) const
{
    if (m_fluents != other.m_fluents)
        return false;
    if (m_facts.size() != other.m_facts.size())
        return false;
    for (const auto& f : m_facts)
    {
        if (!other.holds(f.name, to_names(f.args)))
            return false;
    }
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
bool WorldState::evaluates(Predicate const& p) const
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
        return eval_numeric(*this, p.args[0]) >= eval_numeric(*this, p.args[1]);
    }
    if (name == ">")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0]) > eval_numeric(*this, p.args[1]);
    }
    if (name == "<")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0]) < eval_numeric(*this, p.args[1]);
    }
    if (name == "<=")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0]) <= eval_numeric(*this, p.args[1]);
    }
    if (name == "=")
    {
        if (p.args.size() != 2)
            return false;
        return eval_numeric(*this, p.args[0]) == eval_numeric(*this, p.args[1]);
    }

    return holds(name, to_names(p.args));
}

//---------------------------------------------------------------------------------------------------------------------
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