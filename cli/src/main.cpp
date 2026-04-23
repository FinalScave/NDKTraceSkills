#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ndktrace/json_output.h"
#include "ndktrace/services.h"

namespace {

using ndktrace::RestoreRequest;
using ndktrace::ScanRequest;
using ndktrace::ValidateRequest;

void PrintUsage() {
    std::cout
        << "ndktrace-cli\n"
        << "  restore --ndk <path> --so <path> [--stack-file <file> | --stdin] [--tool auto|symbolizer|addr2line] [--match basename|exact] [--no-recursive-so] [--pretty]\n"
        << "  scan-ndk [--pretty]\n"
        << "  validate --ndk <path> --so <path> [--pretty]\n";
}

std::string RequireValue(const std::vector<std::string>& args, std::size_t index, const std::string& flag) {
    if (index + 1 >= args.size()) {
        throw std::runtime_error("Missing value for " + flag);
    }
    return args[index + 1];
}

RestoreRequest ParseRestore(const std::vector<std::string>& args) {
    RestoreRequest request;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--ndk") {
            request.ndk_path = RequireValue(args, i, arg);
            ++i;
        } else if (arg == "--so") {
            request.so_path = RequireValue(args, i, arg);
            ++i;
        } else if (arg == "--stack-file") {
            request.stack_file = RequireValue(args, i, arg);
            ++i;
        } else if (arg == "--stdin") {
            request.read_stdin = true;
        } else if (arg == "--tool") {
            request.tool_preference = RequireValue(args, i, arg);
            ++i;
        } else if (arg == "--match") {
            request.match_mode = RequireValue(args, i, arg);
            ++i;
        } else if (arg == "--no-recursive-so") {
            request.recursive_so_search = false;
        } else if (arg == "--pretty") {
            request.pretty_json = true;
        } else if (arg == "--json") {
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    return request;
}

ScanRequest ParseScan(const std::vector<std::string>& args) {
    ScanRequest request;
    for (const std::string& arg : args) {
        if (arg == "--pretty") {
            request.pretty_json = true;
        } else if (arg == "--json") {
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return request;
}

ValidateRequest ParseValidate(const std::vector<std::string>& args) {
    ValidateRequest request;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--ndk") {
            request.ndk_path = RequireValue(args, i, arg);
            ++i;
        } else if (arg == "--so") {
            request.so_path = RequireValue(args, i, arg);
            ++i;
        } else if (arg == "--pretty") {
            request.pretty_json = true;
        } else if (arg == "--json") {
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    return request;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            PrintUsage();
            return 1;
        }

        const std::string command = argv[1];
        const std::vector<std::string> args(argv + 2, argv + argc);

        if (command == "--help" || command == "-h" || command == "help") {
            PrintUsage();
            return 0;
        }

        if (command == "restore") {
            const RestoreRequest request = ParseRestore(args);
            const auto result = ndktrace::RunRestore(request);
            std::cout << ndktrace::ToJson(result, request.pretty_json) << std::endl;
            return result.ok ? 0 : 1;
        }

        if (command == "scan-ndk") {
            const ScanRequest request = ParseScan(args);
            const auto result = ndktrace::RunScan(request);
            std::cout << ndktrace::ToJson(result, request.pretty_json) << std::endl;
            return result.ok ? 0 : 1;
        }

        if (command == "validate") {
            const ValidateRequest request = ParseValidate(args);
            const auto result = ndktrace::RunValidate(request);
            std::cout << ndktrace::ToJson(result, request.pretty_json) << std::endl;
            return result.ok ? 0 : 1;
        }

        PrintUsage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return 2;
    }
}

