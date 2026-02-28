#include <cctype>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

// ─── Lexer
// ────────────────────────────────────────────────────────────────────

struct Token
{
    std::string value;
    int line;
};

struct Lexer
{
    std::string_view src;
    std::string filename;
    size_t pos = 0;
    int line = 1;

    void skip_ws()
    {
        while (pos < src.size())
        {
            if (src[pos] == '\n')
            {
                ++line;
                ++pos;
            }
            else if (std::isspace(src[pos]))
            {
                ++pos;
            }
            else if (src[pos] == ';')
            {
                while (pos < src.size() && src[pos] != '\n')
                    ++pos;
            }
            else
                break;
        }
    }

    Token next_token()
    {
        skip_ws();
        if (pos >= src.size())
            return { "", line };
        int tok_line = line;
        if (src[pos] == '(' || src[pos] == ')')
            return { std::string(1, src[pos++]), tok_line };
        size_t start = pos;
        while (pos < src.size() && !std::isspace(src[pos]) && src[pos] != '(' &&
               src[pos] != ')')
            ++pos;
        return { std::string(src.substr(start, pos - start)), tok_line };
    }

    Token peek()
    {
        size_t save_pos = pos;
        int save_line = line;
        auto t = next_token();
        pos = save_pos;
        line = save_line;
        return t;
    }

    [[noreturn]] void error(int err_line, const std::string& msg) const
    {
        throw std::runtime_error(
            std::format("{}:{}: error: {}", filename, err_line, msg));
    }
};

// ─── CST (S-expressions)
// ──────────────────────────────────────────────────────

struct SExpr
{
    bool is_atom;
    std::string atom;
    std::vector<SExpr> children;
    int line = 0;

    // Helpers
    bool tagged(const std::string& tag) const
    {
        return !is_atom && !children.empty() && children[0].is_atom &&
               children[0].atom == tag;
    }

    const SExpr& operator[](size_t i) const
    {
        return children[i];
    }
    size_t size() const
    {
        return children.size();
    }
};

SExpr parse_sexpr(Lexer& lex)
{
    auto tok = lex.next_token();
    if (tok.value == "")
        lex.error(tok.line, "unexpected end of file");
    if (tok.value == ")")
        lex.error(tok.line, "unexpected ')'");

    if (tok.value == "(")
    {
        SExpr node{ false, {}, {}, tok.line };
        while (true)
        {
            auto p = lex.peek();
            if (p.value == "")
                lex.error(p.line, "unclosed '('");
            if (p.value == ")")
            {
                lex.next_token();
                break;
            }
            node.children.push_back(parse_sexpr(lex));
        }
        return node;
    }
    return SExpr{ true, tok.value, {}, tok.line };
}

// ─── AST
// ──────────────────────────────────────────────────────────────────────

struct Term
{
    std::string name; // ex: "?x", "block-a"
    bool is_variable; // commence par '?'
};

struct Predicate
{
    std::string name;
    std::vector<Term> args;
    int line = 0;
};

// Effet d'une action : add ou delete
struct Effect
{
    bool is_negated; // true => (not (...))
    Predicate predicate;
};

struct Action
{
    std::string name;
    std::vector<Term> parameters;
    std::vector<Predicate> preconditions;
    std::vector<Effect> effects;
    int line = 0;
};

// État du monde : ensemble de prédicats ground (sans variables)
struct WorldState
{
    std::vector<Predicate> facts;

    bool holds(const std::string& pred_name,
               const std::vector<std::string>& args) const
    {
        for (auto& f : facts)
        {
            if (f.name != pred_name || f.args.size() != args.size())
                continue;
            bool match = true;
            for (size_t i = 0; i < args.size(); ++i)
                if (f.args[i].name != args[i])
                {
                    match = false;
                    break;
                }
            if (match)
                return true;
        }
        return false;
    }
};

// ─── CST → AST helpers
// ────────────────────────────────────────────────────────

Term parse_term(const SExpr& e, Lexer& lex)
{
    if (!e.is_atom)
        lex.error(e.line, "expected a term (atom), got a list");
    return { e.atom, e.atom.starts_with('?') };
}

// Parse (predicate-name ?a ?b ...) ou (predicate-name obj1 obj2 ...)
Predicate parse_predicate(const SExpr& e, Lexer& lex)
{
    if (e.is_atom || e.size() == 0)
        lex.error(e.line, "expected predicate list");
    Predicate p;
    p.line = e.line;
    p.name = e[0].atom;
    for (size_t i = 1; i < e.size(); ++i)
        p.args.push_back(parse_term(e[i], lex));
    return p;
}

// Parse (and (p1 ...) (p2 ...) ...) ou directement (p ...)
std::vector<Predicate> parse_predicate_list(const SExpr& e, Lexer& lex)
{
    if (e.tagged("and"))
    {
        std::vector<Predicate> result;
        for (size_t i = 1; i < e.size(); ++i)
            result.push_back(parse_predicate(e[i], lex));
        return result;
    }
    return { parse_predicate(e, lex) };
}

// Parse (and (p) (not (p)) ...)
std::vector<Effect> parse_effects(const SExpr& e, Lexer& lex)
{
    std::vector<Effect> result;
    auto process = [&](const SExpr& eff)
    {
        if (eff.tagged("not"))
        {
            if (eff.size() != 2)
                lex.error(eff.line, "(not ...) expects exactly one predicate");
            result.push_back({ true, parse_predicate(eff[1], lex) });
        }
        else
        {
            result.push_back({ false, parse_predicate(eff, lex) });
        }
    };
    if (e.tagged("and"))
        for (size_t i = 1; i < e.size(); ++i)
            process(e[i]);
    else
        process(e);
    return result;
}

// Parse les paramètres : (?x ?y - type) — on ignore les types pour l'instant
std::vector<Term> parse_parameters(const SExpr& e, Lexer& lex)
{
    std::vector<Term> params;
    for (size_t i = 0; i < e.size(); ++i)
    {
        if (e[i].atom == "-")
        {
            ++i;
            continue;
        } // skip "- type"
        params.push_back(parse_term(e[i], lex));
    }
    return params;
}

// ─── Parsing domain
// ───────────────────────────────────────────────────────────

Action parse_action(const SExpr& e, Lexer& lex)
{
    // (:action name :parameters (...) :precondition (...) :effect (...))
    if (e.size() < 2)
        lex.error(e.line, ":action too short");
    Action a;
    a.line = e.line;
    a.name = e[1].atom;

    for (size_t i = 2; i + 1 < e.size(); i += 2)
    {
        const std::string& key = e[i].atom;
        const SExpr& val = e[i + 1];
        if (key == ":parameters")
            a.parameters = parse_parameters(val, lex);
        else if (key == ":precondition")
            a.preconditions = parse_predicate_list(val, lex);
        else if (key == ":effect")
            a.effects = parse_effects(val, lex);
    }
    return a;
}

WorldState parse_init(const SExpr& e, Lexer& lex)
{
    // (:init (p a b) (p c) ...)
    WorldState ws;
    for (size_t i = 1; i < e.size(); ++i)
        ws.facts.push_back(parse_predicate(e[i], lex));
    return ws;
}

struct Domain
{
    std::string name;
    std::vector<Action> actions;
};

Domain parse_domain(const SExpr& root, Lexer& lex)
{
    if (!root.tagged("define"))
        lex.error(root.line, "expected (define ...)");
    Domain d;
    // (define (domain name) ...)
    if (root.size() > 1 && root[1].tagged("domain"))
        d.name = root[1][1].atom;

    for (size_t i = 2; i < root.size(); ++i)
    {
        if (root[i].tagged(":action"))
            d.actions.push_back(parse_action(root[i], lex));
    }
    return d;
}