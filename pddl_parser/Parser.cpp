#include "Parser.hpp"
#include "SExpr.hpp"
#include <cctype>
#include <fstream>
#include <sstream>

namespace pddl::parser
{

/// *****************************************************************************
/// Convert an S-expression to a string (used for metric serialization).
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

/// Returns true if @p s is a well-formed integer or floating-point literal.
static bool is_number(std::string const& s)
{
    if (s.empty())
        return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size())
        return false;
    bool has_digit = false;
    bool has_dot   = false;
    for (; i < s.size(); ++i)
    {
        char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            has_digit = true;
        }
        else if (c == '.' && !has_dot)
        {
            has_dot = true;
        }
        else if ((c == 'e' || c == 'E') && has_digit)
        {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                ++i;
            bool exp_digit = false;
            for (; i < s.size(); ++i)
            {
                if (std::isdigit(static_cast<unsigned char>(s[i])))
                    exp_digit = true;
                else
                    return false;
            }
            return exp_digit;
        }
        else
        {
            return false;
        }
    }
    return has_digit;
}

/// *****************************************************************************
/// Parse a term in a numeric context.
///
/// Sets Term::numeric to a double literal or FluentRef so that callers never
/// need to re-parse the string at evaluation time.
/// *****************************************************************************
static Term parse_numeric_term(SExpr const& e)
{
    Term t;
    t.is_variable = false;

    if (e.is_atom)
    {
        t.name = e.atom;
        if (is_number(e.atom))
            t.numeric = std::stod(e.atom);
        // plain atom without numeric value: name holds it (e.g. a variable)
    }
    else
    {
        t.name = sexpr_to_string(e); // keep original text for debugging / metric use
        if (!e.children.empty() && e.children[0].is_atom)
        {
            FluentRef ref;
            ref.func = e.children[0].atom;
            for (size_t i = 1; i < e.children.size(); ++i)
                ref.args.push_back(e.children[i].atom);
            t.numeric = std::move(ref);
        }
    }
    return t;
}

/// *****************************************************************************
/// Parse a plain term from an S-expression (atom or serialized sub-expr).
/// *****************************************************************************
static Term parse_term(SExpr const& e, [[maybe_unused]] Lexer& lex)
{
    if (e.is_atom)
        return { e.atom, /*type=*/"", e.atom.starts_with('?') };
    return { sexpr_to_string(e), /*type=*/"", false };
}

/// Returns true for predicate names whose arguments are numeric expressions.
static bool is_numeric_predicate(std::string const& name)
{
    return name == ">=" || name == ">" || name == "<" || name == "<=" ||
           name == "=" || name == "increase" || name == "decrease" || name == "assign";
}

/// Map a predicate name to its NumericOp (None if not a numeric mutation effect).
static NumericOp numeric_op_of(std::string const& name)
{
    if (name == "increase") return NumericOp::Increase;
    if (name == "decrease") return NumericOp::Decrease;
    if (name == "assign")   return NumericOp::Assign;
    return NumericOp::None;
}

/// *****************************************************************************
/// Parse a predicate from an S-expression.
///
/// Arguments of numeric predicates are parsed with parse_numeric_term so that
/// FluentRef / double values are available without any runtime re-parsing.
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

    const bool numeric = is_numeric_predicate(p.name);
    for (size_t i = 1; i < e.children.size(); ++i)
    {
        if (numeric)
            p.args.push_back(parse_numeric_term(e.children[i]));
        else
            p.args.push_back(parse_term(e.children[i], lex));
    }
    return p;
}

/// *****************************************************************************
/// Parse a list of precondition predicates from an S-expression.
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
/// *****************************************************************************
static std::vector<Effect> parse_effects(SExpr const& e, Lexer& lex)
{
    // TODO: (forall (?x - type) effect) universal effects
    // TODO: (scale-up fluent factor) / (scale-down fluent factor) numeric scaling
    std::vector<Effect> result;
    auto process = [&](SExpr const& eff)
    {
        if (tagged(eff, "not"))
        {
            if (eff.children.size() != 2)
                lexer_error(lex, eff.line, "(not ...) expects exactly one predicate");
            Effect e;
            e.is_negated = true;
            e.predicate  = parse_predicate(eff.children[1], lex);
            result.push_back(std::move(e));
        }
        else if (tagged(eff, "when"))
        {
            if (eff.children.size() != 3)
                lexer_error(lex, eff.line, "(when ...) expects a condition and an effect");
            Effect cond_eff;
            cond_eff.when_condition = parse_predicate(eff.children[1], lex);
            const SExpr& consequent = eff.children[2];
            if (tagged(consequent, "not"))
            {
                if (consequent.children.size() != 2)
                    lexer_error(lex, consequent.line, "(not ...) expects exactly one predicate");
                cond_eff.is_negated = true;
                cond_eff.predicate  = parse_predicate(consequent.children[1], lex);
            }
            else
            {
                cond_eff.is_negated = false;
                cond_eff.predicate  = parse_predicate(consequent, lex);
                cond_eff.numeric_op = numeric_op_of(cond_eff.predicate.name);
            }
            result.push_back(std::move(cond_eff));
        }
        else
        {
            Effect e;
            e.predicate  = parse_predicate(eff, lex);
            e.numeric_op = numeric_op_of(e.predicate.name);
            result.push_back(std::move(e));
        }
    };
    if (tagged(e, "and"))
    {
        for (size_t i = 1; i < e.children.size(); ++i)
            process(e.children[i]);
    }
    else
    {
        process(e);
    }
    return result;
}

/// *****************************************************************************
/// Parse a typed list of terms (params or objects).
///
/// Handles: ?x ?y - type1  ?z - type2
/// Returns one Term per name with the type field filled in.
///
/// @param e      The S-expression whose children form the typed list.
/// @param lex    The lexer (for error reporting).
/// @param start  First child index to process (0 for inner param lists,
///               1 for full section nodes that begin with a keyword tag).
/// *****************************************************************************
static std::vector<Term> parse_typed_terms(SExpr const& e, Lexer& lex, size_t start = 0)
{
    // TODO: Support "either" types: ?x - (either type1 type2)
    std::vector<Term> terms;
    std::vector<SExpr const*> pending; // terms waiting for a type declaration

    for (size_t i = start; i < e.children.size(); ++i)
    {
        const SExpr& tok = e.children[i];
        if (tok.atom == "-")
        {
            ++i;
            if (i >= e.children.size())
                lexer_error(lex, tok.line, "expected type name after '-'");
            std::string type_name = e.children[i].atom;
            for (auto* s : pending)
                terms.push_back({ s->atom, type_name, s->atom.starts_with('?') });
            pending.clear();
        }
        else
        {
            pending.push_back(&tok);
        }
    }
    // Remaining terms without explicit type
    for (auto* s : pending)
        terms.push_back({ s->atom, /*type=*/"", s->atom.starts_with('?') });

    return terms;
}

/// *****************************************************************************
/// Parse an action from an S-expression.
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
            a.parameters = parse_typed_terms(val, lex);
        else if (key == ":precondition")
            a.preconditions = parse_predicate_list(val, lex);
        else if (key == ":effect")
        {
            a.effects = parse_effects(val, lex);

            // Extract action cost from (increase (total-cost) N) effect
            for (const auto& eff : a.effects)
            {
                const Predicate& p = eff.predicate;
                if (!eff.is_negated && p.name == "increase" && p.args.size() >= 2)
                {
                    const std::string& target = p.args[0].name;
                    if (target == "(total-cost)" || target == "total-cost")
                    {
                        try
                        {
                            a.cost = static_cast<int>(std::stod(p.args[1].name));
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
        }
    }
    return a;
}

/// *****************************************************************************
/// Parse the :types section into a list of TypeDef.
///
/// Input: (type1 type2 - parent  type3)
/// *****************************************************************************
static std::vector<TypeDef> parse_types(SExpr const& e)
{
    std::vector<TypeDef> result;
    std::vector<std::string> pending;

    for (size_t i = 1; i < e.children.size(); ++i)
    {
        const SExpr& tok = e.children[i];
        if (tok.atom == "-")
        {
            ++i;
            if (i >= e.children.size())
                break;
            std::string parent = e.children[i].atom;
            for (const auto& name : pending)
                result.push_back({ name, parent });
            pending.clear();
        }
        else
        {
            pending.push_back(tok.atom);
        }
    }
    for (const auto& name : pending)
        result.push_back({ name, /*parent=*/"" });

    return result;
}

/// *****************************************************************************
/// Parse a domain from an S-expression.
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
        else if (tagged(section, ":types"))
        {
            domain.types = parse_types(section);
        }
        else if (tagged(section, ":constants"))
        {
            // start=1 to skip the ":constants" tag in children[0]
            auto terms = parse_typed_terms(section, lex, /*start=*/1);
            for (const auto& t : terms)
                domain.constants.push_back(t.name);
        }
        else if (tagged(section, ":predicates"))
        {
            for (size_t j = 1; j < section.children.size(); ++j)
                domain.predicates.push_back(parse_predicate(section.children[j], lex));
        }
        else if (tagged(section, ":functions"))
        {
            // Store function signatures (numeric or object fluents)
            for (size_t j = 1; j < section.children.size(); ++j)
            {
                if (!section.children[j].is_atom)
                    domain.functions.push_back(parse_predicate(section.children[j], lex));
                // Skip "-" and type annotations between function signatures
            }
        }
        else if (tagged(section, ":derived"))
        {
            // (:derived (head args…) body)
            if (section.children.size() >= 3)
            {
                DerivedPredicate dp;
                dp.head = parse_predicate(section.children[1], lex);
                dp.body = parse_predicate_list(section.children[2], lex);
                domain.derived.push_back(std::move(dp));
            }
        }
        else if (tagged(section, ":action"))
        {
            domain.actions.push_back(parse_action(section, lex));
        }
        // :durative-action — TODO: temporal planning support
    }
    return domain;
}

/// *****************************************************************************
/// Parse the :init section into a WorldState.
/// *****************************************************************************
static WorldState parse_init(const SExpr& e, Lexer& lex)
{
    WorldState ws;
    for (size_t i = 1; i < e.children.size(); ++i)
    {
        const SExpr& child = e.children[i];
        // (at t fact) timed initial literals — skip silently
        if (tagged(child, "at"))
            continue;
        ws.add(parse_predicate(child, lex));
    }
    return ws;
}

/// *****************************************************************************
/// Parse a problem from an S-expression.
/// *****************************************************************************
static Problem parse_problem(SExpr const& root, Lexer& lex)
{
    if (!tagged(root, "define"))
        lexer_error(lex, root.line, "expected (define ...)");

    Problem problem;
    if (root.children.size() > 1 && tagged(root.children[1], "problem"))
        problem.name = root.children[1].children[1].atom;

    for (size_t i = 2; i < root.children.size(); ++i)
    {
        const auto& section = root.children[i];

        if (tagged(section, ":domain"))
        {
            problem.domain_name = section.children[1].atom;
        }
        else if (tagged(section, ":objects"))
        {
            auto terms = parse_typed_terms(section, lex);
            for (const auto& t : terms)
                problem.objects.push_back(t.name);
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
        else if (tagged(section, ":metric"))
        {
            // Serialize the full metric expression as a string (e.g. "minimize (total-cost)")
            std::string m;
            for (size_t j = 1; j < section.children.size(); ++j)
            {
                if (j > 1)
                    m += " ";
                m += sexpr_to_string(section.children[j]);
            }
            problem.metric = std::move(m);
        }
        // :constraints — skip silently (trajectory constraints not supported)
    }
    return problem;
}

/// *****************************************************************************
/// Read the entire contents of a file into a string.
/// *****************************************************************************
static std::string read_file(std::filesystem::path const& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("cannot open file: " + path.string());
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
