cmake_minimum_required(VERSION 3.20)
project(pddl_parser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Parser library ─────────────────────────────────────────────────────────────
# Low-level PDDL front-end: lexer, S-expression parser, AST, domain/problem loader.
# Has no dependency on any planner.
add_library(pddl_parser_lib STATIC
    Lexer.cpp
    SExpr.cpp
    AST.cpp
    Parser.cpp
)
target_include_directories(pddl_parser_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── Solver library ─────────────────────────────────────────────────────────────
# Concrete planners.  Depends on pddl_parser_lib.
# Add new solvers here (e.g. OrderedGoalsSolver.cpp) as they are implemented.
add_library(pddl_solver_lib STATIC
    AStarSolver.cpp
)
target_include_directories(pddl_solver_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pddl_solver_lib PUBLIC pddl_parser_lib)

# ── Main executable ────────────────────────────────────────────────────────────
add_executable(pddl_planner main.cpp)
target_link_libraries(pddl_planner PRIVATE pddl_solver_lib)
