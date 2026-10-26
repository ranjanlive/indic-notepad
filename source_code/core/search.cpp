#include "core/core.h"
#include <chrono>
#include <memory>
#include <pcre2.h>
namespace np {
namespace {
struct Deadline {
    const std::atomic_bool* cancel;
    std::chrono::steady_clock::time_point end;
};
int callout(pcre2_callout_block*, void* p) {
    auto& d = *static_cast<Deadline*>(p);
    return (d.cancel && d.cancel->load()) || std::chrono::steady_clock::now() > d.end
               ? PCRE2_ERROR_CALLOUT
               : 0;
}
} // namespace
std::vector<Match> search(std::string_view text, std::string_view pattern, SearchOptions o,
                          const std::atomic_bool* cancel) {
    if (pattern.empty())
        throw Error("Enter text to find.");
    if (pattern.size() > 4096)
        throw Error("Search patterns are limited to 4096 bytes.");
    if (text.size() > edit_limit)
        throw Error("Search is limited to the loaded 32 MiB buffer.");
    if (!valid_utf8(pattern) || !valid_utf8(text))
        throw Error("Search requires valid UTF-8.");
    std::string regex;
    if (o.regex)
        regex = pattern;
    else
        for (char c : pattern) {
            if (std::string_view("\\.^$|()[]{}*+?").find(c) != std::string_view::npos)
                regex += '\\';
            regex += c;
        }
    if (o.whole_word)
        regex = "(?<![\\p{L}\\p{N}_])(?:" + regex + ")(?![\\p{L}\\p{N}_])";
    uint32_t flags =
        PCRE2_UTF | PCRE2_UCP | PCRE2_MULTILINE | PCRE2_AUTO_CALLOUT | PCRE2_NEVER_BACKSLASH_C;
    if (!o.match_case)
        flags |= PCRE2_CASELESS;
    int error{};
    PCRE2_SIZE offset{};
    auto code = std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)>(
        pcre2_compile(reinterpret_cast<PCRE2_SPTR>(regex.data()), regex.size(), flags, &error,
                      &offset, nullptr),
        pcre2_code_free);
    if (!code) {
        PCRE2_UCHAR message[256]{};
        pcre2_get_error_message(error, message, 256);
        throw Error("Invalid pattern at byte " + std::to_string(offset) + ": " +
                    reinterpret_cast<const char*>(message));
    }
    auto data = std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>(
        pcre2_match_data_create_from_pattern(code.get(), nullptr), pcre2_match_data_free);
    auto context = std::unique_ptr<pcre2_match_context, decltype(&pcre2_match_context_free)>(
        pcre2_match_context_create(nullptr), pcre2_match_context_free);
    if (!data || !context)
        throw Error("Search allocation failed.");
    pcre2_set_match_limit(context.get(), 200000);
    pcre2_set_depth_limit(context.get(), 256);
    pcre2_set_heap_limit(context.get(), 8192);
    Deadline deadline{cancel, std::chrono::steady_clock::now() + std::chrono::milliseconds(1500)};
    pcre2_set_callout(context.get(), callout, &deadline);
    std::vector<Match> matches;
    size_t at = 0;
    while (at <= text.size()) {
        if ((cancel && cancel->load()) || std::chrono::steady_clock::now() > deadline.end)
            throw Error("Search cancelled or time limit exceeded. No replacements were applied.");
        int result = pcre2_match(code.get(), reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(),
                                 at, 0, data.get(), context.get());
        if (result == PCRE2_ERROR_NOMATCH)
            break;
        if (result < 0)
            throw Error("Pattern exceeded a search resource limit or was cancelled. No "
                        "replacements were applied.");
        auto v = pcre2_get_ovector_pointer(data.get());
        matches.push_back({v[0], v[1]});
        if (matches.size() > 100000)
            throw Error("More than 100,000 matches. Narrow the search.");
        at = v[1];
        if (v[0] == v[1]) {
            if (at == text.size())
                break;
            ++at;
            while (at < text.size() && (uint8_t(text[at]) & 0xc0) == 0x80)
                ++at;
        }
    }
    return matches;
}
std::string replace_matches(std::string_view text, const std::vector<Match>& matches,
                            std::string_view replacement) {
    if (!valid_utf8(replacement) || replacement.find('\0') != std::string_view::npos)
        throw Error("Replacement is not valid text.");
    uint64_t length = text.size();
    size_t last = 0;
    for (auto m : matches) {
        if (m.start < last || m.end < m.start || m.end > text.size())
            throw Error("Invalid or stale replacement range.");
        length -= m.end - m.start;
        length += replacement.size();
        if (length > edit_limit)
            throw Error("Replacement would exceed the document size limit.");
        last = m.end;
    }
    std::string out;
    out.reserve(size_t(length));
    last = 0;
    for (auto m : matches) {
        out.append(text.substr(last, m.start - last));
        out += replacement;
        last = m.end;
    }
    out.append(text.substr(last));
    return out;
}
} // namespace np
