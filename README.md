# NDKTraceMCP

NDKTraceMCP is a small toolchain for Android native crash restoration and AI-assisted analysis. The repository is organized around four parts:

- a C++ CLI in `cli/`
- a shared core library in `cli/` that includes crash artifact parsing and symbolization helpers
- an MCP server wrapper in `mcp/server/`
- a Codex skill in `skills/android-native-crash-analysis/`

The current scaffold already includes a buildable CLI skeleton, an MCP wrapper that shells out to the CLI, and the first skill/documentation pass.

## Repository Layout

- `cli/` contains the CMake-based C++ command-line application
- `mcp/server/` contains the Python MCP wrapper
- `skills/android-native-crash-analysis/` contains the Codex skill
- `docs/` contains the project notes and contracts

## Documentation

- [Documentation index](docs/index.md)
- [Architecture overview](docs/architecture.md)
- [CLI guide](docs/cli.md)
- [MCP server wrapper](docs/mcp-server.md)
- [Skill guide](docs/skill.md)

## Current Scope

- Define the CLI command surface and JSON output contract, including artifact, frame, and memory-map metadata fields
- Keep crash artifact parsing in the shared core library instead of the CLI entry point
- Prefer verified ELF BuildId matches before path or basename fallback when selecting local symbol files
- Normalize logcat-wrapped native crash lines before frame parsing so raw logcat excerpts can be restored directly
- Wrap the CLI with MCP tools instead of duplicating trace logic
- Resolve `sdk.dir`, `ndkVersion`, and preferred native symbol outputs from Android project metadata inside the MCP layer when explicit paths are missing
- Keep the skill focused on workflow and evidence boundaries

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

Run the CLI tests:

```powershell
ctest --test-dir cli/build -C Debug --output-on-failure
```

Check the Python server syntax:

```powershell
python -m py_compile mcp\server\server.py
```
