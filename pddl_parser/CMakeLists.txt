cmake_minimum_required(VERSION 3.20)
project(pddl_parser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(pddl_parser
    main.cpp
    Lexer.cpp
    SExpr.cpp
    AST.cpp
    Parser.cpp
    Executor.cpp
)
