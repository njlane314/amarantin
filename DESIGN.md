# DESIGN

This repository should stay small, direct, and easy to grep.

The goal is not maximum abstraction. The goal is clear data flow and code that
is cheap to change.

## Core Rules

1. `io/` owns persistence only.
   IO classes open files, read objects, write objects, and describe on-disk
   layout. They do not own physics logic, selection logic, or systematic
   calculations.

2. Prefer plain data and namespace functions.
   If a type does not own resources or long-lived state, it should usually be a
   `struct` plus free functions in a namespace. Do not add single-purpose
   builder or service classes when one function will do.

3. Keep workflows in `app/`.
   Libraries provide reusable pieces. Applications and macros orchestrate those
   pieces.

4. Keep module boundaries sharp.
   Current intended split:
   - `io/`: file format and persistence
   - `ana/`: event-list construction, selection, sample definitions
   - `syst/`: systematic calculations and cache construction
   - `plot/`: rendering only

5. Add abstractions only when they delete complexity.
   A new type or library should remove more concepts than it adds. If it only
   wraps one function call, it is probably not worth having.

## Naming

- Keep `IO` for persistence classes.
- Prefer short noun-like names for persistent data objects.
- Prefer verb-like namespace functions for one-shot work:
  - `ana::build_event_list(...)`
  - `syst::build_systematics_cache(...)`

Avoid vague names like `Manager`, `Service`, `Provider`, and `Facade` unless the
type truly has that scope.

## Data Flow

The preferred flow is:

1. read source material with `DatasetIO`
2. build selected event content with `ana`
3. persist selected content with `EventListIO`
4. build derived systematic caches with `syst`
5. load and render with `plot`

The file classes should stay usable without pulling in the full analysis stack.

## Deletion Discipline

After every feature pass, do a small deletion pass before committing:

- delete obsolete files
- delete obsolete classes and wrappers
- delete dead includes
- delete stale targets from CMake
- delete docs that describe removed APIs

Do not leave compatibility scaffolding around unless it has a clear migration
value.

## Practical Test

Before adding code, ask:

- Can this be a function instead of a class?
- Can this live in an existing module?
- Does this make the data flow easier to follow?
- Will a new reader find the logic with one grep?

If the answer is no, simplify first.
