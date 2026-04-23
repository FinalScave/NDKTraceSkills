# MCP Server Wrapper

This page describes the MCP server in `mcp/server/server.py`.

## Purpose

The server exposes the CLI through MCP tools and keeps the transport layer separate from native trace logic.

## Current Tools

- `scan_ndk`
- `resolve_android_project_context`
- `validate_native_symbols`
- `restore_native_stack`

## Behavior

- Resolve the CLI path from `NDKTRACE_CLI` when set.
- Fall back to common local build outputs under `cli/build/`.
- Parse Android project metadata from `settings.gradle`, `local.properties`, `build.gradle`, and `build.gradle.kts` when a project path is provided.
- Resolve `sdk.dir`, `ndkVersion`, the preferred NDK installation, and native symbol output candidates before invoking the CLI.
- Execute the CLI with `subprocess.run`.
- Parse stdout as JSON and attach CLI diagnostics while forwarding the CLI frame metadata contract unchanged.
- Preserve BuildId-aware fields such as `buildId`, `resolvedBuildId`, `symbolMatchStrategy`, `symbol.line`, `symbol.column`, and any fallback warnings for the model layer.
- Prefer unstripped native outputs under `build/intermediates/cxx/.../obj/<abi>/` when auto-selecting symbol paths from an Android project.

## Local Use

Install the dependency set in a Python environment that has the official MCP SDK:

```powershell
pip install "mcp[cli]"
```

Then run the server directly:

```powershell
python mcp\server\server.py
```
