#include "Parser.hpp"
#include "SExpr.hpp"
#include <fstream>
#include <sstream>

namespace pddl::parser
{

// ── CST → AST helpers ────────────────────────────────────────────────

static std::string sexpr_to_string(const SExpr& e)
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

static Term parse_term(const SExpr& e, [[maybe_unused]] Lexer& lex)
{
    if (e.is_atom)
        return { e.atom, e.atom.starts_with('?') };
    return { sexpr_to_string(e), false };
}

static Predicate parse_predicate(const SExpr& e, Lexer& lex)
{
    if (e.is_atom)
        return { e.atom, {}, e.line };
    if (e.children.empty())
        lexer_error(lex, e.line, "expected predicate list");
    Predicate p;
    p.line = e.line;
    p.name = e.children[0].is_atom ? e.children[0].atom
                                   : sexpr_to_string(e.children[0]);
    for (size_t i = 1; i < e.children.size(); ++i)
        p.args.push_back(parse_term(e.children[i], lex));
    return p;
}

static std::vector<Predicate> parse_predicate_list(const SExpr& e, Lexer& lex)
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
                    lexer_error(lex,
                                child.line,
                                "(not ...) expects exactly one predicate");
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

static std::vector<Effect> parse_effects(const SExpr& e, Lexer& lex)
{
    // TODO: (forall (?x - type) effect) universal effects
    // TODO: (when cond effect) conditional effects — partially handled in
    // Executor
    // TODO: (scale-up fluent factor) numeric scaling
    std::vector<Effect> result;
    auto process = [&](const SExpr& eff)
    {
        if (tagged(eff, "not"))
        {
            if (eff.children.size() != 2)
                lexer_error(
                    lex, eff.line, "(not ...) expects exactly one predicate");
            result.push_back({ true, parse_predicate(eff.children[1], lex) });
        }
        else
        {
            result.push_back({ false, parse_predicate(eff, lex) });
        }
    };
    if (tagged(e, "and"))
        for (size_t i = 1; i < e.children.size(); ++i)
            process(e.children[i]);
    else
        process(e);
    return result;
}

static std::vector<Term> parse_parameters(const SExpr& e, Lexer& lex)
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

// ── Action parsing ───────────────────────────────────────────────────

// TODO: (:durative-action ...) with :duration, :condition, :effect
// TODO: (at start ...), (at end ...), (over all ...) temporal annotations
static Action parse_action(const SExpr& e, Lexer& lex)
{
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

// ── Domain parsing ───────────────────────────────────────────────────

static Domain parse_domain(const SExpr& root, Lexer& lex)
{
    if (!tagged(root, "define"))
        lexer_error(lex, root.line, "expected (define ...)");

    Domain d;
    if (root.children.size() > 1 && tagged(root.children[1], "domain"))
        d.name = root.children[1].children[1].atom;

    for (size_t i = 2; i < root.children.size(); ++i)
    {
        const auto& section = root.children[i];

        if (tagged(section, ":requirements"))
        {
            for (size_t j = 1; j < section.children.size(); ++j)
                d.requirements.push_back(section.children[j].atom);
        }
        // TODO: (:types ...) type hierarchy (e.g. agent - object)
        // TODO: (:constants ...) domain-level constant objects
        // TODO: (:functions ...) numeric function declarations
        // TODO: (:derived ...) derived predicates (axioms)
        else if (tagged(section, ":predicates"))
        {
            for (size_t j = 1; j < section.children.size(); ++j)
                d.predicates.push_back(
                    parse_predicate(section.children[j], lex));
        }
        else if (tagged(section, ":action"))
        {
            d.actions.push_back(parse_action(section, lex));
        }
    }
    return d;
}

// ── Problem parsing ──────────────────────────────────────────────────

static WorldState parse_init(const SExpr& e, Lexer& lex)
{
    // TODO: (at t fact) timed initial literals for temporal planning
    WorldState ws;
    for (size_t i = 1; i < e.children.size(); ++i)
        ws.add(parse_predicate(e.children[i], lex));
    return ws;
}

static Problem parse_problem(const SExpr& root, Lexer& lex)
{
    if (!tagged(root, "define"))
        lexer_error(lex, root.line, "expected (define ...)");

    Problem prob;
    if (root.children.size() > 1 && tagged(root.children[1], "problem"))
        prob.name = root.children[1].children[1].atom;

    for (size_t i = 2; i < root.children.size(); ++i)
    {
        const auto& section = root.children[i];

        if (tagged(section, ":domain"))
        {
            prob.domain_name = section.children[1].atom;
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
                prob.objects.push_back(section.children[j].atom);
            }
        }
        else if (tagged(section, ":init"))
        {
            prob.init = parse_init(section, lex);
        }
        else if (tagged(section, ":goal"))
        {
            if (section.children.size() > 1)
                prob.goal = parse_predicate_list(section.children[1], lex);
        }
        // TODO: (:metric minimize/maximize expr) optimization metric
        // TODO: (:constraints ...) trajectory constraints
    }
    return prob;
}

// ── Convenience: parse from file ─────────────────────────────────────

/// Read the entire contents of a file into a string.
/// @throws std::runtime_error if the file cannot be opened.
static std::string read_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("cannot open file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Domain load_domain(const std::string& path)
{
    std::string src = read_file(path);
    Lexer lex{ src, path };
    auto root = parse_sexpr(lex);
    return parse_domain(root, lex);
}

Problem load_problem(const std::string& path)
{
    std::string src = read_file(path);
    Lexer lex{ src, path };
    auto root = parse_sexpr(lex);
    return parse_problem(root, lex);
}

} // namespace pddl::parser
