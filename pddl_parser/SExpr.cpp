#include "SExpr.hpp"

namespace pddl::parser {

bool tagged(const SExpr& e, const std::string& tag)
{
    return !e.is_atom && !e.children.empty() && e.children[0].is_atom &&
           e.children[0].atom == tag;
}

SExpr parse_sexpr(Lexer& lex)
{
    auto tok = next_token(lex);
    if (tok.text.empty())
        lexer_error(lex, tok.line, "unexpected end of file");
    if (tok.text == ")")
        lexer_error(lex, tok.line, "unexpected ')'");

    if (tok.text == "(")
    {
        SExpr node{ false, {}, {}, tok.line };
        while (true)
        {
            auto p = peek_token(lex);
            if (p.text.empty())
                lexer_error(lex, p.line, "unclosed '('");
            if (p.text == ")")
            {
                next_token(lex);
                break;
            }
            node.children.push_back(parse_sexpr(lex));
        }
        return node;
    }
    return SExpr{ true, tok.text, {}, tok.line };
}

} // namespace pddl::parser
