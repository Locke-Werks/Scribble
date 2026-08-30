#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace scribble {

namespace fs = std::filesystem;

/// Runs a child process with stdout and stderr merged into one pipe.
///
/// They share a pipe deliberately: reading two pipes from a single thread
/// deadlocks as soon as either fills. Every tool invoked here runs at a quiet
/// log level, so on success stderr is empty and on failure stdout is.
///
/// Returns false only when the process could not be started. A process that
/// ran and failed returns true with a non-zero `exit_code`.
bool run_process(const fs::path &exe, const std::vector<std::string> &args, std::string *output,
                 int *exit_code, std::string *error);

/// Windows command line quoting for a single argument.
std::string quote_arg(const std::string &arg);

/// Locates a system tool. Windows 11 ships both curl and tar in System32, which
/// is why model downloads need no HTTP or archive library linked in.
fs::path find_system_tool(const std::string &name);

}  // namespace scribble
