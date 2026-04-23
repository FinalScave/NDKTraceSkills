# CLI Guide

This page describes the current C++ command-line scaffold in `cli/`.

## Build

```powershell
cmake -S cli -B cli/build
cmake --build cli/build --config Debug
```

The default Windows executable path is:

```text
cli/build/Debug/ndktrace-cli.exe
```

## Commands

- `scan-ndk`
  - discovers local Android NDK candidates
- `validate`
  - checks that an NDK path and symbol path are usable
- `restore`
  - reads crash artifacts, resolves local `.so` files, and invokes `llvm-symbolizer` or `llvm-addr2line`

## Example

```powershell
Get-Content crash.txt | cli\build\Debug\ndktrace-cli.exe restore --ndk D:\Android\Sdk\ndk\26.1.10909125 --so D:\symbols --stdin --pretty
```

## Output

The CLI returns JSON by default. The main top-level sections are:

- `meta`
- `request`
- `artifact`
- `toolchain`
- `summary`
- `frames`
- `memoryMaps`
- `errors`

The parser module preserves more frame context than the initial scaffold. It now keeps:

- `artifact.abi`
- `artifact.signal`
- `artifact.tombstonePath`
- `artifact.hasBacktrace`
- `artifact.hasMemoryMap`
- `frameNumber`
- `frameKind`
- `frameSuffix`
- `symbolHint`
- `buildId`
- `fileOffset`
- `loadBase`
- `resolvedBuildId`
- `symbolMatchStrategy`
- `symbol.file`
- `symbol.line`
- `symbol.column`

`frameSuffix` still carries the raw trailing text, while `buildId`, `fileOffset`, and `loadBase` are exposed as explicit fields when present. The `memoryMaps` array preserves parsed image metadata from tombstone memory map sections.

Local symbol selection is now BuildId-aware:

- `restore` first looks for local ELF candidates whose `NT_GNU_BUILD_ID` matches the crash artifact
- when no verified BuildId match is available, it falls back to exact path or basename selection
- candidates with a known mismatched BuildId are rejected instead of being used for symbolization

The frame output reflects that decision through:

- `resolvedBuildId`
- `symbolMatchStrategy`
- warning messages when a fallback path was used without a verified BuildId match
- structured source locations with file, line, and optional column values

The parser covers these common shapes:

- `#NN pc <addr> <path> (...)`
- `#NN 0x<addr> <path> (...)`
- `pc <addr> <path>`
- logcat-wrapped lines whose payload contains one of the frame shapes above

The artifact parser also recognizes:

- `ABI: 'arm64'`
- `Fatal signal 11 ...`
- `signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0xc`
- `Tombstone written to: ...`
- `memory map:` sections with `BuildId` and `load base` metadata

Tool output parsing also accepts both `file:line` and `file:line:column` locations, including Windows-style paths such as `D:/repo/file.cpp:1197:39`.

## Tests

The CLI uses lightweight CTest targets with parser, artifact, BuildId, resolver, and JSON contract coverage:

```powershell
ctest --test-dir cli/build -C Debug --output-on-failure
```
