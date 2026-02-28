/// @file Lexer.hpp
/// Lexer for PDDL files — splits source text into tokens.
#pragma once
#include <string>
#include <string_view>

namespace pddl::parser
{

/// A single lexical token extracted from source text.
struct Token
{
    std::string text; ///< Token content (e.g. "(", ")", ":action", "?x").
    int line = 0;     ///< Source line where this token starts.
};

/// Lexer state — tracks position within a source buffer.
struct Lexer
{
    std::string_view src; ///< Full source text (must outlive the Lexer).
    std::string filename; ///< Filename used in error messages.
    size_t pos = 0;       ///< Current byte offset in @c src.
    int line = 1;         ///< Current line number (1-based).
};

/// Throw a std::runtime_error with file/line context.
/// @param lex   Lexer providing the filename.
/// @param err_line  Line number to report.
/// @param msg   Human-readable error description.
[[noreturn]] void
lexer_error(const Lexer& lex, int err_line, const std::string& msg);

/// Consume and return the next token, advancing the lexer.
/// @return An empty-text Token at EOF.
Token next_token(Lexer& lex);

/// Return the next token without advancing the lexer.
Token peek_token(Lexer& lex);

} // namespace pddl::parser
