# Loomle Working Rules

## Goal

Loomle exists to help agents operate Unreal Engine smoothly, reliably, and in
terms that match UE semantics.

Loomle should not invent a replacement model for UE, and its progress should
not depend on continually discovering new product ideas. Its work is to compare
agent workflows against UE itself, find the places where direct operation is
awkward, fragile, or unclear, and expose those capabilities through clear,
faithful tools.

UE source code is Loomle's most important context. Any change that touches UE
behavior, interface semantics, or tool boundaries must start by reading the
relevant UE source and then designing Loomle's expression of that behavior.

## Public Protocol Naming

Public tool names use dotted domains, such as `blueprint.graph.inspect`.
Public enum-like protocol values, including `view`, `operation`, `kind`,
`reason`, and error code fragments, should use readable snake_case, such as
`exec_flow`, `data_flow`, and `open_exec_output`.

JSON field names should remain consistent with the existing public surface.
Unreal C++ implementation code should continue to follow Unreal naming
conventions and UE API names.

## Workflow

### 1. Sense Demand

Demand can come from user instructions, bugs, agent usage friction, issues,
test failures, or awkwardness in existing interfaces.

Unless the issue is truly a small local bug, do not treat it as a one-off patch.
First identify what class of gap it exposes: UE semantics, tool boundaries,
schema shape, error feedback, execution path, or agent usability.

### 2. Form Design

Design starts by reading the relevant UE source and Loomle's current
implementation.

Design must be maintained in formal documentation. The documentation is for both
users and developers, and should stay concise and clear. It should describe:

- Design intent
- Tool boundaries
- Input and output schemas
- Edit operation schemas
- Error responses
- Relevant UE internal implementation principles
- How Loomle maps UE behavior

Do not implement until the design is documented and confirmed.

### 3. Implement

Implement only the confirmed scope.

Prefer UE's native APIs, data structures, and execution paths. Do not introduce
a second source of truth that drifts away from UE semantics to solve a local
problem. If a temporary approach is truly required, document its reason, scope,
and exit condition.

If implementation proves a design assumption wrong, pause, update the design,
and continue from the corrected design.

### 4. Audit

Audit is not limited to whether tests pass. Loomle is an agent-facing project,
so acceptance must also check whether the agent can use the result smoothly.

Audit should check:

- Whether tool names and schemas make usage clear to the agent
- Whether schemas are explicit enough that the agent does not need to guess
- Whether returned data matches UE semantics
- Whether errors guide the next action
- Whether the class of gap exposed by the issue has been identified
- Whether tests cover the important success and failure paths

Audit conclusions must be maintained back into the formal documentation. When a
task is complete, state what changed, what was verified, what documentation was
updated, and what gaps remain.
