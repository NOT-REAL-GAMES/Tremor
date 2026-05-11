# Tremor Source Layout

This directory is the target home for Tremor's long-term engine module layout.

The current codebase still lives mostly at the repository root, but the build
now mirrors this structure conceptually through CMake module lists and IDE
source groups. The migration rule is simple:

- `Foundation` must not depend on `Runtime` or `Editor`
- `Runtime` must not depend on `Editor`
- `Editor` code must stay out of cook/runtime-only builds

The goal is to move from a single large executable with broad include
visibility toward explicit engine modules with tighter ownership boundaries.

Current status:

- physical file moves are in progress
- module ownership is tracked in `cmake/TremorModules.cmake`
- the migration roadmap lives in
  [C:\Projects\Tremor\docs\engine-module-refactor-roadmap.md](C:\Projects\Tremor\docs\engine-module-refactor-roadmap.md)
