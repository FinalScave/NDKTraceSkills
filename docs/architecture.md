# Architecture Overview

NDKTraceMCP is organized around two primary layers:

- A C++ CLI that performs deterministic project resolution, input validation, and native stack restoration.
- A Codex skill that drives the workflow, asks for missing artifacts when CLI-first resolution stays incomplete, and presents evidence.

## Design Goals

- Keep stack parsing, project resolution, and symbolization in the CLI.
- Let the skill prefer CLI project resolution before reading the workspace directly.
- Use AI fallback only when CLI project resolution remains partial, ambiguous, or not found.
- Keep final conclusions tied to deterministic CLI output, not prompt-only inference.

## Current Boundaries

- `cli/src/main.cpp` owns command parsing and top-level orchestration.
- `ndktrace-core` owns crash artifact parsing, Android project resolution, ELF BuildId extraction, symbol file lookup, toolchain discovery, and process execution.
- `skills/android-native-crash-analysis/` owns task guidance, CLI invocation order, AI fallback rules, and evidence rules.

## Command and Workflow Surface

- CLI commands: `resolve-project`, `restore`, `scan-ndk`, `validate`
- Recommended local workflow: run `resolve-project`, inspect the workspace only when project resolution is incomplete, run `validate`, then run `restore`
- Fallback boundary: the skill may inspect the workspace and choose explicit paths, but the CLI remains the only deterministic executor

## Near-Term Work

- Extend the parser module so `restore` can consume more crash artifacts directly, including raw logcat excerpts and fuller tombstone variants.
- Use extracted `BuildId`, `fileOffset`, and `loadBase` to make local symbol matching more reliable across more Android packaging layouts.
- Add more end-to-end tests around real crash samples, project-aware resolution, and richer source location outputs.
