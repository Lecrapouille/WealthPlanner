# PDDL Parser — Coverage

Legend: ✅ fully supported · ⚠️ partially supported · ❌ not supported

---

## Requirements flags

| Flag | Status | Notes |
|------|--------|-------|
| `:strips` | ✅ | Core add/delete effects |
| `:typing` | ⚠️ | Types parsed and stored in `Term::type` / `TypeDef`; hierarchy not used to filter instantiation |
| `:equality` | ✅ | `(= ...)` evaluated in `WorldState::evaluates` |
| `:numeric-fluents` | ⚠️ | `increase` / `decrease` / `assign` supported; `scale-up` / `scale-down` not |
| `:action-costs` | ✅ | `(increase (total-cost) N)` parsed and stored in `Action::cost` |
| `:disjunctive-preconditions` | ❌ | `(or ...)` not supported |
| `:existential-preconditions` | ❌ | `(exists ...)` not supported |
| `:universal-preconditions` | ❌ | `(forall ...)` in preconditions not supported |
| `:quantified-preconditions` | ❌ | Implies both existential + universal |
| `:conditional-effects` | ⚠️ | `(when cond eff)` handled in `AStarSolver` during execution; not reified in AST |
| `:derived-predicates` | ✅ | Parsed and ground-expanded at fixed-point after each action |
| `:timed-initial-literals` | ❌ | `(at t fact)` entries in `:init` silently skipped |
| `:durative-actions` | ❌ | `:durative-action` blocks not parsed |
| `:duration-inequalities` | ❌ | Depends on durative actions |
| `:continuous-effects` | ❌ | Continuous change not supported |
| `:preferences` | ❌ | PDDL 3.0 soft goals not supported |
| `:constraints` | ❌ | Trajectory constraints silently skipped |
| `:object-fluents` | ❌ | Non-numeric function values not supported |

---

## Domain sections

| Section | Status | Notes |
|---------|--------|-------|
| `(domain name)` | ✅ | |
| `:requirements` | ✅ | Stored as string list |
| `:types` | ⚠️ | Parsed into `TypeDef{name, parent}`; type hierarchy not used for object filtering |
| `:constants` | ✅ | Stored in `Domain::constants`; included during action instantiation |
| `:predicates` | ✅ | Signatures stored |
| `:functions` | ⚠️ | Signatures stored in `Domain::functions`; not validated against fluent usage |
| `:action` | ✅ | Full support for `:parameters`, `:precondition`, `:effect` |
| `:durative-action` | ❌ | Not parsed |
| `:derived` | ✅ | Parsed into `DerivedPredicate{head, body}`; instantiated and expanded at runtime |
| `:process` | ❌ | PDDL+ process blocks not supported |
| `:event` | ❌ | PDDL+ event blocks not supported |

---

## Precondition constructs

| Construct | Status | Notes |
|-----------|--------|-------|
| `(and ...)` | ✅ | Conjunctions |
| `(not ...)` | ✅ | Negation via `not:` prefix convention |
| `(= e1 e2)` | ✅ | Numeric or fact equality |
| `(>= e1 e2)` | ✅ | |
| `(> e1 e2)` | ✅ | |
| `(< e1 e2)` | ✅ | |
| `(<= e1 e2)` | ✅ | |
| `(or ...)` | ❌ | Requires non-conjunctive precondition model |
| `(imply c e)` | ❌ | |
| `(exists (?x - t) ...)` | ❌ | |
| `(forall (?x - t) ...)` | ❌ | |

---

## Effect constructs

| Construct | Status | Notes |
|-----------|--------|-------|
| `(and ...)` | ✅ | |
| `(not ...)` | ✅ | Delete effect |
| `(increase f n)` | ✅ | `double` arithmetic |
| `(decrease f n)` | ✅ | |
| `(assign f n)` | ✅ | |
| `(when cond eff)` | ⚠️ | Executed in `AStarSolver`; not stored as AST node (condition reified as string) |
| `(forall (?x - t) eff)` | ❌ | Universal effects not supported |
| `(scale-up f factor)` | ❌ | |
| `(scale-down f factor)` | ❌ | |

---

## Problem sections

| Section | Status | Notes |
|---------|--------|-------|
| `(problem name)` | ✅ | |
| `:domain` | ✅ | |
| `:objects` | ✅ | Object names stored; types stored in the `Term::type` field during parsing |
| `:init` | ✅ | Bool facts + numeric fluents via `(= (f args) val)` |
| `:init (at t fact)` | ❌ | Timed initial literals silently skipped |
| `:goal` | ✅ | Conjunctive goals |
| `:metric` | ⚠️ | Expression serialized as a string in `Problem::metric`; not used by planner |
| `:constraints` | ❌ | Silently skipped |
| `:length` | ❌ | Deprecated PDDL 1.0 field; silently ignored |

---

## Numeric expressions

| Expression | Status | Notes |
|------------|--------|-------|
| Integer literals | ✅ | |
| Float / scientific notation | ✅ | `double` fluents; `is_number` handles `.`, `e`, `E` |
| Fluent reference `(f args)` | ✅ | Looked up in `WorldState::m_fluents` |
| `(+ a b)` `(- a b)` | ❌ | Arithmetic sub-expressions not evaluated |
| `(* a b)` `(/ a b)` | ❌ | |

---

## PDDL version summary

| Version | Status | Notes |
|---------|--------|-------|
| PDDL 1.0 (STRIPS) | ⚠️ | Core supported; `or`/`exists`/`forall` missing |
| PDDL 2.1 (numeric + temporal) | ⚠️ | Numeric fluents ✅; durative actions ❌ |
| PDDL 2.2 (derived + timed init) | ⚠️ | Derived predicates ✅; timed initial literals ❌ |
| PDDL 3.0 (preferences) | ❌ | |
| PDDL 3.1 (object fluents + action costs) | ⚠️ | Action costs ✅; object fluents ❌ |
