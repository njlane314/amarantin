# ExecPlan template for amarantin simplification work

The current plan lives at `.agent/current_execplan.md`.

## Required sections

### 1. Objective
State exactly what should become smaller, flatter, or easier to grep.

### 2. Constraints
List:
- behavior that must not change
- CLI surfaces that must remain stable
- public headers / installed targets that must remain stable
- modules in scope and out of scope

### 3. Design anchor
Quote or summarize the rule from `DESIGN.md` that justifies the change.

### 4. System map
List the exact files, modules, commands, and tests involved.

### 5. Candidate simplifications
Group them under:
- wrapper collapse
- script simplification
- boundary sharpening
- doc / build cleanup
- stale scaffolding

### 6. Milestones
Each milestone must fit in one edit / verify loop.
For each milestone include:
- status: `todo` / `in_progress` / `done` / `blocked` / `cancelled`
- hypothesis: why this is simpler
- files / symbols touched
- expected behavior risk
- verification commands
- acceptance criteria

### 7. Public-surface check
If a milestone touches installed headers, build config, or CLI behavior, add:
- compatibility impact
- migration note or explicit non-goal
- reviewer sign-off

### 8. Reduction ledger
Track:
- files deleted
- wrappers removed
- shell branches removed
- stale docs removed
- targets or dependencies removed
- approximate LOC delta

### 9. Decision log
Capture decisions that prevent oscillation.

### 10. Stop conditions
Stop when:
- the next milestone is high risk for small simplicity gain
- only public-API churn remains without approval
- remaining work is mostly formatting or style-only cleanup
