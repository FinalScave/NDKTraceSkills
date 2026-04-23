---
name: android-native-crash-analysis
description: Analyze Android native crashes using deterministic MCP-based symbolization, stack reconstruction, and evidence-driven triage.
---

# Android Native Crash Analysis

Use this skill when the task involves tombstones, logcat traces, native backtraces, ANR/crash reports with native frames, or JNI/NDK failures on Android.

## Workflow

- Collect the crash text, app/version context, ABI, and either the local NDK plus symbol paths or the Android project path.
- Use `resolve_android_project_context` when the user provides an Android project path, module name, variant, ABI, or only partial native build inputs.
- Use `scan_ndk` when the local NDK path is unknown and no Android project metadata is available.
- Use `validate_native_symbols` before symbolization when the symbol inputs are uncertain, even when they were auto-resolved from an Android project.
- Use `restore_native_stack` to resolve frames before any root-cause explanation.
- Summarize the likely failure path, evidence, and blocking gaps after deterministic restoration.

## Rules

- Do not guess symbols, addresses, or source locations when MCP-backed resolution is available.
- Do not guess the NDK version or symbol output path when the Android project can be inspected directly.
- Treat ABI, build ID, load bias, and symbol mismatches as blockers until they are resolved.
- Surface module, variant, ABI, or symbol-path ambiguities before treating auto-resolved paths as authoritative.
- Keep conclusions tied to resolved frames, tool output, and binary evidence.
- Ask for the exact tombstone, unstripped libraries, symbol files, and build metadata when the input set is incomplete.

## Output

Provide a concise incident-style analysis with:

- crash signature
- resolved native frames
- probable faulting component
- confidence level
- missing artifacts, if any
