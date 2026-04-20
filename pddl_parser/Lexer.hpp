/// ****************************************************************************
/// @file Lexer.hpp
/// Lexer for PDDL files — splits source text into tokens.
/// ****************************************************************************

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace pddl::parser
{

/// *****************************************************************************
/// A single lexical token extracted from source text.
/// *****************************************************************************
struct Token
{
    std::string text; ///< Token content (e.g. "(", ")", ":action", "?x").
    size_t line = 0;  ///< Source line where this token starts.
};

/// *****************************************************************************
/// Lexer state: tracks position within a source buffer.
/// *****************************************************************************
struct Lexer
{
    std::string_view src;           ///< Full source text (must outlive the Lexer).
    std::filesystem::path filename; ///< Filename used in error messages.
    size_t pos = 0;                 ///< Current byte offset in @c src.
    size_t line = 1;                ///< Current line number (1-based).
};

/// *****************************************************************************
/// Throw a std::runtime_error with file/line context.
/// @param lex   Lexer providing the filename.
/// @param err_line  Line number to report.
/// @param msg   Human-readable error description.
/// *****************************************************************************
[[noreturn]] void lexer_error(Lexer const& lex, size_t err_line, std::string const& msg);

/// *****************************************************************************
/// Consume and return the next token, advancing the lexer.
/// @param lex  The lexer to advance.
/// @return The next token.
/// *****************************************************************************
Token next_token(Lexer& lex);

/// *****************************************************************************
/// Return the next token without advancing the lexer.
/// @param lex  The lexer to peek.
/// @return The next token.
/// *****************************************************************************
Token peek_token(Lexer& lex);

} // namespace pddl::parser
