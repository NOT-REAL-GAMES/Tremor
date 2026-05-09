# Flecs Interpreter Roadmap

This roadmap assumes Tremor 1.0 will prioritize a Flecs-driven interpreter layer
over a packaged VM.

The goal is not to replace native C++ gameplay systems. The goal is to add a
safe, inspectable, moddable layer for gameplay logic, event reactions, state
machines, and content scripting that fits the existing ECS architecture.

## Why This Direction

Tremor already has:

- a real `flecs::world`
- registered gameplay components
- named systems in `DMCSurvivors::Game::setupSystems()`
- a strong data/runtime packaging direction through Taffy

That means the easiest path to shipping is:

- keep performance-critical or engine-critical systems in C++
- expose ECS concepts to interpreted logic
- use interpreted logic for orchestration, content behavior, tuning, triggers,
  progression, AI state transitions, and mod hooks

This avoids making the VM the center of the engine before it has earned that role.

## Product Definition

For 1.0, the Flecs interpreter should be:

- ECS-native
- moddable
- debuggable
- data-driven
- packageable in Taffy
- able to reference either embedded or loose script sources

It should not try to be:

- a full general-purpose programming language
- a JIT/runtime compiler project
- a replacement for all C++ gameplay code

For the concrete 1.0 expression and assignment scope, see
`docs/taffyscript-mini-spec.md`.

## Recommended Shape

The best first version is not "a language" so much as "an ECS rule and command
interpreter."

Think in terms of:

- triggers
- queries
- conditions
- actions
- state machines
- event handlers

That maps naturally onto Flecs.

## Core Architecture

### 1. Script Source Model

Support both:

- embedded script chunks inside `.taf`
- loose external script files referenced through `DEPS`

For 1.0, script payloads can live in:

- `SCPT` chunks for embedded behavior
- external `.tafscript`, `.json`, `.toml`, or `.yaml` files for loose mods

The interpreter host should not care where the source came from once loaded.

### 2. Interpreter Host

Add a `FlecsInterpreterHost` service responsible for:

- loading script assets
- resolving external references
- parsing interpreter definitions
- binding them to a `flecs::world`
- registering event listeners / update passes
- reporting errors and debug state

This should be a normal engine-owned service, not a magical singleton.

### 3. Execution Model

Use three execution modes:

- `OnLoad`
  - package or scene initialization
- `OnEvent`
  - collision, spawn, death, wave start, input command, timer, etc.
- `OnTick`
  - periodic logic with explicit cadence

Avoid unconstrained "every script runs every frame" as the default.

### 4. Host API Surface

The interpreter should call into a deliberately small action API:

- create entity
- destroy entity
- add/remove component
- set component field
- emit event
- play audio cue
- spawn overlay/UI state
- start cooldown/timer
- query entities by component/tag

For 1.0, the interpreter should mutate the world through stable host commands,
not direct arbitrary pointer access.

## Script Data Model

Start with a declarative command graph rather than an expression-heavy language.

Example concepts:

- `rule`
- `when`
- `query`
- `if`
- `then`
- `repeat`
- `cooldown`
- `state`
- `transition`
- `emit`

Example shape:

```yaml
rules:
  - name: orb_pickup
    when:
      event: collision
      with: [Player, RedOrb]
    then:
      - add_component:
          target: event.other
          component: PendingDespawn
      - emit:
          event: gain_orbs
          target: event.self
          amount: 100
```

This is enough to ship a lot of content logic before inventing a language syntax war.

## Relationship to Native Systems

The interpreter should sit above the existing ECS systems, not compete with them.

Recommended split:

- Native C++ systems:
  - physics sync
  - movement integration
  - rendering bridges
  - audio runtime
  - low-level combat resolution math
- Interpreted logic:
  - enemy archetype tuning
  - wave composition
  - state transitions
  - scripted encounters
  - loot/drop tables
  - event reactions
  - mod hooks

This is the “use the right tool for the layer” version.

## Phased Roadmap

### Phase 0: Define the Host Contract

Goal: avoid improvising the API in ten places.

- Define interpreter input format.
- Define runtime action API.
- Define event payload schema.
- Define loose-file resolution rules through Taffy `DEPS`.
- Define error reporting format.

Deliverable:

- one design header and one short doc for script host semantics

### Phase 1: Load and Inspect Scripts

Goal: get script assets into the engine cleanly.

- Add script asset descriptors to Taffy `SCPT` chunks.
- Add loose external script references through `DEPS`.
- Build a loader that resolves:
  - embedded scripts
  - relative loose script files
  - mod override directories
- Add an inspector/debug print path.

Deliverable:

- Tremor can mount a package and list the scripts it found.

### Phase 2: Event and Command Interpreter

Goal: support useful gameplay reactions without inventing a full language.

- Implement event registration.
- Implement condition evaluation.
- Implement host commands.
- Add timer/cooldown support.
- Add a small expression evaluator for comparisons and arithmetic.

Deliverable:

- basic gameplay event scripts run against real entities

### Phase 3: State Machines and Queries

Goal: make AI and encounter logic practical.

- Add entity query selectors
- Add named state machines
- Add transitions based on component fields, timers, and events
- Add script-local variables / world-scoped blackboard values

Deliverable:

- enemy and encounter scripts can be authored mostly outside C++

### Phase 4: Editor and Debugging

Goal: make it usable, not merely possible.

- add interpreter error surfacing in editor/log UI
- show active rules and state machines
- show last fired events
- show entity matches for queries
- allow hot reload for loose scripts

Deliverable:

- modding and tuning loop becomes fast and humane

### Phase 5: Packaging and Mod Policy

Goal: make the interpreter a real Taffy feature.

- package embedded scripts into `SCPT`
- support package-local external script refs through `DEPS`
- define override precedence:
  - embedded base
  - package-local external
  - overlay/mod external
- validate missing required refs vs optional refs

Deliverable:

- one package can be self-contained or mod-open by design

## Data Resolution Rules

To support the moddable hybrid container model, script resolution should be:

1. explicit override passed by engine/mod layer
2. package-relative external ref from `DEPS`
3. embedded `SCPT` fallback

That gives you:

- strong packaged defaults
- easy loose-file iteration
- clean mod override behavior

## Integration with Existing `DMCSurvivors`

Given the current code, good first interpreter targets are:

- wave spawning definitions
- enemy archetype behavior states
- orb pickup / reward logic
- combo/style event reactions
- scripted encounter triggers

Bad first targets are:

- low-level physics synchronization
- per-frame movement math
- renderer-facing transform work

Those should stay native.

## Recommended Implementation Order

If we start building this next, the most leverage comes from:

1. create `FlecsInterpreterHost`
2. define script asset schema for `SCPT`
3. add loose script resolution through `DEPS`
4. implement event -> condition -> action pipeline
5. move wave spawning into interpreted content first

Wave spawning is a great first slice because it is:

- visible
- content-like
- easy to validate
- not safety-critical

## Non-Goals for 1.0

Be disciplined about saying no to:

- custom bytecode
- full compiler pipeline
- arbitrary native memory access from scripts
- self-modifying scripts
- network-transparent script replication
- LLM-driven runtime code generation

Those can wait.

## Practical North Star

For 1.0, success looks like this:

> "A Tremor package can contain or reference script content that drives ECS-level
> gameplay behavior, with fast iteration, clean modding, and no custom VM burden."

That is enough to be extremely valuable.
