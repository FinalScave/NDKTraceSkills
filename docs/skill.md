# Skill Guide

This page describes the Codex skill in `skills/android-native-crash-analysis/`.

## Purpose

The skill tells the model when a task is really an Android native crash analysis task and routes deterministic work through MCP tools before reasoning about root cause.

## Current Focus

- trigger on tombstones, native backtraces, JNI crashes, and NDK crash triage requests
- resolve Android project metadata before symbolization when the user provides a project path instead of explicit NDK and symbol paths
- require deterministic symbolization before inference
- separate observed evidence from conclusions

## Workflow Shape

- resolve project-aware inputs with `resolve_android_project_context` when the Android module, variant, ABI, NDK path, or symbol path is not explicit
- validate inputs when NDK path or symbols are missing
- call MCP tools for discovery, validation, and restoration
- summarize resolved frames, frame metadata, confidence, and missing artifacts

## Notes

The skill should stay short and should not duplicate CLI or MCP implementation details.
