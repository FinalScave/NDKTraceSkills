---
name: android-native-crash-analysis
description: Analyze Android native crashes through a CLI-first workflow, use AI fallback only when project resolution is incomplete, and present evidence-driven triage.
---

# Android Native Crash Analysis

Use this skill when the task involves tombstones, logcat traces, native backtraces, ANR/crash reports with native frames, or JNI/NDK failures on Android.

## Workflow

- Collect the crash text, app/version context, ABI, and the richest available Android inputs: explicit NDK and symbol paths, or a `project_path` with optional module, variant, ABI, library name, and build hints.
- Run `ndktrace-cli resolve-project` first when any build input must be discovered from the Android project.
- Use CLI-selected module, variant, ABI, `ndk_path`, and preferred symbol output when project resolution returns `resolved`.
- Inspect the workspace only when `resolve-project` reports `partial`, `ambiguous`, or `not_found`, then retry the CLI with explicit paths.
- Prefer `sdk.dir/ndk/<ndkVersion>` when the project declares both values. Use `ndktrace-cli scan-ndk` only when the project metadata does not resolve a usable local NDK path.
- Prefer unstripped native outputs under `build/intermediates/cxx/<variant>/**/obj/<abi>/` when selecting a `.so` or symbol directory.
- Run `ndktrace-cli validate` before symbolization when the symbol inputs are uncertain, even when they were auto-resolved from the Android project.
- Run `ndktrace-cli restore` to resolve frames before any root-cause explanation.
- Summarize the likely failure path, resolved project context, fallback source, evidence, and blocking gaps after deterministic restoration.

## Rules

- Do not guess symbols, addresses, or source locations when the local CLI can validate or restore them deterministically.
- Treat `resolve-project` as the default way to discover module, variant, ABI, NDK, and symbol paths.
- Inspect Android project metadata directly only after `resolve-project` fails to produce a usable result.
- Treat ABI, build ID, load bias, module selection, variant selection, and symbol mismatches as blockers until they are resolved.
- Surface diagnostics when project resolution falls back, finds multiple candidate modules or variants, or cannot map the requested ABI to symbols.
- State whether `ndk_path` and `symbol_path` came from direct user input, CLI project resolution, or skill fallback before presenting conclusions.
- State the selected module, variant, ABI, `ndk_path`, and `symbol_path` before presenting conclusions when those values were auto-resolved.
- Keep conclusions tied to resolved frames, tool output, and binary evidence.
- Ask for the exact tombstone, unstripped libraries, symbol files, and build metadata when the input set is incomplete.

## Output

Provide a concise incident-style analysis with:

- crash signature
- resolved project context when auto-derived local defaults were used
- fallback source when the skill had to override incomplete project resolution
- resolved native frames
- probable faulting component
- confidence level
- missing artifacts, if any
