/// @file SExpr.hpp
/// S-expression tree (concrete syntax tree) for PDDL.
#pragma once
#include "Lexer.hpp"
#include <string>
#include <vector>

namespace pddl::parser
{

/// A node in an S-expression tree.
///
/// Either an atom (leaf with a string value) or a list of child S-expressions.
struct SExpr
{
    bool is_atom = false; ///< True if this node is a leaf atom.
    std::string atom; ///< Atom text (meaningful only when @c is_atom is true).
    std::vector<SExpr>
        children; ///< Child nodes (meaningful only when @c is_atom is false).
    int line = 0; ///< Source line where this expression starts.
};

/// Check whether an S-expression is a list whose first child is @p tag.
/// @param e    The S-expression to inspect.
/// @param tag  The expected tag string (e.g. "define", ":action").
/// @return True if @p e is a list and its first atom equals @p tag.
bool tagged(const SExpr& e, const std::string& tag);

/// Recursively parse one S-expression from the token stream.
/// @param lex  Lexer to read tokens from.
/// @return The parsed S-expression tree.
/// @throws std::runtime_error on unexpected EOF or mismatched parentheses.
SExpr parse_sexpr(Lexer& lex);

} // namespace pddl::parser
