#include "Lexer.hpp"
#include <cctype>
#include <format>
#include <stdexcept>

namespace pddl::parser
{

[[noreturn]] void
lexer_error(const Lexer& lex, int err_line, const std::string& msg)
{
    throw std::runtime_error(
        std::format("{}:{}: {}", lex.filename, err_line, msg));
}

static void skip_ws(Lexer& lex)
{
    while (lex.pos < lex.src.size())
    {
        if (lex.src[lex.pos] == '\n')
        {
            ++lex.line;
            ++lex.pos;
        }
        else if (std::isspace(static_cast<unsigned char>(lex.src[lex.pos])))
        {
            ++lex.pos;
        }
        else if (lex.src[lex.pos] == ';')
        {
            while (lex.pos < lex.src.size() && lex.src[lex.pos] != '\n')
                ++lex.pos;
        }
        else
            break;
    }
}

Token next_token(Lexer& lex)
{
    skip_ws(lex);
    if (lex.pos >= lex.src.size())
        return { "", lex.line };

    int tok_line = lex.line;
    if (lex.src[lex.pos] == '(' || lex.src[lex.pos] == ')')
        return { std::string(1, lex.src[lex.pos++]), tok_line };

    size_t start = lex.pos;
    while (lex.pos < lex.src.size() &&
           !std::isspace(static_cast<unsigned char>(lex.src[lex.pos])) &&
           lex.src[lex.pos] != '(' && lex.src[lex.pos] != ')')
        ++lex.pos;
    return { std::string(lex.src.substr(start, lex.pos - start)), tok_line };
}

Token peek_token(Lexer& lex)
{
    size_t save_pos = lex.pos;
    int save_line = lex.line;
    auto t = next_token(lex);
    lex.pos = save_pos;
    lex.line = save_line;
    return t;
}

} // namespace pddl::parser
