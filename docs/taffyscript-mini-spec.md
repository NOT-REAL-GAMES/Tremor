# Taffyscript Mini-Spec (1.0)

This document defines the small-scope expression and assignment features for
Taffyscript 1.0.

The goal is to make scripts capable of stateful gameplay orchestration without
turning Taffyscript into a general-purpose programming language.

## Design Goals

Taffyscript 1.0 should be:

- small enough to implement and debug quickly
- expressive enough to replace gameplay tuning and event glue in `DMCSurvivors`
- deterministic and easy to inspect
- friendly to embedded `SCPT` chunks and loose script files
- built around events, blackboard state, conditions, and host commands

Taffyscript 1.0 should not try to provide:

- loops
- user-defined functions
- arbitrary recursion
- complex collections
- dynamic code loading inside a running script
- direct unrestricted engine memory access

## Post-1.0 Direction

The long-term goal is to make gameplay packages expressive enough that
`DMCSurvivors` can shrink toward a thin host/bootstrap layer.

That future requires three additional capabilities beyond the strict 1.0 scope:

- objects
- assignment as a first-class language feature
- lambdas / callable script values

These are intentionally staged after the core 1.0 expression milestone so the
runtime grows in a coherent order:

1. typed values
2. expressions and assignments
3. object construction and field access
4. lambdas and closures

The interpreter runtime may grow object/lambda-capable value storage before the
surface syntax exposes those features directly.

## Current Baseline

The interpreter already supports:

- `program <name>`
- `rule <name>`
- `on_load`
- `on_event <event_name>`
- `on_tick <seconds>`
- `cooldown <seconds>`
- `action log <message>`
- `action emit <event_name>`
- `action command <command_name> [argument]`

This spec extends that baseline with:

- typed values
- assignment
- expression evaluation
- conditional rule/action execution

## Value Model

Taffyscript 1.0 supports four runtime value types:

- `number`
  - stored as float/double internally
- `bool`
  - `true` or `false`
- `string`
  - UTF-8 text
- `null`
  - missing/empty value sentinel

Arrays and general-purpose collection operations are not part of 1.0
expression evaluation. Small object literals and field access may be supported
for structured gameplay state.

The runtime may additionally support object and lambda-capable values as an
implementation foundation for post-1.0 growth, but the strict 1.0 surface area
should remain focused and small.

### Lambda Groundwork

The runtime may support lambda literals as stored values before callable
invocation exists.

Recommended groundwork syntax:

```text
fn() { body }
fn(wave) { wave + 1 }
fn(wave, intensity) { wave * intensity }
```

For groundwork-only support:

- lambda literals may be parsed and stored
- lambdas may be assigned into blackboard/object fields
- lambdas may appear in debug output
- calling/invoking lambdas is explicitly deferred

### Host Bridge Callbacks

For 1.0, the preferred first invocation model is a host bridge rather than a
general-purpose script `call(...)` feature.

That means:

- scripts may store lambdas in blackboard/object fields
- scripts may bind those lambdas to named host callbacks
- engine systems may invoke those callbacks explicitly at controlled moments
- callback bodies remain expression-based in the first implementation

Recommended binding pattern:

```text
action set package_state.wave_descriptor_policy fn() { { enemy_count: var.package_state.base_wave_enemy_count + (var.wave_state.current_wave - 1), spawn_interval: var.package_state.base_spawn_interval - (var.wave_state.current_wave * 0.12), wave_intensity: var.wave_state.default_wave_intensity } }
action command bind_host_callback wave_descriptor_policy package_state.wave_descriptor_policy
```

The first host bridge uses `arg.<name>` to expose invocation arguments inside
the lambda body.

Host callbacks may also return object descriptors, which is useful when a
single script policy needs to produce a bundle of gameplay values rather than a
single number.

## Value Sources

Expressions may resolve values from:

- literals
- event fields
- blackboard values
- host callback arguments

### Literals

Examples:

```text
42
3.5
true
false
"wave_advanced"
```

### Event Field References

Use `event.<field>` to access the current event payload.

Examples:

```text
event.wave
event.enemies_per_wave
event.entity_id
```

If no current event exists, or the field is missing, the result is `null`.

### Blackboard References

Use `var.<name>` to access interpreter blackboard state.

Examples:

```text
var.spawn_interval
var.enemy_budget
var.current_phase
```

If the key is missing, the result is `null`.

Nested object fields may be addressed with dot paths:

```text
var.package_state.gameplay_mode
var.package_state.wave_enemy_count
var.loose_state.last_completed_wave
```

### Host Callback Arguments

Use `arg.<name>` to access named values passed by the engine when invoking a
bound host callback.

Examples:

```text
arg.wave
arg.base_interval
arg.base_count
arg.wave_intensity
```

If no callback argument exists for the requested name, the result is `null`.

## Assignment

Assignment writes values into the interpreter blackboard.

### Syntax

```text
set <identifier> <expression>
```

Examples:

```text
set spawn_interval 2.5
set current_wave event.wave
set pressure_multiplier 1.0 + 0.15
set wave_label "first_wave"
set is_boss_wave event.wave >= 5
```

### Semantics

- the target must be a bare identifier
- the right-hand side is evaluated as an expression
- the evaluated result is stored in the blackboard
- assigning `null` is allowed

Implementations may also support dotted assignment targets for nested object
state:

```text
action set package_state.current_wave event.wave
action set package_state.active_spawn_interval 2.5
```

### Storage Contract

For 1.0, blackboard values may continue to serialize internally as strings if
needed for implementation simplicity, but the interpreter must preserve logical
types during expression evaluation.

That means:

- `set count 5` should behave as a number in later comparisons
- `set armed true` should behave as a boolean
- `set label "alpha"` should behave as a string

## Expressions

Expressions are intentionally small in 1.0.

### Supported Operators

Arithmetic:

- `+`
- `-`
- `*`
- `/`

Comparison:

- `==`
- `!=`
- `>`
- `>=`
- `<`
- `<=`

Boolean:

- `and`
- `or`
- `not`

### Operator Precedence

Highest to lowest:

1. parentheses
2. unary `not`
3. `*` `/`
4. `+` `-`
5. comparison operators
6. `and`
7. `or`

### Parentheses

Parentheses are supported and should be honored.

Examples:

```text
set spawn_scale (event.wave + 1) * 0.5
if (event.wave >= 3) and not var.boss_spawned
```

### Object Literals

Object literals use `{}` with `key: expression` entries.

Examples:

```text
{ gameplay_mode: packaged, wave_enemy_count: 6 }
{ status: loaded, last_completed_wave: event.wave }
```

Supported object literal rules:

- keys may be bare identifiers or quoted strings
- values are full expressions
- fields are comma-separated
- nested objects are allowed

## Type Rules

### Arithmetic

Arithmetic operators require numeric operands.

Examples:

```text
set interval 3.0 - 0.25
set intensity event.wave * 1.15
```

If an operand cannot be interpreted as a number, evaluation fails and the rule
should log an interpreter error.

### Comparison

Comparison works as follows:

- numbers compare numerically
- bools compare by exact equality/inequality only
- strings compare by exact equality/inequality only
- ordering operators (`>`, `<`, `>=`, `<=`) are valid only for numbers
- `null` compares equal only to `null`

### Boolean Evaluation

Boolean operators require boolean operands after coercion.

For 1.0, use these coercion rules:

- `bool` stays `bool`
- `number`:
  - `0` -> `false`
  - non-zero -> `true`
- `string`:
  - empty -> `false`
  - non-empty -> `true`
- `null` -> `false`

## Conditions

Conditions guard rule execution or individual actions.

### Rule-Level Condition

```text
if <expression>
```

When attached to a rule, the condition is evaluated when the trigger fires.
If false, the rule does nothing.

Example:

```text
rule tighten_spawn_rate
on_event wave_started
if event.wave >= 3
action command set_wave_spawn_interval 1.5
```

### Action-Level Condition

For 1.0, action-level conditions are optional. The preferred first
implementation is rule-level `if`.

If action-level conditions are added, the syntax should stay explicit, such as:

```text
action_if <expression> command emit_ui_message Boss incoming|5.0|FF4040FF
```

That is not required for the first milestone.

## Command Argument Interpolation

Command arguments may embed expressions using `${...}`.

This is intended to let interpreted state flow into existing host commands
without requiring every command to grow a typed argument ABI immediately.

### Syntax

```text
action command <command_name> ${<expression>}
action command <command_name> prefix ${<expression>} suffix
```

Examples:

```text
action command set_wave_enemy_count ${var.packaged_wave_enemy_count}
action command set_wave_spawn_interval ${var.packaged_spawn_interval}
action command emit_ui_message wave ${event.wave} complete|5.0|30FFB0FF
```

### Semantics

- each `${...}` block is evaluated as a normal expression
- the resulting value is converted with `Value::toString()`
- interpolation happens before command dispatch
- interpolation failure aborts the current command action and logs an interpreter error

## Recommended 1.0 Grammar Additions

This is a suggested line-oriented grammar extension:

```text
rule <name>
on_load
on_event <event_name>
on_tick <seconds>
cooldown <seconds>
if <expression>
set <identifier> <expression>
action log <message>
action emit <event_name>
action command <command_name> [argument]
action set <identifier> <expression>
```

### Preferred First Implementation

To minimize parser churn, implement in this order:

1. `if <expression>` on rules
2. `action set <identifier> <expression>`
3. expression parser/evaluator
4. optional top-level `set <identifier> <expression>` as shorthand

That keeps the rule format stable and avoids ambiguity about whether `set`
outside an action is immediate or deferred.

## Recommended Execution Semantics

### Rule Evaluation Order

When a rule fires:

1. check trigger
2. check cooldown
3. evaluate `if` condition, if present
4. execute actions in source order
5. set cooldown timer

### Failure Handling

Expression or assignment failures should:

- log an interpreter error with origin and rule name
- fail the current action
- not crash the host

For 1.0, a failed condition evaluation should treat the condition as false.

## Examples

### Wave Tuning

```text
program wave_logic

rule wave_three_pressure
on_event wave_started
if event.wave >= 3
action set spawn_interval 1.5
action command set_wave_spawn_interval 1.5
action command emit_ui_message Spawn pressure increased|4.0|FF9040FF
```

### Blackboard Copy

```text
rule record_current_wave
on_event wave_started
action set current_wave event.wave
action set enemy_budget event.enemies_per_wave
```

### Derived State

```text
rule boss_gate
on_event wave_started
action set is_boss_wave event.wave >= 5
```

### Conditional UI

```text
rule announce_boss_wave
on_event wave_started
if event.wave >= 5
action command emit_ui_message Boss wave incoming|6.0|FF4040FF
```

## Non-Goals for 1.0

The following are explicitly deferred:

- loops or iteration syntax
- mutation of arbitrary entity fields from expressions
- direct component-path access like `entity.health.current`
- arrays and dictionaries
- string concatenation rules beyond simple host-command arguments
- custom operator overloads
- user-defined functions or macros

## Implementation Notes

The current interpreter host already has:

- a blackboard
- event payloads
- rule triggers
- command dispatch

That means the cleanest implementation path is:

1. add a typed `Value` runtime representation
2. add expression tokenization/parsing
3. add optional rule condition storage
4. add `action set`
5. evaluate expressions against:
   - literals
   - `event.<field>`
   - `var.<name>`

## Definition of Done for This Milestone

This mini-spec is satisfied when Tremor can do all of the following:

- parse rule-level `if <expression>`
- parse and execute `action set <identifier> <expression>`
- evaluate arithmetic, comparison, and boolean operators
- read `event.*` and `var.*` values in expressions
- preserve logical value types during evaluation
- use the result to drive real gameplay commands in `DMCSurvivors`

At that point, Taffyscript becomes capable of real stateful decision-making,
which is the right next step for replacing hardcoded DMC Survivors gameplay
glue with script.
