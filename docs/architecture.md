# Architecture Overview

NDKTraceMCP is organized around three layers:

- A C++ CLI that performs deterministic native stack restoration.
- An MCP server wrapper that translates MCP tool calls into CLI invocations.
- A Codex skill that tells the model when to call MCP tools and how to present evidence.

## Design Goals

- Keep stack parsing and symbolization in the CLI.
- Keep the MCP layer thin, predictable, and stateless.
- Keep the skill focused on workflow, not implementation details.

## Current Boundaries

- `cli/src/main.cpp` owns command parsing and top-level orchestration.
- `ndktrace-core` owns crash artifact parsing, ELF BuildId extraction, symbol file lookup, toolchain discovery, and process execution.
- `mcp/server/` owns MCP transport, CLI path resolution, and JSON forwarding.
- `skills/android-native-crash-analysis/` owns task guidance, evidence rules, and fallback behavior.

## Command and Tool Surface

- CLI commands: `restore`, `scan-ndk`, `validate`
- MCP tools: `restore_native_stack`, `scan_ndk`, `resolve_android_project_context`, `validate_native_symbols`

## Near-Term Work

- Extend the parser module so `restore` can consume more crash artifacts directly, including raw logcat excerpts and fuller tombstone variants.
- Use extracted `BuildId`, `fileOffset`, and `loadBase` to make local symbol matching more reliable across more Android packaging layouts.
- Add more end-to-end tests around real crash samples and richer source location outputs before wiring a GUI or another client on top.
