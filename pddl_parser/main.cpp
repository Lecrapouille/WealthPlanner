#include "AStarSolver.hpp"
#include "Parser.hpp"
#include <cstring>
#include <iomanip>
#include <iostream>

namespace parser = pddl::parser;
namespace solver = pddl::solver;

static void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog << " -d <domain.pddl> -p <problem.pddl> [-h]\n"
              << "Options:\n"
              << "  -d <file>   Domain PDDL file\n"
              << "  -p <file>   Problem PDDL file\n"
              << "  -h          Show this help\n";
}

int main(int argc, char* argv[])
{
    const char* domain_path = nullptr;
    const char* problem_path = nullptr;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            domain_path = argv[++i];
        else if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            problem_path = argv[++i];
        else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Check if domain and problem paths are provided
    if (!domain_path || !problem_path)
    {
        std::cerr << "Error: both -d and -p are required\n";
        print_usage(argv[0]);
        return 1;
    }

    try
    {
        auto domain = parser::load_domain(domain_path);
        auto problem = parser::load_problem(problem_path);

        // Grounding
        auto actions = solver::AStarSolver::instantiate_actions(domain, problem);
        auto derived = solver::AStarSolver::instantiate_derived(domain, problem);

        // Initial state
        auto initial = solver::AStarSolver::build_initial_state(problem);
        initial = solver::AStarSolver::expand_derived(initial, derived);

        // Planning
        solver::AStarConfig config;
        config.verbose = false;
        config.fluent_bucket_size = 10;

        solver::AStarSolver planner(config);
        solver::SolverContext ctx{ initial, actions, problem.goal, derived };
        auto result = planner.solve(ctx);

        // Result
        if (!result.success)
        {
            std::cout << "No plan found after " << result.iterations << " iterations.\n";
            return EXIT_FAILURE;
        }

        std::cout << "Plan (" << result.plan.size() << " steps, " << result.iterations << " iterations):\n";
        for (size_t i = 0; i < result.plan.size(); ++i)
            std::cout << "  " << std::setw(3) << (i + 1) << ": " << result.plan[i] << std::endl;
        std::cout << "Goal reached: " << (result.final_state.is_goal_reached(problem.goal) ? "YES" : "NO") << "\n";
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
