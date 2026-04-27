# NDKTraceMCP

NDKTraceMCP is a local toolchain for Android native crash restoration and AI-assisted analysis. The recommended workflow is a project-aware agent skill that runs a deterministic C++ CLI first and falls back to guided workspace inspection only when project resolution is incomplete. The repository is organized around three primary parts:

- a C++ CLI in `cli/`
- a shared core library in `cli/` that includes crash artifact parsing and symbolization helpers
- a Codex skill in `skills/android-native-crash-analysis/`

The current scaffold already includes a buildable CLI, project-aware Android resolution, and a local skill workflow that can fall back to workspace inspection when project resolution is incomplete.

## Repository Layout

- `cli/` contains the CMake-based C++ command-line application
- `skills/android-native-crash-analysis/` contains the Codex skill
- `docs/` contains the project notes and contracts

## Documentation

- [Documentation index](docs/index.md)
- [Architecture overview](docs/architecture.md)
- [CLI guide](docs/cli.md)
- [Skill guide](docs/skill.md)

## Current Scope

- Define the CLI command surface and JSON output contract, including artifact, frame, and memory-map metadata fields
- Keep crash artifact parsing in the shared core library instead of the CLI entry point
- Prefer verified ELF BuildId matches before path or basename fallback when selecting local symbol files
- Normalize logcat-wrapped native crash lines before frame parsing so raw logcat excerpts can be restored directly
- Let the CLI resolve Android project metadata, module selection, NDK paths, and preferred unstripped symbols when explicit paths are missing
- Let the local analysis skill call the CLI first and use workspace inspection only when the CLI reports partial or ambiguous project resolution
- Keep deterministic stack restoration, validation, and BuildId-aware matching in the CLI

## Recommended Local Workflow

- provide the crash text plus either explicit `ndk_path` and `symbol_path` values or an Android `project_path`
- let the skill call `ndktrace-cli resolve-project` first to resolve the module, variant, ABI, NDK path, and preferred unstripped `.so`
- use skill-driven workspace inspection only when `resolve-project` returns `partial`, `ambiguous`, or `not_found`
- run `ndktrace-cli validate` before stack restoration when the selected native inputs are uncertain
- run `ndktrace-cli restore` only after the skill has selected concrete local paths

## Quick Start

Build the CLI:

```powershell
cmake -S cli -B cli/build
cmake --build cli/build --config Debug
```

Run the scanner:

```powershell
cli\build\Debug\ndktrace-cli.exe scan-ndk --pretty
```

Resolve an Android project directly from a crash artifact:

```powershell
cli\build\Debug\ndktrace-cli.exe resolve-project --project D:\Project\Android --stack-file crash.txt --pretty
```

Run the CLI tests:

```powershell
ctest --test-dir cli/build -C Debug --output-on-failure
```
