#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
namespace np {
namespace fs = std::filesystem;
constexpr uint64_t edit_limit = 32ull * 1024 * 1024;
constexpr uint64_t preview_limit = 4ull * 1024 * 1024;
constexpr uint64_t session_limit = 64ull * 1024 * 1024;
enum class Encoding : uint32_t { utf8, utf8bom, utf16le, utf16be };
enum class Eol : uint32_t { lf, crlf, cr, mixed, none };
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Handle {
    HANDLE h{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : h(value) {}
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE && h)
            CloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const {
        return h && h != INVALID_HANDLE_VALUE;
    }
};
struct Stamp {
    uint64_t volume{}, id{}, size{}, modified{};
    std::string sha256;
    bool exists{};
    bool operator==(const Stamp&) const = default;
};
struct Loaded {
    fs::path path;
    std::string text;
    Encoding encoding{Encoding::utf8};
    Eol eol{Eol::none};
    Stamp stamp;
    bool read_only{}, truncated{};
};
struct Snapshot {
    std::wstring path, title;
    std::string text;
    Encoding encoding{Encoding::utf8};
    Eol eol{Eol::lf};
    uint64_t cursor{}, anchor{};
    bool dirty{};
    Stamp stamp;
};
std::string utf8(std::wstring_view s);
std::wstring wide(std::string_view s);
bool valid_utf8(std::string_view s);
size_t characters(std::string_view s);
Eol detect_eol(std::string_view s);
std::string convert_eol(std::string_view s, Eol eol);
std::string encode(std::string_view text, Encoding encoding);
Loaded decode(std::string_view bytes);
std::wstring encoding_name(Encoding encoding);
std::wstring eol_name(Eol eol);
std::string sha256(std::string_view bytes);
fs::path normalized(const fs::path& path);
std::wstring path_key(const fs::path& path);
Loaded load(const fs::path& path, bool allow_preview = false);
Stamp file_stamp(const fs::path& path);
Stamp save(const fs::path& path, std::string_view text, Encoding encoding,
           const std::optional<Stamp>& expected, bool allow_overwrite = false);
void write_atomic(const fs::path& path, std::string_view bytes);
std::string read_bytes(const fs::path& path, uint64_t limit);
void save_session(const fs::path& path, const std::vector<Snapshot>& docs);
std::vector<Snapshot> read_session(const fs::path& path);
struct Match {
    size_t start{}, end{};
};
struct SearchOptions {
    bool match_case{}, whole_word{}, regex{};
};
std::vector<Match> search(std::string_view text, std::string_view pattern, SearchOptions options,
                          const std::atomic_bool* cancel = nullptr);
std::string replace_matches(std::string_view text, const std::vector<Match>& matches,
                            std::string_view replacement);
} // namespace np
