/// ****************************************************************************
/// @file Parser.hpp
/// PDDL parser: converts S-expression trees into AST structures.
///
/// Handles both classical (STRIPS) and numeric (PDDL 2.1) constructs.
/// Numeric expressions like @c (>= (money ?a) 10000) are represented as
/// generic predicates with serialized sub-expression arguments.
/// ****************************************************************************

#pragma once

#include "AST.hpp"
#include <filesystem>
namespace pddl::parser
{

/// *****************************************************************************
/// Load and parse a PDDL domain file.
/// @param path  Filesystem path to the domain file.
/// @return Parsed Domain structure.
/// *****************************************************************************
Domain load_domain(std::filesystem::path const& path);

/// *****************************************************************************
/// Load and parse a PDDL problem file.
/// @param path  Filesystem path to the problem file.
/// @return Parsed Problem structure.
/// *****************************************************************************
Problem load_problem(std::filesystem::path const& path);

} // namespace pddl::parser
