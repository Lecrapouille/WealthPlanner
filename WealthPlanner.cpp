#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
//  COMPANIES & UNIVERSITIES (data tables)
// ============================================================
enum class EducationLevel
{
    None = 0,
    Licence = 1,
    Master = 2
};

struct Company
{
    std::string name;
    int baseSalary;              // Money per Work action
    int overtimeSalary;          // Money per Overtime action
    int healthCostWork;          // health drained by Work
    int healthCostOT;            // health drained by Overtime
    EducationLevel minEducation; // Required education level to join
};

struct University
{
    std::string name;
    int cost;                      // Money cost to attend
    int duration;                  // Actions consumed
    EducationLevel educationGrant; // Education level obtained
    int healthCost;                // How tiring
};

const std::vector<Company> COMPANIES = {
    { .name = "Startup",
      .baseSalary = 40'000,
      .overtimeSalary = 80'000,
      .healthCostWork = 40,
      .healthCostOT = 45,
      .minEducation = EducationLevel::Licence },
    { .name = "MegaCorp",
      .baseSalary = 60'000,
      .overtimeSalary = 120'000,
      .healthCostWork = 40,
      .healthCostOT = 50,
      .minEducation = EducationLevel::Master }, // needs degree
};

const std::vector<University> UNIS = {
    { .name = "University",
      .cost = 5'000,
      .duration = 8,
      .educationGrant = EducationLevel::Licence,
      .healthCost = 25 }, // degree
    { .name = "HighSchool",
      .cost = 20'000,
      .duration = 12,
      .educationGrant = EducationLevel::Master,
      .healthCost = 35 }, // master
};

// ============================================================
//  GOALS
// ============================================================

const int TARGET_MONEY = 1'000'000; // Goal: become a millionaire (€)
const int TARGET_HEALTH = 80;       // Goal: have >= 80% of health

// ============================================================
//  HEURISTICS CONSTANTS
// ============================================================

// Best possible salary per action: (i.e. MegaCorp OT = 100k)
const int MAX_BASE_SALARY =
    std::max_element(COMPANIES.begin(),
                     COMPANIES.end(),
                     [](const Company& a, const Company& b)
                     { return a.overtimeSalary < b.overtimeSalary; })
        ->overtimeSalary;

const int SLEEP_health_GAIN = 20;

// ============================================================
//  WORLD STATE
// ============================================================
struct WorldState
{
    // Current amount of money
    int money = 0;
    // Current amount of health for working (0..100)
    int health = 100;
    // Current education level
    EducationLevel education = EducationLevel::None;
    // Cumulative hours worked this "week" (resets after vacation/sleep)
    int hoursWorked = 0;
    // Cumulative hours worked since start
    int totalHours = 0;
    // Which company/university is the agent currently linked to (-1 = none)
    int companySlot = -1; // index in COMPANIES
    int uniSlot = -1;     // index in UNIS

    bool isGoalReached()
    {
        return money >= TARGET_MONEY && health >= TARGET_HEALTH;
    }

    bool operator==(const WorldState& o) const
    {
        return money == o.money && health == o.health &&
               education == o.education && hoursWorked == o.hoursWorked &&
               companySlot == o.companySlot && uniSlot == o.uniSlot;
    }
};

// ============================================================
//  ACTION
// ============================================================
struct Action
{
    std::string name;
    int cost;
    std::function<bool(const WorldState&)> precondition;
    std::function<WorldState(WorldState)> effect;
};

// ============================================================
//  BUILD ACTION LIST
// ============================================================
std::vector<Action> buildActions()
{
    std::vector<Action> actions;

    // --- SLEEP ---
    actions.push_back(
        { .name = "Sleep",
          .cost = 3,
          .precondition = [](const WorldState& s) { return s.health < 80; },
          .effect =
              [](WorldState s)
          {
              s.health = std::min(100, s.health + SLEEP_health_GAIN);
              s.totalHours += 8;
              return s;
          } });

    // --- VACATION ---
    for (int i = 0; i < (int)COMPANIES.size(); i++)
    {
        actions.push_back({ .name = "Vacation",
                            .cost = 2,
                            .precondition =
                                [i](const WorldState& s)
                            {
                                return s.money >= 10'000 &&
                                       s.companySlot == i &&
                                       s.hoursWorked >= 120;
                            },
                            .effect =
                                [](WorldState s)
                            {
                                s.money -= 10'000;
                                s.health = std::min(100, s.health + 60);
                                s.hoursWorked = 0;
                                s.totalHours += 24;
                                return s;
                            } });
    }

    // --- JOIN COMPANY ---
    for (int i = 0; i < (int)COMPANIES.size(); i++)
    {
        const Company& c = COMPANIES[i];
        actions.push_back({ .name = "Join@" + c.name,
                            .cost = 1,
                            .precondition =
                                [i](const WorldState& s)
                            {
                                // Uncomment to forbid quitting companies
                                return /*s.companySlot == -1 &&*/
                                    s.education >= COMPANIES[i].minEducation;
                            },
                            .effect =
                                [i](WorldState s)
                            {
                                s.companySlot = i;
                                s.hoursWorked = 0;
                                return s;
                            } });
    }

    // --- WORK (normal hours) ---
    for (int i = 0; i < (int)COMPANIES.size(); i++)
    {
        const Company& c = COMPANIES[i];
        actions.push_back({ .name = "Work@" + c.name,
                            .cost = 1,
                            .precondition =
                                [i](const WorldState& s)
                            {
                                return s.companySlot == i &&
                                       s.health >=
                                           COMPANIES[i].healthCostWork + 10;
                            },
                            .effect =
                                [i](WorldState s)
                            {
                                s.money += COMPANIES[i].baseSalary;
                                s.health -= COMPANIES[i].healthCostWork;
                                s.health = std::max(0, s.health);
                                s.hoursWorked += 40;
                                s.totalHours += 40;
                                return s;
                            } });
    }

    // --- OVERTIME ---
    for (int i = 0; i < (int)COMPANIES.size(); i++)
    {
        const Company& c = COMPANIES[i];
        actions.push_back({ .name = "Overtime@" + c.name,
                            .cost = 1,
                            .precondition =
                                [i](const WorldState& s)
                            {
                                // Overtime only available after regular week +
                                // enough health
                                return s.companySlot == i &&
                                       s.hoursWorked >= 40 &&
                                       s.health >=
                                           COMPANIES[i].healthCostOT + 10;
                            },
                            .effect =
                                [i](WorldState s)
                            {
                                s.money += COMPANIES[i].overtimeSalary;
                                s.health -= COMPANIES[i].healthCostOT;
                                s.health = std::max(0, s.health);
                                s.hoursWorked += 20;
                                s.totalHours += 20;
                                return s;
                            } });
    }

    // --- ATTEND UNIVERSITY ---
    for (int j = 0; j < (int)UNIS.size(); j++)
    {
        const University& u = UNIS[j];
        actions.push_back({ .name = "Study@" + u.name,
                            .cost = 4,
                            .precondition =
                                [j](const WorldState& s)
                            {
                                return s.money >= UNIS[j].cost &&
                                       s.health >= UNIS[j].healthCost + 10 &&
                                       s.education < UNIS[j].educationGrant;
                            },
                            .effect =
                                [j](WorldState s)
                            {
                                s.money -= UNIS[j].cost;
                                s.health -= UNIS[j].healthCost;
                                s.hoursWorked = 0;
                                s.totalHours += UNIS[j].duration * 40;
                                s.education = UNIS[j].educationGrant;
                                s.health = std::max(0, s.health);
                                return s;
                            } });
    }

    return actions;
}

// ============================================================
//  STATE KEY (for visited set)
// ============================================================
std::string stateKey(const WorldState& s)
{
    // Bucket money/health to avoid infinite graph explosion
    int moneyBucket = (s.money / 10'000);
    int healthBucket = (s.health / 10);
    int hoursBucket = (s.hoursWorked / 40);
    std::ostringstream ss;
    ss << moneyBucket << "_" << healthBucket << "_"
       << static_cast<int>(s.education) << "_" << hoursBucket << "_"
       << s.companySlot;
    return ss.str();
}

// ============================================================
//  HEURISTIC
// ============================================================
float heuristic(const WorldState& s)
{
    // How many actions needed to have money ?
    // MegaCorp OT gives MAX_BASE_SALARY money by action
    int remaining_money = std::max(0, TARGET_MONEY - s.money);
    float h_money = float(remaining_money) / float(MAX_BASE_SALARY);

    // How many actions needed to feel ?
    // Sleeping give SLEEP_health_GAIN health by action
    int remaining_health = std::max(0, TARGET_HEALTH - s.health);
    float h_health = float(remaining_health) / float(SLEEP_health_GAIN);

    // Do not use summation of heuristics
    return std::max(h_money, h_health);
}

// ============================================================
//  A* GOAP PLANNER
// ============================================================
struct Node
{
    // Estimated total cost.
    // Important: must NEVER overestimate the real cost !!!
    float estimated_cost;
    // Real cost to reach the current state
    float real_cost;
    // Current state
    WorldState state;
    // Sequence of actions to reach the current state
    std::vector<std::string> plan;

    // For priority queue: lowest estimated_cost at the top
    bool operator>(const Node& o) const
    {
        return estimated_cost > o.estimated_cost;
    }
};

// A* GOAP: finds a sequence of action names (plan) to become a millionaire.
std::tuple<std::vector<std::string>, WorldState, size_t>
plan(const WorldState& initial, const std::vector<Action>& actions)
{
    // Iteration limit
    size_t iterations = 0;
    const size_t MAX_ITER = 500'000;

    // Priority queue ordered by f = g + h (lowest f at the top)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    // Best known real_cost per state (key = stateKey) to avoid duplicates
    std::unordered_map<std::string, float> best_real_cost;

    // Initialize start node
    Node start;
    start.real_cost = 0;
    start.estimated_cost = heuristic(initial);
    start.state = initial;
    open.push(start);

    while (!open.empty() && iterations++ < MAX_ITER)
    {
        // Get current node from unexplored nodes
        Node current = open.top();
        open.pop();

        // Goal reached: return the plan and the final state
        if (current.state.isGoalReached())
        {
            return { current.plan, current.state, iterations };
        }

        // Already explored with a better or equal cost g: skip
        std::string key = stateKey(current.state);
        if (best_real_cost.count(key) &&
            best_real_cost[key] <= current.real_cost)
            continue;
        best_real_cost[key] = current.real_cost;

        // Expand: try each applicable action
        for (const Action& a : actions)
        {
            // Action not applicable: skip
            if (!a.precondition(current.state))
            {
                continue;
            }

            // Apply action: create new state
            WorldState new_state = a.effect(current.state);
            if (new_state.money < 0) // Needed for the bucketization
            {
                continue;
            }

            // Calculate new real cost
            float ng = current.real_cost + a.cost;
            std::string new_key = stateKey(new_state);

            // Neighbor already reached with a better or equal cost g → skip
            if (best_real_cost.count(new_key) && best_real_cost[new_key] <= ng)
            {
                continue;
            }

            // Add new state to unexplored nodes
            Node next_state;
            next_state.real_cost = ng;
            next_state.estimated_cost = ng + heuristic(new_state);
            next_state.state = new_state;
            next_state.plan = current.plan;
            next_state.plan.push_back(a.name);
            open.push(next_state);
        }
    }

    // No plan found within the iteration limit
    return { {}, initial, iterations };
}

// ============================================================
//  DISPLAY HELPERS
// ============================================================
std::string moneyBar(int money)
{
    int barLength = 40;
    int pct =
        std::min(100, (int)((float)money / (float)TARGET_MONEY * barLength));
    std::string bar = "[";
    for (int i = 0; i < barLength; i++)
        bar += (i < pct ? "#" : ".");
    bar += "]";
    return bar;
}

std::string healthBar(int health)
{
    int barLength = 40;
    int pct = health * barLength / 100;
    std::string bar = "[";
    for (int i = 0; i < barLength; i++)
        bar += (i < pct ? "#" : ".");
    bar += "]";
    return bar;
}

std::string eduLabel(EducationLevel e)
{
    switch (e)
    {
        case EducationLevel::None:
            return "No degree";
        case EducationLevel::Licence:
            return "Degree";
        case EducationLevel::Master:
            return "Master";
    }
    return "?";
}

// ============================================================
//  MAIN
// ============================================================
int main()
{
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║       GOAP – Simulation: Become a Millionaire        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

    // Print companies
    std::cout << "══ Available Companies "
                 "═════════════════════════════════════════════════════════════"
                 "══════\n";
    for (const auto& c : COMPANIES)
    {
        std::cout << "  🏢 " << std::left << std::setw(16) << c.name
                  << " Salary: " << std::setw(8) << c.baseSalary
                  << " OT: " << std::setw(8) << c.overtimeSalary
                  << " health(W/OT): " << c.healthCostWork << "/"
                  << c.healthCostOT << "  MinEdu: " << eduLabel(c.minEducation)
                  << "\n";
    }
    std::cout << "\n══ Available Universities "
                 "═════════════════════════════════════════════════════════════"
                 "═══\n";
    for (const auto& u : UNIS)
    {
        std::cout << "  🎓 " << std::left << std::setw(16) << u.name
                  << " Cost: " << std::setw(8) << u.cost
                  << " Degree: " << eduLabel(u.educationGrant) << "  health: -"
                  << u.healthCost << "\n";
    }
    std::cout << "\n";

    WorldState initial;
    initial.money = 7'000;
    initial.health = 100;
    initial.education = EducationLevel::None;
    initial.hoursWorked = 0;
    initial.companySlot = -1;
    initial.uniSlot = -1;

    std::cout
        << "══ A* Planning ══════════════"
           "═════════════════════════════════════════════════════════════\n\n";

    auto actions = buildActions();
    auto [planSteps, finalState, iterations] = plan(initial, actions);

    if (planSteps.empty())
    {
        std::cout << "❌ No plan found after " << iterations
                  << " iterations.\n";
        return 1;
    }

    std::cout << "✅ A " << planSteps.size()
              << "-steps plan has been found after " << iterations
              << " iterations\n\n";
    const int wNum = 4, wAction = 26, wMoney = 18, whealth = 12, wAchieved = 16,
              wHours = 8;
    std::cout << "══ Step-by-step Simulation "
                 "═════════════════════════════════════════════════════════════"
                 "══\n";
    std::cout << std::left << std::setw(wNum) << "#" << std::setw(wAction)
              << "Action" << std::right << std::setw(wMoney) << "Money (€)"
              << std::setw(whealth) << "health" << " " << std::setw(wAchieved)
              << "Education" << std::right << std::setw(wHours) << "Hours"
              << "\n";

    std::cout << std::left << std::setw(wNum) << 0 << std::setw(wAction)
              << "🏁 (initial)" << std::right << std::setw(wMoney)
              << initial.money << std::setw(whealth) << initial.health << " "
              << std::setw(wAchieved) << eduLabel(initial.education)
              << std::right << std::setw(wHours) << initial.totalHours << "\n";

    WorldState s = initial;
    for (int i = 0; i < (int)planSteps.size(); i++)
    {
        const std::string& aname = planSteps[i];
        // find action
        auto it =
            std::find_if(actions.begin(),
                         actions.end(),
                         [&](const Action& a) { return a.name == aname; });
        if (it != actions.end())
            s = it->effect(s);

        std::string icon;
        if (aname.find("Work") != std::string::npos)
            icon = "💼";
        if (aname.find("Overtime") != std::string::npos)
            icon = "🔥";
        if (aname.find("Sleep") != std::string::npos)
            icon = "😴";
        if (aname.find("Vacation") != std::string::npos)
            icon = "🌴";
        if (aname.find("Join") != std::string::npos)
            icon = "🤝";
        if (aname.find("Study") != std::string::npos)
            icon = "🎓";

        std::cout << std::left << std::setw(wNum) << (i + 1)
                  << std::setw(wAction) << (icon + " " + aname) << std::right
                  << std::setw(wMoney) << s.money << std::setw(whealth)
                  << s.health << " " << std::setw(wAchieved)
                  << eduLabel(s.education) << std::right << std::setw(wHours)
                  << s.totalHours << "\n";
    }

    std::cout << "\n══ Final Result "
                 "═════════════════════════════════════════════════════════"
                 "════════════════\n";
    std::cout << "  Money     " << moneyBar(finalState.money) << " "
              << finalState.money << " €\n";
    std::cout << "  health    " << healthBar(finalState.health) << " "
              << finalState.health << "/100\n";
    std::cout << "  Education : " << eduLabel(finalState.education) << "\n";
    std::cout << "  Hours     : " << finalState.totalHours
              << " hours worked (cumulative)\n";
    std::cout << "  Steps     : " << planSteps.size() << "\n";

    if (finalState.isGoalReached())
    {
        std::cout << "\n  🎉 MILLIONAIRE! Goal achieved.\n";
    }
    else
    {
        std::cout << "\n  ❌ Goal not achieved.\n";
    }

    std::cout << "═════════════════════════════════════════════════════════════"
                 "═════════════════════════════\n";
    return EXIT_SUCCESS;
}
