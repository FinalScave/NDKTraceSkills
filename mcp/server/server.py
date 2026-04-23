from __future__ import annotations

import json
import os
import platform
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    from mcp.server.fastmcp import FastMCP
except ModuleNotFoundError as import_error:
    _MCP_IMPORT_ERROR = import_error

    class FastMCP:  # type: ignore[override]
        def __init__(self, *_args: Any, **_kwargs: Any) -> None:
            self._import_error = _MCP_IMPORT_ERROR

        def tool(self, *_args: Any, **_kwargs: Any):
            def decorator(func):
                return func

            return decorator

        def run(self) -> None:
            raise ModuleNotFoundError(
                "The official MCP SDK is required to run this server. "
                "Install it with: pip install \"mcp[cli]\""
            ) from self._import_error


mcp = FastMCP("NDKTraceMCP", json_response=True)


class CliInvocationError(RuntimeError):
    pass


class ProjectResolutionError(RuntimeError):
    pass


@dataclass(slots=True)
class ModuleSpec:
    name: str
    path: Path
    build_files: list[Path]
    ndk_version: str | None
    ndk_path: Path | None
    has_native_build: bool


@dataclass(slots=True)
class SymbolCandidate:
    path: Path
    library_name: str
    source: str
    module: str
    variant: str
    abi: str
    stripped: bool
    score: int

    def to_payload(self) -> dict[str, Any]:
        return {
            "path": to_generic_path(self.path),
            "libraryName": self.library_name,
            "source": self.source,
            "module": self.module,
            "variant": self.variant,
            "abi": self.abi,
            "stripped": self.stripped,
            "score": self.score,
        }


def clean_optional(value: str | None) -> str | None:
    if value is None:
        return None
    trimmed = value.strip()
    return trimmed or None


def to_generic_path(path: Path | None) -> str | None:
    if path is None:
        return None
    return path.resolve().as_posix()


def resolve_cli_path() -> Path:
    configured = os.getenv("NDKTRACE_CLI")
    if configured:
        return Path(configured).expanduser().resolve()

    root = Path(__file__).resolve().parents[2]
    executable = "ndktrace-cli.exe" if platform.system().lower().startswith("win") else "ndktrace-cli"
    candidates = [
        root / "cli" / "build" / executable,
        root / "cli" / "build" / "Debug" / executable,
        root / "cli" / "build" / "Release" / executable,
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def run_cli(arguments: list[str], stdin_text: str | None = None) -> dict[str, Any]:
    cli_path = resolve_cli_path()
    if not cli_path.exists():
        raise CliInvocationError(
            f"CLI binary was not found at {cli_path}. "
            "Build the C++ CLI first or set NDKTRACE_CLI to the executable path."
        )

    process = subprocess.run(
        [str(cli_path), *arguments],
        input=stdin_text,
        text=True,
        capture_output=True,
        check=False,
    )

    stdout = process.stdout.strip()
    stderr = process.stderr.strip()
    if not stdout:
        raise CliInvocationError(stderr or "The CLI did not return JSON output.")

    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError as exc:
        raise CliInvocationError(f"Failed to decode CLI output as JSON: {exc}") from exc

    if stderr:
        payload.setdefault("diagnostics", {})["stderr"] = stderr
    payload.setdefault("diagnostics", {})["exitCode"] = process.returncode
    return payload


def read_text_if_exists(path: Path) -> str | None:
    try:
        if path.exists() and path.is_file():
            return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    return None


def has_build_file(path: Path) -> bool:
    return (path / "build.gradle").exists() or (path / "build.gradle.kts").exists()


def normalize_module_name(module: str | None) -> str | None:
    cleaned = clean_optional(module)
    if cleaned is None:
        return None
    return cleaned if cleaned.startswith(":") else f":{cleaned}"


def module_short_name(module_name: str) -> str:
    return module_name.split(":")[-1]


def decode_properties_value(value: str) -> str:
    decoded: list[str] = []
    index = 0
    while index < len(value):
        ch = value[index]
        if ch != "\\":
            decoded.append(ch)
            index += 1
            continue

        index += 1
        if index >= len(value):
            decoded.append("\\")
            break

        escaped = value[index]
        index += 1
        if escaped == "u" and index + 4 <= len(value):
            code = value[index:index + 4]
            if re.fullmatch(r"[0-9a-fA-F]{4}", code):
                decoded.append(chr(int(code, 16)))
                index += 4
                continue
            decoded.append("u")
            continue

        mapping = {
            "t": "\t",
            "n": "\n",
            "r": "\r",
            "f": "\f",
        }
        decoded.append(mapping.get(escaped, escaped))
    return "".join(decoded)


def parse_local_properties(path: Path) -> dict[str, str]:
    properties_path = path / "local.properties"
    text = read_text_if_exists(properties_path)
    if text is None:
        return {}

    properties: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith("!"):
            continue
        match = re.match(r"^\s*([^:=\s][^:=]*)\s*[:=]\s*(.*)$", raw_line)
        if not match:
            continue
        key = match.group(1).strip()
        value = decode_properties_value(match.group(2).strip())
        properties[key] = value
    return properties


def find_android_project_root(project_path: str) -> Path:
    start = Path(project_path).expanduser().resolve()
    if not start.exists():
        raise ProjectResolutionError(f"Android project path does not exist: {start}")
    current = start if start.is_dir() else start.parent

    for candidate in (current, *current.parents):
        if (candidate / "settings.gradle").exists() or (candidate / "settings.gradle.kts").exists():
            return candidate

    for candidate in (current, *current.parents):
        if (candidate / "local.properties").exists():
            return candidate

    return current


def parse_included_modules(settings_text: str) -> set[str]:
    modules: set[str] = set()
    include_pattern = re.compile(r"^\s*include\s*(.+)$", re.MULTILINE)
    for match in include_pattern.finditer(settings_text):
        body = match.group(1)
        for module_name in re.findall(r"['\"](:[^'\"]+)['\"]", body):
            normalized = normalize_module_name(module_name)
            if normalized is not None:
                modules.add(normalized)
    return modules


def parse_module_directory_overrides(project_root: Path, settings_text: str) -> dict[str, Path]:
    overrides: dict[str, Path] = {}
    pattern = re.compile(
        r"project\(\s*['\"](:[^'\"]+)['\"]\s*\)\.projectDir\s*=\s*file\(\s*['\"]([^'\"]+)['\"]\s*\)"
    )
    for module_name, relative_dir in pattern.findall(settings_text):
        normalized = normalize_module_name(module_name)
        if normalized is None:
            continue
        overrides[normalized] = (project_root / relative_dir).resolve()
    return overrides


def default_module_path(project_root: Path, module_name: str) -> Path:
    parts = [segment for segment in module_name.split(":") if segment]
    if not parts:
        return project_root
    return project_root.joinpath(*parts)


def extract_first_match(text: str, pattern: str) -> str | None:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        return None
    value = match.group(1).strip()
    return value or None


def build_module_spec(module_name: str, module_path: Path) -> ModuleSpec:
    build_files = [candidate for candidate in (module_path / "build.gradle", module_path / "build.gradle.kts") if candidate.exists()]
    combined_text = "\n".join(read_text_if_exists(build_file) or "" for build_file in build_files)

    ndk_version = extract_first_match(combined_text, r"ndkVersion\s*(?:=)?\s*['\"]([^'\"]+)['\"]")
    raw_ndk_path = extract_first_match(combined_text, r"ndkPath\s*(?:=)?\s*['\"]([^'\"]+)['\"]")
    ndk_path = None
    if raw_ndk_path is not None:
        candidate = Path(raw_ndk_path).expanduser()
        ndk_path = (module_path / candidate).resolve() if not candidate.is_absolute() else candidate.resolve()

    lower_text = combined_text.lower()
    has_native_build = ndk_version is not None or (
        "externalnativebuild" in lower_text and "cmake" in lower_text and "path" in lower_text
    )

    return ModuleSpec(
        name=module_name,
        path=module_path.resolve(),
        build_files=build_files,
        ndk_version=ndk_version,
        ndk_path=ndk_path,
        has_native_build=has_native_build,
    )


def discover_modules(project_root: Path, input_path: Path) -> list[ModuleSpec]:
    module_names: set[str] = set()
    overrides: dict[str, Path] = {}

    for settings_file in (project_root / "settings.gradle", project_root / "settings.gradle.kts"):
        settings_text = read_text_if_exists(settings_file)
        if settings_text is None:
            continue
        module_names.update(parse_included_modules(settings_text))
        overrides.update(parse_module_directory_overrides(project_root, settings_text))

    if not module_names and has_build_file(project_root):
        module_names.add(f":{project_root.name}")

    specs: list[ModuleSpec] = []
    for module_name in sorted(module_names):
        module_path = overrides.get(module_name, default_module_path(project_root, module_name))
        if not module_path.exists():
            continue
        if not has_build_file(module_path):
            continue
        specs.append(build_module_spec(module_name, module_path))

    if has_build_file(input_path) and all(spec.path != input_path for spec in specs):
        specs.append(build_module_spec(f":{input_path.name}", input_path))

    return specs


def resolve_sdk_dir(project_root: Path) -> Path | None:
    properties = parse_local_properties(project_root)
    sdk_dir = properties.get("sdk.dir")
    if sdk_dir:
        candidate = Path(sdk_dir).expanduser()
        if candidate.exists():
            return candidate.resolve()

    for env_name in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        env_value = clean_optional(os.getenv(env_name))
        if env_value is None:
            continue
        candidate = Path(env_value).expanduser()
        if candidate.exists():
            return candidate.resolve()

    local_app_data = clean_optional(os.getenv("LOCALAPPDATA"))
    if local_app_data is not None:
        candidate = Path(local_app_data) / "Android" / "Sdk"
        if candidate.exists():
            return candidate.resolve()

    user_profile = clean_optional(os.getenv("USERPROFILE"))
    if user_profile is not None:
        candidate = Path(user_profile) / "AppData" / "Local" / "Android" / "Sdk"
        if candidate.exists():
            return candidate.resolve()

    home = clean_optional(os.getenv("HOME"))
    if home is not None:
        for candidate in (Path(home) / "Android" / "Sdk", Path(home) / "Library" / "Android" / "sdk"):
            if candidate.exists():
                return candidate.resolve()

    return None


def is_valid_ndk_directory(path: Path) -> bool:
    return path.exists() and path.is_dir() and (path / "toolchains").exists() and (path / "build").exists()


def version_sort_key(value: str) -> tuple[Any, ...]:
    parts = re.split(r"(\d+)", value)
    key: list[Any] = []
    for part in parts:
        if not part:
            continue
        key.append(int(part) if part.isdigit() else part.lower())
    return tuple(key)


def common_ndk_roots() -> list[Path]:
    roots: list[Path] = []
    for env_name in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "NDK_HOME", "NDK_ROOT"):
        env_value = clean_optional(os.getenv(env_name))
        if env_value is not None:
            roots.append(Path(env_value).expanduser())

    sdk_dir = resolve_sdk_dir(Path.cwd())
    if sdk_dir is not None:
        roots.append(sdk_dir / "ndk")

    local_app_data = clean_optional(os.getenv("LOCALAPPDATA"))
    if local_app_data is not None:
        roots.append(Path(local_app_data) / "Android" / "Sdk" / "ndk")

    user_profile = clean_optional(os.getenv("USERPROFILE"))
    if user_profile is not None:
        roots.append(Path(user_profile) / "Android" / "Sdk" / "ndk")
        roots.append(Path(user_profile) / "AppData" / "Local" / "Android" / "Sdk" / "ndk")

    home = clean_optional(os.getenv("HOME"))
    if home is not None:
        roots.append(Path(home) / "Android" / "Sdk" / "ndk")
        roots.append(Path(home) / "Library" / "Android" / "sdk" / "ndk")

    roots.extend(
        [
            Path("C:/Android/ndk"),
            Path("D:/Android/ndk"),
            Path("/usr/local/android-ndk"),
            Path("/opt/android-ndk"),
        ]
    )
    return roots


def discover_local_ndk_candidates() -> list[Path]:
    discovered: dict[str, Path] = {}
    for root in common_ndk_roots():
        if not root.exists():
            continue
        if is_valid_ndk_directory(root):
            discovered[to_generic_path(root) or str(root)] = root.resolve()
            continue
        if not root.is_dir():
            continue
        for child in root.iterdir():
            if is_valid_ndk_directory(child):
                discovered[to_generic_path(child) or str(child)] = child.resolve()
    return sorted(discovered.values(), key=lambda item: version_sort_key(item.name), reverse=True)


def extract_requested_library_names(library_name: str | None, crash_text: str | None) -> list[str]:
    names: list[str] = []
    explicit = clean_optional(library_name)
    if explicit is not None:
        names.append(Path(explicit).name)

    if crash_text:
        for raw_name in re.findall(r"(?<![\w./\\])([^/\s()]+\.so)\b", crash_text):
            basename = Path(raw_name).name
            if basename not in names:
                names.append(basename)
    return names


def path_matches_variant(path: Path, variant: str | None) -> bool:
    variant_name = clean_optional(variant)
    if variant_name is None:
        return True
    path_lower = path.as_posix().lower()
    variant_lower = variant_name.lower()
    return variant_lower in path_lower


def path_matches_abi(path: Path, abi: str | None) -> bool:
    abi_name = clean_optional(abi)
    if abi_name is None:
        return True
    return f"/{abi_name.lower()}/" in path.as_posix().lower()


def collect_symbol_candidates(
    module_spec: ModuleSpec,
    variant: str,
    abi: str,
    requested_libraries: list[str],
) -> list[SymbolCandidate]:
    build_dir = module_spec.path / "build"
    search_roots = [
        ("cxx_obj", build_dir / "intermediates" / "cxx", False, 500),
        ("cmake_obj", build_dir / "intermediates" / "cmake", False, 460),
        ("external_native_build", build_dir / "intermediates" / "externalNativeBuild", False, 420),
        ("library_jni", build_dir / "intermediates" / "library_jni", False, 260),
        ("library_and_local_jars_jni", build_dir / "intermediates" / "library_and_local_jars_jni", False, 250),
        ("merged_native_libs", build_dir / "intermediates" / "merged_native_libs", False, 180),
        ("merged_jni_libs", build_dir / "intermediates" / "merged_jni_libs", False, 170),
        ("stripped_native_libs", build_dir / "intermediates" / "stripped_native_libs", True, 80),
    ]

    requested_set = set(requested_libraries)
    deduped: dict[str, SymbolCandidate] = {}
    for source, root, stripped, base_score in search_roots:
        if not root.exists():
            continue
        for candidate_path in root.rglob("*.so"):
            if not path_matches_variant(candidate_path, variant) or not path_matches_abi(candidate_path, abi):
                continue

            library_basename = candidate_path.name
            if requested_set and library_basename not in requested_set:
                continue

            score = base_score
            if "/obj/" in candidate_path.as_posix().lower():
                score += 40
            if requested_set:
                score += 200
            if module_spec.has_native_build:
                score += 50

            candidate = SymbolCandidate(
                path=candidate_path.resolve(),
                library_name=library_basename,
                source=source,
                module=module_short_name(module_spec.name),
                variant=variant,
                abi=abi,
                stripped=stripped,
                score=score,
            )
            key = to_generic_path(candidate.path) or str(candidate.path)
            existing = deduped.get(key)
            if existing is None or candidate.score > existing.score:
                deduped[key] = candidate

    return sorted(deduped.values(), key=lambda item: (-item.score, item.path.as_posix()))


def choose_preferred_symbol_path(
    candidates: list[SymbolCandidate],
    requested_libraries: list[str],
) -> Path | None:
    if not candidates:
        return None

    if requested_libraries:
        matching = [candidate for candidate in candidates if candidate.library_name in set(requested_libraries)]
        if not matching:
            return None
        if len(requested_libraries) == 1:
            return matching[0].path

        parents = {candidate.path.parent for candidate in matching}
        if len(parents) == 1:
            return next(iter(parents))
        return matching[0].path

    top_candidate = candidates[0]
    if top_candidate.source in {"cxx_obj", "cmake_obj", "external_native_build"}:
        return top_candidate.path.parent
    return top_candidate.path


def select_module_spec(
    module_specs: list[ModuleSpec],
    input_path: Path,
    explicit_module: str | None,
    variant: str,
    abi: str,
    requested_libraries: list[str],
    ambiguities: list[str],
) -> tuple[ModuleSpec | None, dict[str, list[SymbolCandidate]]]:
    if not module_specs:
        return None, {}

    requested_module = normalize_module_name(explicit_module)
    if requested_module is not None:
        for spec in module_specs:
            if spec.name == requested_module or module_short_name(spec.name) == requested_module.lstrip(":"):
                return spec, {}
        raise ProjectResolutionError(f"Android module was not found: {explicit_module}")

    for spec in module_specs:
        if spec.path == input_path.resolve():
            return spec, {}

    preferred_pool = [spec for spec in module_specs if spec.has_native_build]
    candidate_pool = preferred_pool or module_specs
    candidate_cache: dict[str, list[SymbolCandidate]] = {}

    if requested_libraries:
        ranked: list[tuple[ModuleSpec, list[SymbolCandidate]]] = []
        for spec in candidate_pool:
            candidates = collect_symbol_candidates(spec, variant, abi, requested_libraries)
            candidate_cache[spec.name] = candidates
            if candidates:
                ranked.append((spec, candidates))

        if not ranked and preferred_pool:
            for spec in module_specs:
                if spec.name in candidate_cache:
                    continue
                candidates = collect_symbol_candidates(spec, variant, abi, requested_libraries)
                candidate_cache[spec.name] = candidates
                if candidates:
                    ranked.append((spec, candidates))

        if ranked:
            ranked.sort(key=lambda item: (-item[1][0].score, -len(item[1]), item[0].name))
            if len(ranked) > 1 and ranked[0][1][0].score == ranked[1][1][0].score:
                ambiguities.append(
                    "Multiple Android modules expose matching native outputs. "
                    f"Using {module_short_name(ranked[0][0].name)}."
                )
            return ranked[0][0], candidate_cache

    if len(candidate_pool) == 1:
        return candidate_pool[0], candidate_cache

    if candidate_pool:
        ambiguities.append(
            "Multiple Android modules were discovered. "
            f"Using {module_short_name(candidate_pool[0].name)}."
        )
        return candidate_pool[0], candidate_cache

    return module_specs[0], candidate_cache


def resolve_ndk_path(
    sdk_dir: Path | None,
    module_spec: ModuleSpec | None,
    module_specs: list[ModuleSpec],
    warnings: list[str],
    ambiguities: list[str],
) -> tuple[str | None, Path | None]:
    ndk_version = module_spec.ndk_version if module_spec is not None else None
    ndk_path = module_spec.ndk_path if module_spec is not None else None

    if ndk_version is None:
        discovered_versions = {spec.ndk_version for spec in module_specs if spec.ndk_version}
        if len(discovered_versions) == 1:
            ndk_version = next(iter(discovered_versions))
        elif len(discovered_versions) > 1:
            ambiguities.append("Multiple ndkVersion declarations were found across modules.")

    if ndk_path is not None and is_valid_ndk_directory(ndk_path):
        return ndk_version, ndk_path.resolve()

    if sdk_dir is not None and ndk_version is not None:
        candidate = sdk_dir / "ndk" / ndk_version
        if is_valid_ndk_directory(candidate):
            return ndk_version, candidate.resolve()
        warnings.append(f"Declared ndkVersion {ndk_version} was not found under sdk.dir.")

    local_candidates = discover_local_ndk_candidates()
    if ndk_version is not None:
        for candidate in local_candidates:
            if candidate.name == ndk_version:
                return ndk_version, candidate.resolve()

    if sdk_dir is not None:
        ndk_root = sdk_dir / "ndk"
        if ndk_root.exists() and ndk_root.is_dir():
            installed = sorted(
                [candidate for candidate in ndk_root.iterdir() if is_valid_ndk_directory(candidate)],
                key=lambda item: version_sort_key(item.name),
                reverse=True,
            )
            if len(installed) == 1:
                return ndk_version or installed[0].name, installed[0].resolve()
            if installed:
                warnings.append("Using the newest installed NDK under sdk.dir because no exact match was found.")
                return ndk_version or installed[0].name, installed[0].resolve()

    if len(local_candidates) == 1:
        return ndk_version or local_candidates[0].name, local_candidates[0].resolve()
    if len(local_candidates) > 1:
        ambiguities.append("Multiple local NDK installations were found.")
        return ndk_version, local_candidates[0].resolve()

    return ndk_version, None


def resolve_android_project_context_data(
    project_path: str,
    module: str | None = None,
    variant: str = "Debug",
    abi: str = "arm64-v8a",
    library_name: str | None = None,
    crash_text: str | None = None,
) -> dict[str, Any]:
    input_path = Path(project_path).expanduser().resolve()
    project_root = find_android_project_root(project_path)
    module_specs = discover_modules(project_root, input_path)
    warnings: list[str] = []
    ambiguities: list[str] = []
    errors: list[str] = []

    if not module_specs:
        errors.append("No Android Gradle modules were found under the provided project path.")

    requested_libraries = extract_requested_library_names(library_name, crash_text)
    selected_module, candidate_cache = select_module_spec(
        module_specs,
        input_path,
        module,
        variant,
        abi,
        requested_libraries,
        ambiguities,
    ) if module_specs else (None, {})

    sdk_dir = resolve_sdk_dir(project_root)
    if sdk_dir is None:
        warnings.append("Android sdk.dir was not found in local.properties or the environment.")

    ndk_version, ndk_path = resolve_ndk_path(sdk_dir, selected_module, module_specs, warnings, ambiguities)
    if ndk_path is None:
        errors.append("Unable to resolve an Android NDK path from the project or local environment.")

    symbol_candidates = []
    if selected_module is not None:
        symbol_candidates = candidate_cache.get(selected_module.name)
        if symbol_candidates is None:
            symbol_candidates = collect_symbol_candidates(selected_module, variant, abi, requested_libraries)

    preferred_symbol_path = choose_preferred_symbol_path(symbol_candidates, requested_libraries)
    if requested_libraries and preferred_symbol_path is None:
        errors.append("Unable to resolve a native symbol path from the Android build outputs.")
    elif preferred_symbol_path is None:
        warnings.append("No matching native symbol output was found under the module build directory.")

    visible_candidates = symbol_candidates[:20]
    if len(symbol_candidates) > len(visible_candidates):
        warnings.append(
            f"Only the top {len(visible_candidates)} symbol candidates are included out of {len(symbol_candidates)} matches."
        )

    return {
        "ok": not errors,
        "inputPath": to_generic_path(input_path),
        "projectPath": to_generic_path(project_root),
        "module": module_short_name(selected_module.name) if selected_module is not None else None,
        "moduleName": selected_module.name if selected_module is not None else None,
        "modulePath": to_generic_path(selected_module.path) if selected_module is not None else None,
        "variant": variant,
        "abi": abi,
        "sdkDir": to_generic_path(sdk_dir),
        "ndkVersion": ndk_version,
        "ndkPath": to_generic_path(ndk_path),
        "libraryNames": requested_libraries,
        "preferredSymbolPath": to_generic_path(preferred_symbol_path),
        "symbolCandidates": [candidate.to_payload() for candidate in visible_candidates],
        "warnings": warnings,
        "ambiguities": ambiguities,
        "errors": errors,
    }


def resolve_native_inputs(
    ndk_path: str | None,
    symbol_path: str | None,
    project_path: str | None,
    module: str | None,
    variant: str,
    abi: str,
    library_name: str | None,
    crash_text: str | None,
) -> tuple[str, str, dict[str, Any] | None]:
    resolved_ndk_path = clean_optional(ndk_path)
    resolved_symbol_path = clean_optional(symbol_path)
    project_context = None

    if clean_optional(project_path) is not None:
        project_context = resolve_android_project_context_data(
            project_path=project_path or "",
            module=module,
            variant=variant,
            abi=abi,
            library_name=library_name,
            crash_text=crash_text,
        )
        if resolved_ndk_path is None:
            resolved_ndk_path = clean_optional(project_context.get("ndkPath"))
        if resolved_symbol_path is None:
            resolved_symbol_path = clean_optional(project_context.get("preferredSymbolPath"))

    if resolved_ndk_path is None:
        if project_context is not None and project_context.get("errors"):
            raise CliInvocationError("; ".join(project_context["errors"]))
        raise CliInvocationError("An NDK path is required. Provide ndk_path or project_path.")

    if resolved_symbol_path is None:
        if project_context is not None and project_context.get("errors"):
            raise CliInvocationError("; ".join(project_context["errors"]))
        raise CliInvocationError("A symbol path is required. Provide symbol_path or project_path.")

    return resolved_ndk_path, resolved_symbol_path, project_context


def attach_project_context(
    payload: dict[str, Any],
    project_context: dict[str, Any] | None,
    resolved_ndk_path: str,
    resolved_symbol_path: str | None = None,
) -> dict[str, Any]:
    diagnostics = payload.setdefault("diagnostics", {})
    diagnostics["resolvedInputs"] = {
        "ndkPath": resolved_ndk_path,
        "symbolPath": resolved_symbol_path,
    }
    if project_context is not None:
        payload["projectContext"] = project_context
    return payload


@mcp.tool()
def scan_ndk(
    project_path: str | None = None,
    module: str | None = None,
    pretty: bool = True,
) -> dict[str, Any]:
    """Discover local Android NDK installations and optionally compare them with an Android project's declared NDK."""

    arguments = ["scan-ndk", "--json"]
    if pretty:
        arguments.append("--pretty")
    payload = run_cli(arguments)

    if clean_optional(project_path) is not None:
        project_context = resolve_android_project_context_data(project_path=project_path or "", module=module)
        payload["projectContext"] = project_context
        payload.setdefault("diagnostics", {})["preferredNdkPath"] = project_context.get("ndkPath")

    return payload


@mcp.tool()
def resolve_android_project_context(
    project_path: str,
    module: str | None = None,
    variant: str = "Debug",
    abi: str = "arm64-v8a",
    library_name: str | None = None,
    crash_text: str | None = None,
) -> dict[str, Any]:
    """Resolve the Android module, sdk.dir, ndkVersion, NDK path, and native symbol outputs for a local project."""

    return resolve_android_project_context_data(
        project_path=project_path,
        module=module,
        variant=variant,
        abi=abi,
        library_name=library_name,
        crash_text=crash_text,
    )


@mcp.tool()
def validate_native_symbols(
    ndk_path: str | None = None,
    symbol_path: str | None = None,
    project_path: str | None = None,
    module: str | None = None,
    variant: str = "Debug",
    abi: str = "arm64-v8a",
    library_name: str | None = None,
    crash_text: str | None = None,
    pretty: bool = True,
) -> dict[str, Any]:
    """Validate the NDK and symbol inputs for native stack restoration, optionally resolving them from an Android project."""

    resolved_ndk_path, resolved_symbol_path, project_context = resolve_native_inputs(
        ndk_path=ndk_path,
        symbol_path=symbol_path,
        project_path=project_path,
        module=module,
        variant=variant,
        abi=abi,
        library_name=library_name,
        crash_text=crash_text,
    )

    arguments = ["validate", "--ndk", resolved_ndk_path, "--so", resolved_symbol_path, "--json"]
    if pretty:
        arguments.append("--pretty")
    payload = run_cli(arguments)
    return attach_project_context(payload, project_context, resolved_ndk_path, resolved_symbol_path)


@mcp.tool()
def restore_native_stack(
    crash_text: str,
    ndk_path: str | None = None,
    symbol_path: str | None = None,
    project_path: str | None = None,
    module: str | None = None,
    variant: str = "Debug",
    abi: str = "arm64-v8a",
    library_name: str | None = None,
    tool: str = "auto",
    match_mode: str = "basename",
    recursive_so_search: bool = True,
    pretty: bool = True,
) -> dict[str, Any]:
    """Symbolize Android native crash frames using local NDK binaries and symbol files, with optional Android project discovery."""

    resolved_ndk_path, resolved_symbol_path, project_context = resolve_native_inputs(
        ndk_path=ndk_path,
        symbol_path=symbol_path,
        project_path=project_path,
        module=module,
        variant=variant,
        abi=abi,
        library_name=library_name,
        crash_text=crash_text,
    )

    arguments = [
        "restore",
        "--ndk",
        resolved_ndk_path,
        "--so",
        resolved_symbol_path,
        "--stdin",
        "--json",
        "--tool",
        tool,
        "--match",
        match_mode,
    ]
    if not recursive_so_search:
        arguments.append("--no-recursive-so")
    if pretty:
        arguments.append("--pretty")

    payload = run_cli(arguments, stdin_text=crash_text)
    return attach_project_context(payload, project_context, resolved_ndk_path, resolved_symbol_path)


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
