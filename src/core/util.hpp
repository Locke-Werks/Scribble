#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace scribble {

namespace fs = std::filesystem;

std::string trim(std::string_view s);
std::string to_lower(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool starts_with(std::string_view s, std::string_view prefix);
std::vector<std::string> split(std::string_view s, char delim);
std::string join(const std::vector<std::string> &parts, std::string_view sep);
std::string replace_all(std::string s, std::string_view from, std::string_view to);

/// "01:23:45,678" for SubRip, "01:23:45.678" for WebVTT and everything else.
std::string format_timestamp(double seconds, bool comma_millis);
/// "1h 23m" style, for durations shown to a person.
std::string format_duration(double seconds);

/// Strips characters Windows rejects in a filename, so an output path derived
/// from a title can never fail to open.
std::string sanitise_filename(std::string_view name);

std::string escape_json(std::string_view s);
/// Escapes the characters that would otherwise be read as Markdown syntax.
std::string escape_markdown(std::string_view s);

/// Reads a whole file. Returns false when it cannot be opened.
bool read_file(const fs::path &path, std::string *out);
/// Writes via a temporary in the same directory then renames, so a crash
/// mid-write cannot leave a half-written transcript in place of a good one.
bool write_file_atomic(const fs::path &path, std::string_view data, std::string *error);

/// UTF-8 to the wide strings the Win32 and MSVC file APIs want.
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view utf16);

}  // namespace scribble
