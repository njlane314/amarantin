# Initial backlog for amarantin simplification

## Tier 1: do first

### 1. `tools/run-macro` simplification
Audit the shell wrapper for redundant branching, type inference complexity, and invocation assembly complexity.

Why first:
- concentrated complexity in a non-installed surface
- easy to verify
- high clarity payoff if behavior is preserved

Constraint:
- preserve the supported invocation grammar documented in `COMMANDS`

### 2. Wrapper and helper trimming
Audit small wrapper layers in `app/`, `tools/`, and non-installed helper code.

Why first:
- low migration risk
- often deletes code instead of moving it

Constraint:
- keep the `io/` / `ana/` / `syst/` / `plot/` split sharp

## Tier 2: do after verification discipline is working

### 3. Public utility-class audit
Audit:
- `SnapshotService`
- `EventListSelection`
- `AnalysisChannels`

Why later:
- these conflict with the repo's plain-data / namespace-function style
- but they live in public installed headers, so migration risk is real

Constraint:
- default to compatibility-preserving simplification unless a break is explicitly approved

### 4. Doc and include trimming
Delete stale docs, dead includes, and local compatibility scaffolding that no longer matches reality.

## Tier 3: only after above is clean

### 5. Deeper `syst/` and `plot/` cleanup
Keep this local and incremental. Those modules changed recently, so avoid broad redesign.
