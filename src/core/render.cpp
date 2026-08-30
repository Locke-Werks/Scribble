#include "render.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "util.hpp"

namespace scribe {
namespace {

bool same_speaker(const Segment &a, const Segment &b) {
    if (a.global_id >= 0 || b.global_id >= 0) {
        return a.global_id == b.global_id;
    }
    return a.local_label == b.local_label;
}

std::string srt(const std::vector<Segment> &segments, const RenderContext &ctx) {
    std::ostringstream out;
    int n = 1;
    for (const auto &seg : segments) {
        if (seg.text.empty()) {
            continue;
        }
        out << n++ << "\n"
            << format_timestamp(seg.start, true) << " --> " << format_timestamp(seg.end, true)
            << "\n";
        std::string label = ctx.label_for(seg);
        if (!label.empty()) {
            out << "[" << label << "] ";
        }
        out << seg.text << "\n\n";
    }
    return out.str();
}

std::string vtt(const std::vector<Segment> &segments, const RenderContext &ctx) {
    std::ostringstream out;
    out << "WEBVTT\n\n";
    for (const auto &seg : segments) {
        if (seg.text.empty()) {
            continue;
        }
        out << format_timestamp(seg.start, false) << " --> " << format_timestamp(seg.end, false)
            << "\n";
        std::string label = ctx.label_for(seg);
        if (!label.empty()) {
            // The voice span is what a player uses to style per speaker, so it
            // carries the name rather than prefixing it into the caption text.
            out << "<v " << label << ">" << seg.text << "</v>\n\n";
        } else {
            out << seg.text << "\n\n";
        }
    }
    return out.str();
}

std::string txt(const std::vector<Segment> &segments, const RenderContext &ctx) {
    std::ostringstream out;
    for (const auto &seg : segments) {
        if (seg.text.empty()) {
            continue;
        }
        std::string label = ctx.label_for(seg);
        if (!label.empty()) {
            out << label << ": ";
        }
        out << seg.text << "\n";
    }
    return out.str();
}

std::string tsv(const std::vector<Segment> &segments, const RenderContext &ctx) {
    std::ostringstream out;
    out << "start\tend\tspeaker\ttext\n";
    for (const auto &seg : segments) {
        if (seg.text.empty()) {
            continue;
        }
        std::string text = replace_all(replace_all(seg.text, "\t", " "), "\n", " ");
        out << seg.start << "\t" << seg.end << "\t" << ctx.label_for(seg) << "\t" << text
            << "\n";
    }
    return out.str();
}

std::string markdown(const std::vector<Segment> &segments, const RenderContext &ctx) {
    std::ostringstream out;
    out << "# " << ctx.source_name << "\n\n";
    if (ctx.track_count > 1) {
        out << "Track " << (ctx.track + 1) << " of " << ctx.track_count << "  \n";
    }
    out << "Duration: " << format_duration(ctx.duration) << "  \n";
    if (!ctx.language.empty()) {
        out << "Language: " << ctx.language << "  \n";
    }
    if (!ctx.model.empty()) {
        out << "Model: " << ctx.model << "  \n";
    }
    out << "\n---\n\n";

    // Consecutive lines from one speaker are folded into a single block. A
    // transcript that repeats the name on every utterance is unreadable at
    // the length these files run to.
    size_t i = 0;
    while (i < segments.size()) {
        if (segments[i].text.empty()) {
            ++i;
            continue;
        }
        size_t j = i;
        std::string body;
        while (j < segments.size() && same_speaker(segments[i], segments[j])) {
            if (!segments[j].text.empty()) {
                if (!body.empty()) {
                    body += " ";
                }
                body += segments[j].text;
            }
            ++j;
        }

        std::string label = ctx.label_for(segments[i]);
        out << "**" << (label.empty() ? std::string("Unattributed") : label) << "** "
            << "`" << format_timestamp(segments[i].start, false) << "`\n\n"
            << body << "\n\n";
        i = j;
    }
    return out.str();
}

std::string json(const std::vector<Segment> &segments, const RenderContext &ctx) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"source\": \"" << escape_json(ctx.source_path) << "\",\n";
    out << "  \"track\": " << ctx.track << ",\n";
    out << "  \"track_count\": " << ctx.track_count << ",\n";
    out << "  \"duration\": " << ctx.duration << ",\n";
    out << "  \"language\": \"" << escape_json(ctx.language) << "\",\n";
    out << "  \"model\": \"" << escape_json(ctx.model) << "\",\n";
    out << "  \"segments\": [\n";

    bool first = true;
    for (const auto &seg : segments) {
        if (seg.text.empty()) {
            continue;
        }
        if (!first) {
            out << ",\n";
        }
        first = false;

        out << "    {\n";
        out << "      \"start\": " << seg.start << ",\n";
        out << "      \"end\": " << seg.end << ",\n";
        out << "      \"speaker\": \"" << escape_json(ctx.label_for(seg)) << "\",\n";
        out << "      \"speaker_id\": " << seg.global_id << ",\n";
        out << "      \"local_label\": \"" << escape_json(seg.local_label) << "\",\n";
        out << "      \"avg_logprob\": " << seg.avg_logprob << ",\n";
        out << "      \"no_speech\": " << seg.no_speech << ",\n";
        out << "      \"text\": \"" << escape_json(seg.text) << "\"";

        if (!seg.words.empty()) {
            out << ",\n      \"words\": [\n";
            for (size_t w = 0; w < seg.words.size(); ++w) {
                const auto &word = seg.words[w];
                out << "        {\"start\": " << word.start << ", \"end\": " << word.end
                    << ", \"probability\": " << word.probability << ", \"text\": \""
                    << escape_json(word.text) << "\"}";
                if (w + 1 < seg.words.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "      ]";
        }
        out << "\n    }";
    }

    out << "\n  ]\n}\n";
    return out.str();
}

}  // namespace

std::string RenderContext::label_for(const Segment &seg) const {
    if (speaker_name) {
        return speaker_name(seg.global_id, seg.local_label);
    }
    if (seg.global_id >= 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "SPEAKER_%04lld", static_cast<long long>(seg.global_id));
        return buf;
    }
    return seg.local_label;
}

bool is_supported_format(const std::string &format) {
    return iequals(format, "srt") || iequals(format, "vtt") || iequals(format, "md") ||
           iequals(format, "json") || iequals(format, "txt") || iequals(format, "tsv");
}

std::string render(const std::string &format, const std::vector<Segment> &segments,
                   const RenderContext &ctx) {
    if (iequals(format, "srt"))  return srt(segments, ctx);
    if (iequals(format, "vtt"))  return vtt(segments, ctx);
    if (iequals(format, "md"))   return markdown(segments, ctx);
    if (iequals(format, "json")) return json(segments, ctx);
    if (iequals(format, "txt"))  return txt(segments, ctx);
    if (iequals(format, "tsv"))  return tsv(segments, ctx);
    return {};
}

bool write_transcript(const std::string &format, const std::vector<Segment> &segments,
                      const RenderContext &ctx, const fs::path &path, std::string *error) {
    if (!is_supported_format(format)) {
        if (error) {
            *error = "unsupported output format: " + format;
        }
        return false;
    }
    return write_file_atomic(path, render(format, segments, ctx), error);
}

fs::path output_path(const fs::path &out_dir, const fs::path &source, int track,
                     int track_count, const std::string &format, bool mirror_tree,
                     const fs::path &mirror_root) {
    fs::path relative;
    if (mirror_tree && !mirror_root.empty()) {
        std::error_code ec;
        relative = fs::relative(source.parent_path(), mirror_root, ec);
        if (ec || relative.empty() || relative.native().rfind(L"..", 0) == 0) {
            relative.clear();
        }
    }

    std::string stem = sanitise_filename(source.stem().string());
    if (track_count > 1) {
        stem += ".track" + std::to_string(track + 1);
    }

    fs::path dir = out_dir;
    if (!relative.empty() && relative != ".") {
        dir /= relative;
    }
    return dir / (stem + "." + to_lower(format));
}

fs::path common_root(const std::vector<fs::path> &paths) {
    if (paths.empty()) {
        return {};
    }

    fs::path root = paths.front().parent_path();
    for (const auto &p : paths) {
        fs::path dir = p.parent_path();
        auto a = root.begin();
        auto b = dir.begin();
        fs::path shared;
        while (a != root.end() && b != dir.end() && *a == *b) {
            shared /= *a;
            ++a;
            ++b;
        }
        root = shared;
        if (root.empty()) {
            break;
        }
    }
    return root;
}

}  // namespace scribe
