#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ndktrace/services.h"

namespace {

using ndktrace::ResolveProjectRequest;
using ndktrace::RunResolveProject;
using ndktrace::RunValidate;
using ndktrace::ValidateRequest;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

std::filesystem::path CreateTempRoot() {
    const auto unique_suffix = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("ndktrace_project_tests_" + unique_suffix);
    std::filesystem::create_directories(root);
    return root;
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

void WriteFakeNdk(const std::filesystem::path& ndk_path) {
    std::filesystem::create_directories(ndk_path / "build");
    std::filesystem::create_directories(ndk_path / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin");
    WriteTextFile(ndk_path / "source.properties", "Pkg.Revision=28.2.13676358\n");
    WriteTextFile(
        ndk_path / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-symbolizer.exe",
        "");
    WriteTextFile(
        ndk_path / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-addr2line.exe",
        "");
}

struct AndroidFixture {
    std::filesystem::path root;
    std::filesystem::path ndk_path;
    std::filesystem::path symbol_path;
    std::filesystem::path crash_path;
};

AndroidFixture CreateAndroidFixture(bool add_extra_library = false) {
    AndroidFixture fixture;
    fixture.root = CreateTempRoot();

    const auto sdk_dir = fixture.root / "sdk";
    fixture.ndk_path = sdk_dir / "ndk" / "28.2.13676358";
    WriteFakeNdk(fixture.ndk_path);

    WriteTextFile(
        fixture.root / "local.properties",
        "sdk.dir=" + sdk_dir.generic_string() + "\n");
    WriteTextFile(
        fixture.root / "settings.gradle",
        "include ':app', ':sweeteditor'\n");
    WriteTextFile(
        fixture.root / "app" / "build.gradle",
        "plugins { id 'com.android.application' }\nandroid { namespace 'demo.app' }\n");
    WriteTextFile(
        fixture.root / "sweeteditor" / "build.gradle",
        "plugins { id 'com.android.library' }\n"
        "android {\n"
        "  ndkVersion '28.2.13676358'\n"
        "  externalNativeBuild {\n"
        "    cmake {\n"
        "      path 'src/main/cpp/CMakeLists.txt'\n"
        "    }\n"
        "  }\n"
        "}\n");

    fixture.symbol_path =
        fixture.root / "sweeteditor" / "build" / "intermediates" / "cxx" / "Debug" / "hash" / "obj" / "arm64-v8a" / "libsweeteditor.so";
    WriteTextFile(fixture.symbol_path, "placeholder");
    WriteTextFile(
        fixture.root / "sweeteditor" / "build" / "intermediates" / "stripped_native_libs" / "debug" / "out" / "lib" / "arm64-v8a" / "libsweeteditor.so",
        "stripped");

    if (add_extra_library) {
        WriteTextFile(
            fixture.root / "sweeteditor" / "build" / "intermediates" / "cxx" / "Debug" / "hash" / "obj" / "arm64-v8a" / "libother.so",
            "placeholder");
    }

    fixture.crash_path = fixture.root / "crash.txt";
    WriteTextFile(
        fixture.crash_path,
        "#00 pc 00000000001a6614 /data/app/demo/lib/arm64/libsweeteditor.so (demo+32)\n");

    return fixture;
}

void RemoveFixture(const AndroidFixture& fixture) {
    std::filesystem::remove_all(fixture.root);
}

void TestResolveProjectFromCrashText() {
    const auto fixture = CreateAndroidFixture();
    const auto scope_exit = [&fixture]() {
        RemoveFixture(fixture);
    };

    ResolveProjectRequest request;
    request.project_path = fixture.root.string();
    request.stack_file = fixture.crash_path.string();

    const auto result = RunResolveProject(request);
    Expect(result.ok, "Expected resolve-project to succeed.");
    Expect(result.project_resolution.status == "resolved", "Expected resolved project status.");
    Expect(result.project_resolution.module == "sweeteditor", "Expected sweeteditor module.");
    Expect(
        result.project_resolution.ndk_path == fixture.ndk_path.generic_string(),
        "Expected resolved NDK path.");
    Expect(
        result.project_resolution.preferred_symbol_path == fixture.symbol_path.generic_string(),
        "Expected resolved symbol path.");
    Expect(
        !result.project_resolution.library_names.empty() &&
            result.project_resolution.library_names.front() == "libsweeteditor.so",
        "Expected library name to be extracted from the crash trace.");

    scope_exit();
}

void TestValidateResolvesProjectInputs() {
    const auto fixture = CreateAndroidFixture();
    const auto scope_exit = [&fixture]() {
        RemoveFixture(fixture);
    };

    ValidateRequest request;
    request.project_path = fixture.root.string();
    request.library_name = "libsweeteditor.so";

    const auto result = RunValidate(request);
    Expect(result.ok, "Expected validate to succeed with project-derived paths.");
    Expect(result.project_resolution.ok, "Expected project resolution to succeed for validate.");
    Expect(result.ndk_path == fixture.ndk_path.generic_string(), "Expected validate NDK path.");
    Expect(result.so_path == fixture.symbol_path.generic_string(), "Expected validate symbol path.");
    Expect(result.so_is_file, "Expected project-derived symbol path to be a file.");

    scope_exit();
}

void TestResolveProjectReturnsPartialWhenLibraryMissing() {
    const auto fixture = CreateAndroidFixture();
    const auto scope_exit = [&fixture]() {
        RemoveFixture(fixture);
    };

    ResolveProjectRequest request;
    request.project_path = fixture.root.string();
    request.library_name = "libmissing.so";

    const auto result = RunResolveProject(request);
    Expect(!result.ok, "Expected resolve-project to fail when the requested library is missing.");
    Expect(result.project_resolution.status == "partial", "Expected partial project status.");
    Expect(!result.project_resolution.errors.empty(), "Expected symbol resolution errors.");

    scope_exit();
}

void TestResolveProjectReturnsAmbiguousWhenLibraryIsUnspecified() {
    const auto fixture = CreateAndroidFixture(true);
    const auto scope_exit = [&fixture]() {
        RemoveFixture(fixture);
    };

    ResolveProjectRequest request;
    request.project_path = fixture.root.string();

    const auto result = RunResolveProject(request);
    Expect(!result.ok, "Expected resolve-project to remain ambiguous with multiple libraries.");
    Expect(result.project_resolution.status == "ambiguous", "Expected ambiguous project status.");
    Expect(!result.project_resolution.ambiguities.empty(), "Expected ambiguity diagnostics.");

    scope_exit();
}

}  // namespace

int main() {
    TestResolveProjectFromCrashText();
    TestValidateResolvesProjectInputs();
    TestResolveProjectReturnsPartialWhenLibraryMissing();
    TestResolveProjectReturnsAmbiguousWhenLibraryIsUnspecified();
    return 0;
}
