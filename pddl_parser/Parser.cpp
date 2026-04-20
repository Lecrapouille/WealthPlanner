#include "Parser.hpp"
#include "SExpr.hpp"
#include <fstream>
#include <sstream>

namespace pddl::parser
{

/// *****************************************************************************
/// Convert an S-expression to a string.
/// @param e  The S-expression to convert.
/// @return The string representation of the S-expression.
/// *****************************************************************************
static std::string sexpr_to_string(SExpr const& e)
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

/// *****************************************************************************
/// Parse a term from an S-expression.
/// @param e  The S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed term.
/// *****************************************************************************
static Term parse_term(SExpr const& e, [[maybe_unused]] Lexer& lex)
{
    if (e.is_atom)
        return { e.atom, e.atom.starts_with('?') };
    return { sexpr_to_string(e), false };
}

/// *****************************************************************************
/// Parse a predicate from an S-expression.
/// @param e  The S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed predicate.
/// *****************************************************************************
static Predicate parse_predicate(SExpr const& e, Lexer& lex)
{
    if (e.is_atom)
        return { e.atom, {}, e.line };
    if (e.children.empty())
        lexer_error(lex, e.line, "expected predicate list");
    Predicate p;
    p.line = e.line;
    p.name = e.children[0].is_atom ? e.children[0].atom : sexpr_to_string(e.children[0]);
    for (size_t i = 1; i < e.children.size(); ++i)
        p.args.push_back(parse_term(e.children[i], lex));
    return p;
}

/// *****************************************************************************
/// Parse a predicate list from an S-expression.
/// @param e  The S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed predicate list.
/// *****************************************************************************
static std::vector<Predicate> parse_predicate_list(SExpr const& e, Lexer& lex)
{
    // TODO: (or ...) disjunctive preconditions
    // TODO: (imply cond effect) implications
    // TODO: (exists (?x - type) ...) existential quantifiers
    // TODO: (forall (?x - type) ...) universal quantifiers
    if (tagged(e, "and"))
    {
        std::vector<Predicate> result;
        for (size_t i = 1; i < e.children.size(); ++i)
        {
            const auto& child = e.children[i];
            if (tagged(child, "not"))
            {
                if (child.children.size() != 2)
                    lexer_error(lex, child.line, "(not ...) expects exactly one predicate");
                auto p = parse_predicate(child.children[1], lex);
                p.name = "not:" + p.name;
                result.push_back(std::move(p));
            }
            else
            {
                result.push_back(parse_predicate(child, lex));
            }
        }
        return result;
    }
    if (tagged(e, "not"))
    {
        if (e.children.size() != 2)
            lexer_error(lex, e.line, "(not ...) expects exactly one predicate");
        auto p = parse_predicate(e.children[1], lex);
        p.name = "not:" + p.name;
        return { p };
    }
    return { parse_predicate(e, lex) };
}

/// *****************************************************************************
/// Parse a list of effects from an S-expression.
/// @param e  The S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed list of effects.
/// *****************************************************************************
static std::vector<Effect> parse_effects(SExpr const& e, Lexer& lex)
{
    // TODO: (forall (?x - type) effect) universal effects
    // TODO: (when cond effect) conditional effects — partially handled in
    // Executor
    // TODO: (scale-up fluent factor) numeric scaling
    std::vector<Effect> result;
    auto process = [&](SExpr const& eff)
    {
        if (tagged(eff, "not"))
        {
            if (eff.children.size() != 2)
                lexer_error(lex, eff.line, "(not ...) expects exactly one predicate");
            result.push_back({ true, parse_predicate(eff.children[1], lex) });
        }
        else
        {
            result.push_back({ false, parse_predicate(eff, lex) });
        }
    };
    if (tagged(e, "and"))
    {
        for (size_t i = 1; i < e.children.size(); ++i)
        {
            process(e.children[i]);
        }
    }
    else
    {
        process(e);
    }
    return result;
}

/// *****************************************************************************
/// Parse a list of parameters from an S-expression.
/// @param e  The S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed list of parameters.
/// *****************************************************************************
static std::vector<Term> parse_parameters(SExpr const& e, Lexer& lex)
{
    // TODO: Store parameter types (e.g. ?a - agent) instead of skipping them
    // TODO: Support "either" types: ?x - (either type1 type2)
    std::vector<Term> params;
    for (size_t i = 0; i < e.children.size(); ++i)
    {
        if (e.children[i].atom == "-")
        {
            ++i;
            continue;
        }
        params.push_back(parse_term(e.children[i], lex));
    }
    return params;
}

/// *****************************************************************************
/// Parse an action from an S-expression.
/// @param e  The S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed action.
/// *****************************************************************************
static Action parse_action(SExpr const& e, Lexer& lex)
{
    // TODO: (:durative-action ...) with :duration, :condition, :effect
    // TODO: (at start ...), (at end ...), (over all ...) temporal annotations

    if (e.children.size() < 2)
        lexer_error(lex, e.line, ":action too short");
    Action a;
    a.line = e.line;
    a.name = e.children[1].atom;

    for (size_t i = 2; i + 1 < e.children.size(); i += 2)
    {
        const std::string& key = e.children[i].atom;
        const SExpr& val = e.children[i + 1];
        if (key == ":parameters")
            a.parameters = parse_parameters(val, lex);
        else if (key == ":precondition")
            a.preconditions = parse_predicate_list(val, lex);
        else if (key == ":effect")
            a.effects = parse_effects(val, lex);
        // TODO: Parse action cost from (increase (total-cost) N) in effects
    }
    return a;
}

/// *****************************************************************************
/// Parse a domain from an S-expression.
/// @param root  The root S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed domain.
/// *****************************************************************************
static Domain parse_domain(const SExpr& root, Lexer& lex)
{
    if (!tagged(root, "define"))
        lexer_error(lex, root.line, "expected (define ...)");

    Domain domain;
    if (root.children.size() > 1 && tagged(root.children[1], "domain"))
        domain.name = root.children[1].children[1].atom;

    for (size_t i = 2; i < root.children.size(); ++i)
    {
        const auto& section = root.children[i];

        if (tagged(section, ":requirements"))
        {
            for (size_t j = 1; j < section.children.size(); ++j)
                domain.requirements.push_back(section.children[j].atom);
        }
        // TODO: (:types ...) type hierarchy (e.g. agent - object)
        // TODO: (:constants ...) domain-level constant objects
        // TODO: (:functions ...) numeric function declarations
        // TODO: (:derived ...) derived predicates (axioms)
        else if (tagged(section, ":predicates"))
        {
            for (size_t j = 1; j < section.children.size(); ++j)
                domain.predicates.push_back(parse_predicate(section.children[j], lex));
        }
        else if (tagged(section, ":action"))
        {
            domain.actions.push_back(parse_action(section, lex));
        }
    }
    return domain;
}

/// *****************************************************************************
/// Parse a problem from an S-expression.
/// @param root  The root S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed problem.
/// *****************************************************************************
static WorldState parse_init(const SExpr& e, Lexer& lex)
{
    // TODO: (at t fact) timed initial literals for temporal planning
    WorldState ws;
    for (size_t i = 1; i < e.children.size(); ++i)
        ws.add(parse_predicate(e.children[i], lex));
    return ws;
}

/// *****************************************************************************
/// Parse a problem from an S-expression.
/// @param root  The root S-expression to parse.
/// @param lex  The lexer to use.
/// @return The parsed problem.
/// *****************************************************************************
static Problem parse_problem(SExpr const& root, Lexer& lex)
{
    if (!tagged(root, "define"))
        lexer_error(lex, root.line, "expected (define ...)");

    Problem problem;
    if (root.children.size() > 1 && tagged(root.children[1], "problem"))
    {
        problem.name = root.children[1].children[1].atom;
    }

    for (size_t i = 2; i < root.children.size(); ++i)
    {
        const auto& section = root.children[i];

        if (tagged(section, ":domain"))
        {
            problem.domain_name = section.children[1].atom;
        }
        else if (tagged(section, ":objects"))
        {
            // TODO: Store object types (e.g. alice - agent) instead of skipping
            for (size_t j = 1; j < section.children.size(); ++j)
            {
                if (section.children[j].atom == "-")
                {
                    ++j;
                    continue;
                }
                problem.objects.push_back(section.children[j].atom);
            }
        }
        else if (tagged(section, ":init"))
        {
            problem.init = parse_init(section, lex);
        }
        else if (tagged(section, ":goal"))
        {
            if (section.children.size() > 1)
                problem.goal = parse_predicate_list(section.children[1], lex);
        }
        // TODO: (:metric minimize/maximize expr) optimization metric
        // TODO: (:constraints ...) trajectory constraints
    }
    return problem;
}

/// *****************************************************************************
/// Read the entire contents of a file into a string.
/// @param path  The path to the file to read.
/// @return The contents of the file.
/// @throws std::runtime_error if the file cannot be opened.
/// *****************************************************************************
static std::string read_file(std::filesystem::path const& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        throw std::runtime_error("cannot open file: " + path.string());
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ******************************************************************************
Domain load_domain(std::filesystem::path const& path)
{
    std::string src = read_file(path);
    Lexer lex{ src, path };
    SExpr root = parse_sexpr(lex);
    return parse_domain(root, lex);
}

// ******************************************************************************
Problem load_problem(std::filesystem::path const& path)
{
    std::string src = read_file(path);
    Lexer lex{ src, path };
    SExpr root = parse_sexpr(lex);
    return parse_problem(root, lex);
}

} // namespace pddl::parser
