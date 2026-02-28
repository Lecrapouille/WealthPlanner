#include "Parser.hpp"
#include "Executor.hpp"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace parser = pddl::parser;
namespace solver = pddl::solver;

static void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " -d <domain.pddl> -p <problem.pddl> [-v]\n"
              << "Options:\n"
              << "  -d <file>   Domain PDDL file\n"
              << "  -p <file>   Problem PDDL file\n"
              << "  -v          Verbose mode (debug output)\n"
              << "  -h          Show this help\n";
}

static void print_state(const parser::WorldState& ws)
{
    std::cout << "  Fluents:\n";
    for (const auto& [key, val] : ws.get_fluents())
        std::cout << "    " << key << " = " << val << "\n";

    std::cout << "  Facts:\n";
    for (const auto& f : ws.get_facts())
    {
        std::cout << "    (" << f.name;
        for (const auto& a : f.args)
            std::cout << " " << a.name;
        std::cout << ")\n";
    }
}

int main(int argc, char* argv[])
{
    const char* domain_path = nullptr;
    const char* problem_path = nullptr;
    bool verbose = false;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            domain_path = argv[++i];
        else if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            problem_path = argv[++i];
        else if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else if (std::strcmp(argv[i], "-h") == 0 ||
                 std::strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

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

        std::cout << "=== Domain: " << domain.name << " ===\n";
        std::cout << "Actions templates: " << domain.actions.size() << "\n\n";

        std::cout << "=== Problem: " << problem.name << " ===\n";
        std::cout << "Objects: ";
        for (const auto& o : problem.objects)
            std::cout << o << " ";
        std::cout << "\n\n";

        auto actions = solver::Executor::instantiate_actions(domain, problem);
        std::cout << "=== Ground Actions (" << actions.size() << ") ===\n";
        for (const auto& a : actions)
            std::cout << "  " << a.name << " (cost=" << a.cost << ")\n";
        std::cout << "\n";

        auto initial = solver::Executor::build_initial_state(problem);
        std::cout << "=== Initial State ===\n";
        print_state(initial);
        std::cout << "\n";

        std::cout << "=== A* Planning ===\n";
        solver::PlannerConfig config;
        config.verbose = verbose;
        config.fluent_bucket_size = 10; // bucket by 10 (good for health 0-100)

        auto result = solver::Executor::plan(initial, actions, problem.goal, config);

        if (!result.success)
        {
            std::cout << "No plan found after " << result.iterations
                      << " iterations.\n";
            return 1;
        }

        std::cout << "Plan found! " << result.plan.size() << " steps, "
                  << result.iterations << " iterations\n\n";

        std::cout << "=== Plan Execution ===\n";
        const int wNum = 4, wAction = 30;
        std::cout << std::left << std::setw(wNum) << "#" << std::setw(wAction)
                  << "Action" << "State\n";
        std::cout << std::string(60, '-') << "\n";

        std::cout << std::left << std::setw(wNum) << 0 << std::setw(wAction)
                  << "(initial)";
        for (const auto& [key, val] : initial.get_fluents())
            std::cout << " " << key << "=" << val;
        std::cout << "\n";

        parser::WorldState state = initial;
        for (size_t i = 0; i < result.plan.size(); ++i)
        {
            const std::string& action_name = result.plan[i];
            auto it = std::find_if(actions.begin(), actions.end(),
                                   [&](const solver::GroundAction& a)
                                   { return a.name == action_name; });
            if (it != actions.end())
                state = solver::Executor::apply_action(*it, state);

            std::cout << std::left << std::setw(wNum) << (i + 1)
                      << std::setw(wAction) << action_name;
            for (const auto& [key, val] : state.get_fluents())
                std::cout << " " << key << "=" << val;
            std::cout << "\n";
        }

        std::cout << "\n=== Final State ===\n";
        print_state(state);
        std::cout << "\nGoal reached? "
                  << (state.is_goal_reached(problem.goal) ? "YES" : "NO")
                  << "\n";
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
