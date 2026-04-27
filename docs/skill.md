# Skill Guide

This page describes the Codex skill in `skills/android-native-crash-analysis/`.

## Purpose

The skill tells the model when a task is really an Android native crash analysis task, routes deterministic work through the local CLI, and uses workspace inspection only when CLI project resolution is incomplete.

## Current Focus

- trigger on tombstones, native backtraces, JNI crashes, and NDK crash triage requests
- call `ndktrace-cli resolve-project` when the user provides a project path instead of explicit NDK and symbol paths
- inspect the workspace only when CLI project resolution returns `partial`, `ambiguous`, or `not_found`
- call the CLI with explicit validated paths after the skill has selected the module, variant, ABI, and preferred symbol output
- require deterministic symbolization before inference
- separate observed evidence from conclusions

## Workflow Shape

- start from the richest available inputs: crash text plus explicit NDK and symbol paths, or a `project_path` with optional module, variant, ABI, and library hints
- call `ndktrace-cli resolve-project` first when any native path must be discovered from the Android project
- inspect the workspace only when `resolve-project` reports `partial`, `ambiguous`, or `not_found`, then retry the CLI with explicit `--ndk` and `--so` values
- call `ndktrace-cli validate` on explicit or resolved paths when the native artifact set is incomplete or not yet confirmed
- call `ndktrace-cli restore` only after the skill has selected the concrete local paths it will use for symbolization
- summarize resolved frames, frame metadata, selected project context, fallback source, confidence, and missing artifacts

## Notes

The skill should stay short and should describe the local workflow contract instead of duplicating CLI implementation details. The CLI remains the only deterministic executor.
