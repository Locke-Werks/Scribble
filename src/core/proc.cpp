#include "proc.hpp"

#include <cstdio>
#include <cstdlib>

#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace scribble {

std::string quote_arg(const std::string &arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    // Backslashes are only special immediately before a quote, which is why
    // the run length is doubled there and nowhere else. Getting this wrong
    // breaks every path containing a space.
    std::string out = "\"";
    for (auto it = arg.begin();; ++it) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == '\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (*it == '"') {
            out.append(backslashes * 2 + 1, '\\');
        } else {
            out.append(backslashes, '\\');
        }
        out.push_back(*it);
    }
    out.push_back('"');
    return out;
}

bool run_process(const fs::path &exe, const std::vector<std::string> &args, std::string *output,
                 int *exit_code, std::string *error) {
    if (exe.empty()) {
        if (error) {
            *error = "executable not found";
        }
        return false;
    }

    std::string command = quote_arg(exe.string());
    for (const auto &a : args) {
        command += ' ';
        command += quote_arg(a);
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        if (error) {
            *error = "CreatePipe failed";
        }
        return false;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring wide = widen(command);
    wide.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(write_end);

    if (!ok) {
        CloseHandle(read_end);
        if (error) {
            *error = "cannot start " + exe.string();
        }
        return false;
    }

    std::string buffer;
    char chunk[4096];
    DWORD got = 0;
    while (ReadFile(read_end, chunk, sizeof(chunk), &got, nullptr) && got > 0) {
        buffer.append(chunk, got);
    }
    CloseHandle(read_end);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (output) {
        *output = std::move(buffer);
    }
    if (exit_code) {
        *exit_code = static_cast<int>(code);
    }
    return true;
#else
    command += " 2>&1";
    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        if (error) {
            *error = "cannot start " + exe.string();
        }
        return false;
    }
    std::string buffer;
    char chunk[4096];
    size_t got = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        buffer.append(chunk, got);
    }
    int code = pclose(pipe);
    if (output) {
        *output = std::move(buffer);
    }
    if (exit_code) {
        *exit_code = code;
    }
    return true;
#endif
}

fs::path find_system_tool(const std::string &name) {
    std::error_code ec;
#ifdef _WIN32
    wchar_t sysdir[MAX_PATH] = {};
    if (GetSystemDirectoryW(sysdir, MAX_PATH) > 0) {
        fs::path candidate = fs::path(sysdir) / (name + ".exe");
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    const std::string filename = name + ".exe";
    const char sep = ';';
#else
    const std::string filename = name;
    const char sep = ':';
#endif

    const char *env = std::getenv("PATH");
    if (env == nullptr) {
        return {};
    }
    for (const auto &dir : split(env, sep)) {
        if (dir.empty()) {
            continue;
        }
        fs::path candidate = fs::path(dir) / filename;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace scribble
