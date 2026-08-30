#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace scribble {

std::string trim(std::string_view s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto begin = std::find_if(s.begin(), s.end(), not_space);
    auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> split(std::string_view s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::string join(const std::vector<std::string> &parts, std::string_view sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) {
            out.append(sep);
        }
        out.append(parts[i]);
    }
    return out;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return s;
    }
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string format_timestamp(double seconds, bool comma_millis) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    auto total_ms = static_cast<long long>(seconds * 1000.0 + 0.5);
    long long ms = total_ms % 1000;
    long long total_s = total_ms / 1000;
    long long s = total_s % 60;
    long long m = (total_s / 60) % 60;
    long long h = total_s / 3600;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld%c%03lld", h, m, s,
                  comma_millis ? ',' : '.', ms);
    return buf;
}

std::string format_duration(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    auto total = static_cast<long long>(seconds + 0.5);
    long long h = total / 3600;
    long long m = (total % 3600) / 60;
    long long s = total % 60;

    char buf[48];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%lldh %02lldm", h, m);
    } else if (m > 0) {
        std::snprintf(buf, sizeof(buf), "%lldm %02llds", m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%llds", s);
    }
    return buf;
}

std::string sanitise_filename(std::string_view name) {
    static constexpr std::string_view illegal = "<>:\"/\\|?*";
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || illegal.find(c) != std::string_view::npos) {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.empty()) {
        out = "untitled";
    }
    return out;
}

std::string escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

std::string escape_markdown(std::string_view s) {
    static constexpr std::string_view specials = "\\`*_{}[]()#+-.!|";
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (specials.find(c) != std::string_view::npos) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

bool read_file(const fs::path &path, std::string *out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

bool write_file_atomic(const fs::path &path, std::string_view data, std::string *error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    static std::mt19937 rng{std::random_device{}()};
    fs::path tmp = path;
    tmp += ".tmp" + std::to_string(rng() & 0xffffff);

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) {
                *error = "cannot open " + tmp.string() + " for writing";
            }
            return false;
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) {
            if (error) {
                *error = "write failed for " + tmp.string();
            }
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }

    fs::remove(path, ec);
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) {
            *error = "rename failed: " + ec.message();
        }
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

#ifdef _WIN32

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                 nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                        len);
    return out;
}

std::string narrow(std::wstring_view utf16) {
    if (utf16.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                                 nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), out.data(),
                        len, nullptr, nullptr);
    return out;
}

#else

std::wstring widen(std::string_view utf8) { return std::wstring(utf8.begin(), utf8.end()); }
std::string narrow(std::wstring_view utf16) { return std::string(utf16.begin(), utf16.end()); }

#endif

}  // namespace scribble
