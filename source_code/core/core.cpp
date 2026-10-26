#include "core/core.h"
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <iomanip>
#include <limits>
#include <sstream>
namespace np {
namespace {
[[noreturn]] void fail(const char* what) {
    throw Error(std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ").");
}
uint64_t value(DWORD high, DWORD low) {
    return (uint64_t(high) << 32) | low;
}
std::string read_handle(HANDLE h, uint64_t limit) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size))
        fail("Cannot read file size");
    if (size.QuadPart < 0 || uint64_t(size.QuadPart) > limit)
        throw Error("File exceeds the allowed size.");
    std::string bytes(size_t(size.QuadPart), '\0');
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD count = 0;
        DWORD chunk = DWORD(std::min<size_t>(1024 * 1024, bytes.size() - offset));
        if (!ReadFile(h, bytes.data() + offset, chunk, &count, nullptr))
            fail("Cannot read file");
        if (!count)
            throw Error("File changed or ended while reading.");
        offset += count;
    }
    return bytes;
}
Stamp stamp_handle(HANDLE h, bool with_hash = true) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info))
        fail("Cannot inspect file identity");
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        throw Error("Choose a file, not a directory.");
    Stamp s{info.dwVolumeSerialNumber,
            value(info.nFileIndexHigh, info.nFileIndexLow),
            value(info.nFileSizeHigh, info.nFileSizeLow),
            value(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime),
            {},
            true};
    if (with_hash) {
        LARGE_INTEGER zero{};
        SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
        s.sha256 = sha256(read_handle(h, edit_limit));
    }
    return s;
}
void check_destination(const fs::path& p, HANDLE h) {
    auto attr = GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES &&
        attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))
        throw Error("Save refused: read-only, reparse-point or directory destination. Use Save As "
                    "to a regular file.");
    if (h && h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h, &info))
            fail("Cannot check destination");
        if (info.nNumberOfLinks > 1)
            throw Error("Save refused: this file has multiple hard links. Use Save As to avoid "
                        "changing link semantics.");
    }
}
fs::path temporary(const fs::path& path) {
    static std::atomic_uint64_t serial{};
    return path.parent_path() /
           (L".indic-notepad-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial) + L".tmp");
}
void write_new(const fs::path& path, std::string_view bytes) {
    Handle h(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr));
    if (!h)
        fail("Cannot create temporary file");
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD count{};
        if (!WriteFile(h.h, bytes.data() + offset,
                       DWORD(std::min<size_t>(1024 * 1024, bytes.size() - offset)), &count,
                       nullptr) ||
            !count)
            fail("Cannot write file");
        offset += count;
    }
    if (!FlushFileBuffers(h.h))
        fail("Cannot flush file");
}
void put32(std::string& b, uint32_t v) {
    for (int n = 0; n < 4; ++n)
        b.push_back(char(v >> (n * 8)));
}
void put64(std::string& b, uint64_t v) {
    for (int n = 0; n < 8; ++n)
        b.push_back(char(v >> (n * 8)));
}
void putstr(std::string& b, std::string_view s) {
    if (s.size() > session_limit)
        throw Error("Recovery field too large.");
    put32(b, uint32_t(s.size()));
    b.append(s);
}
struct Reader {
    std::string_view data;
    size_t at{};
    uint64_t number(size_t n) {
        if (at + n > data.size())
            throw Error("Truncated recovery file.");
        uint64_t v = 0;
        for (size_t k = 0; k < n; ++k)
            v |= uint64_t(uint8_t(data[at++])) << (8 * k);
        return v;
    }
    std::string str() {
        auto n = number(4);
        if (n > session_limit || n > data.size() - at)
            throw Error("Invalid recovery field.");
        std::string s(data.substr(at, size_t(n)));
        at += size_t(n);
        return s;
    }
};
} // namespace
std::string utf8(std::wstring_view s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0)
        throw Error("Invalid UTF-16 text.");
    std::string out(n, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n,
                             nullptr, nullptr))
        fail("UTF conversion failed");
    return out;
}
std::wstring wide(std::string_view s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (n <= 0)
        throw Error("Invalid UTF-8 text. Reopen using an explicitly supported encoding.");
    std::wstring out(n, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n))
        fail("UTF conversion failed");
    return out;
}
bool valid_utf8(std::string_view s) {
    return s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()),
                                            nullptr, 0) > 0;
}
size_t characters(std::string_view s) {
    return size_t(
        std::count_if(s.begin(), s.end(), [](char c) { return (uint8_t(c) & 0xc0) != 0x80; }));
}
Eol detect_eol(std::string_view s) {
    bool lf = false, crlf = false, cr = false;
    for (size_t n = 0; n < s.size(); ++n) {
        if (s[n] == '\r') {
            if (n + 1 < s.size() && s[n + 1] == '\n') {
                crlf = true;
                ++n;
            } else
                cr = true;
        } else if (s[n] == '\n')
            lf = true;
    }
    int count = int(lf) + int(crlf) + int(cr);
    return count > 1 ? Eol::mixed : crlf ? Eol::crlf : cr ? Eol::cr : lf ? Eol::lf : Eol::none;
}
std::string convert_eol(std::string_view s, Eol eol) {
    if (eol == Eol::none || eol == Eol::mixed)
        throw Error("Choose LF, CRLF or CR to convert line endings.");
    std::string out;
    out.reserve(s.size());
    for (size_t n = 0; n < s.size(); ++n) {
        char c = s[n];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && n + 1 < s.size() && s[n + 1] == '\n')
                ++n;
            if (eol == Eol::crlf)
                out += "\r\n";
            else
                out += eol == Eol::cr ? '\r' : '\n';
        } else
            out += c;
    }
    return out;
}
std::string encode(std::string_view text, Encoding enc) {
    if (!valid_utf8(text))
        throw Error("Invalid UTF-8 buffer; original file has not been changed.");
    if (text.find('\0') != std::string_view::npos)
        throw Error("NUL/binary content is not supported.");
    if (enc == Encoding::utf8)
        return std::string(text);
    if (enc == Encoding::utf8bom)
        return std::string("\xef\xbb\xbf") + std::string(text);
    auto w = wide(text);
    std::string out = enc == Encoding::utf16le ? "\xff\xfe" : "\xfe\xff";
    out.reserve(w.size() * 2 + 2);
    for (wchar_t c : w) {
        if (enc == Encoding::utf16le) {
            out += char(c & 255);
            out += char(c >> 8);
        } else {
            out += char(c >> 8);
            out += char(c & 255);
        }
    }
    return out;
}
Loaded decode(std::string_view bytes) {
    Loaded d;
    if (bytes.starts_with("\xef\xbb\xbf")) {
        d.encoding = Encoding::utf8bom;
        bytes.remove_prefix(3);
    } else if (bytes.starts_with("\xff\xfe") || bytes.starts_with("\xfe\xff")) {
        d.encoding = bytes.starts_with("\xff\xfe") ? Encoding::utf16le : Encoding::utf16be;
        bytes.remove_prefix(2);
        if (bytes.size() % 2)
            throw Error("Truncated UTF-16 file.");
        std::wstring w;
        w.reserve(bytes.size() / 2);
        for (size_t n = 0; n < bytes.size(); n += 2) {
            uint16_t a = uint8_t(bytes[n]), b = uint8_t(bytes[n + 1]);
            w += wchar_t(d.encoding == Encoding::utf16le ? a | (b << 8) : (a << 8) | b);
        }
        d.text = utf8(w);
        d.eol = detect_eol(d.text);
        if (d.text.find('\0') != std::string::npos)
            throw Error("This file contains NUL/binary content.");
        return d;
    }
    if (!valid_utf8(bytes))
        throw Error("Unsupported encoding or invalid UTF-8. Only UTF-8 and BOM-marked UTF-16 LE/BE "
                    "are opened automatically.");
    if (bytes.find('\0') != std::string_view::npos)
        throw Error("This file contains NUL/binary content.");
    d.text = bytes;
    d.eol = detect_eol(bytes);
    return d;
}
std::wstring encoding_name(Encoding e) {
    switch (e) {
    case Encoding::utf8:
        return L"UTF-8";
    case Encoding::utf8bom:
        return L"UTF-8 BOM";
    case Encoding::utf16le:
        return L"UTF-16 LE";
    case Encoding::utf16be:
        return L"UTF-16 BE";
    }
    return L"Unknown";
}
std::wstring eol_name(Eol e) {
    switch (e) {
    case Eol::crlf:
        return L"CRLF";
    case Eol::cr:
        return L"CR";
    case Eol::mixed:
        return L"Mixed · preserved";
    case Eol::none:
        return L"No EOL";
    default:
        return L"LF";
    }
}
std::string sha256(std::string_view bytes) {
    BCRYPT_ALG_HANDLE alg{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw Error("SHA-256 unavailable.");
    std::array<UCHAR, 32> digest{};
    auto status =
        BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                   ULONG(bytes.size()), digest.data(), ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status < 0)
        throw Error("SHA-256 failed.");
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (auto b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}
fs::path normalized(const fs::path& p) {
    if (p.empty())
        throw Error("An empty path is not valid.");
    auto full = fs::absolute(p).lexically_normal();
    if (fs::exists(full)) {
        Handle h(CreateFileW(full.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (h) {
            std::wstring final(32768, L'\0');
            DWORD n = GetFinalPathNameByHandleW(h.h, final.data(), DWORD(final.size()),
                                                FILE_NAME_NORMALIZED);
            if (n && n < final.size()) {
                final.resize(n);
                return final;
            }
        }
    }
    return full;
}
std::wstring path_key(const fs::path& p) {
    auto s = normalized(p).wstring();
    if (!s.empty())
        CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}
std::string read_bytes(const fs::path& p, uint64_t limit) {
    Handle h(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!h)
        fail("Cannot open file");
    return read_handle(h.h, limit);
}
Stamp file_stamp(const fs::path& p) {
    Handle h(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
            return {};
        fail("Cannot inspect file");
    }
    return stamp_handle(h.h);
}
Loaded load(const fs::path& p, bool allow_preview) {
    auto path = normalized(p);
    Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!h)
        fail("Cannot open file");
    auto stamp = stamp_handle(h.h, false);
    bool preview = stamp.size > edit_limit;
    if (preview && !allow_preview)
        throw Error("This file exceeds the 32 MiB editing limit. Open a limited read-only preview "
                    "instead.");
    std::string bytes;
    if (preview) {
        bytes.resize(size_t(preview_limit));
        DWORD count{};
        if (!ReadFile(h.h, bytes.data(), DWORD(bytes.size()), &count, nullptr))
            fail("Cannot read preview");
        bytes.resize(count);
        if (bytes.starts_with("\xff\xfe") || bytes.starts_with("\xfe\xff")) {
            if (bytes.size() % 2)
                bytes.pop_back();
            if (bytes.size() >= 4) {
                auto a = uint8_t(bytes[bytes.size() - 2]), b = uint8_t(bytes.back());
                auto c = bytes.starts_with("\xff\xfe") ? a | (b << 8) : (a << 8) | b;
                if (c >= 0xd800 && c <= 0xdbff)
                    bytes.resize(bytes.size() - 2);
            }
        } else {
            for (int n = 0; n < 3 && !valid_utf8(bytes); ++n)
                bytes.pop_back();
        }
    } else {
        bytes = read_handle(h.h, edit_limit);
        stamp.sha256 = sha256(bytes);
    }
    auto d = decode(bytes);
    d.path = path;
    d.stamp = stamp;
    auto attr = GetFileAttributesW(path.c_str());
    d.read_only = preview || (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY));
    d.truncated = preview;
    return d;
}
Stamp save(const fs::path& input, std::string_view text, Encoding enc,
           const std::optional<Stamp>& expected, bool overwrite) {
    if (text.size() > edit_limit)
        throw Error("Buffer exceeds the editing limit. Save refused.");
    auto bytes = encode(text, enc);
    if (bytes.size() > edit_limit)
        throw Error("Encoded output exceeds 32 MiB. Original preserved.");
    auto p = fs::absolute(input).lexically_normal();
    Handle original(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Stamp before;
    if (original) {
        check_destination(p, original.h);
        before = stamp_handle(original.h);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        fail("Cannot access save destination");
    if (expected && before != *expected)
        throw Error("The file changed outside Indic NotePad or was removed. Reload it or Save As a "
                    "different file.");
    if (!expected && before.exists && !overwrite)
        throw Error("Destination already exists; confirm replacement in Save As.");
    auto temp = temporary(p);
    try {
        write_new(temp, bytes);
        if (before.exists) {
            if (!ReplaceFileW(p.c_str(), temp.c_str(), nullptr, 0, nullptr, nullptr))
                fail("Cannot replace original; temporary data is recoverable");
        } else if (!MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_WRITE_THROUGH))
            fail("Cannot commit new file");
    } catch (...) {
        DeleteFileW(temp.c_str());
        throw;
    }
    return file_stamp(p);
}
void write_atomic(const fs::path& p, std::string_view bytes) {
    fs::create_directories(p.parent_path());
    auto temp = temporary(p);
    try {
        write_new(temp, bytes);
        auto attr = GetFileAttributesW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) {
            check_destination(p, INVALID_HANDLE_VALUE);
            if (!ReplaceFileW(p.c_str(), temp.c_str(), nullptr, 0, nullptr, nullptr))
                fail("Cannot replace local state");
        } else if (!MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_WRITE_THROUGH))
            fail("Cannot save local state");
    } catch (...) {
        DeleteFileW(temp.c_str());
        throw;
    }
}
void save_session(const fs::path& p, const std::vector<Snapshot>& docs) {
    if (docs.size() > 50)
        throw Error("At most 50 tabs can be stored in recovery.");
    std::string b = "INPSTATE1";
    put32(b, uint32_t(docs.size()));
    for (const auto& d : docs) {
        putstr(b, utf8(d.path));
        putstr(b, utf8(d.title));
        putstr(b, d.text);
        put32(b, uint32_t(d.encoding));
        put32(b, uint32_t(d.eol));
        put64(b, d.cursor);
        put64(b, d.anchor);
        put32(b, d.dirty ? 1 : 0);
        put64(b, d.stamp.volume);
        put64(b, d.stamp.id);
        put64(b, d.stamp.size);
        put64(b, d.stamp.modified);
        put32(b, d.stamp.exists ? 1 : 0);
        putstr(b, d.stamp.sha256);
        if (b.size() > session_limit)
            throw Error("Recovery limit reached (64 MiB). Save documents to keep them safe.");
    }
    b += sha256(b);
    write_atomic(p, b);
}
std::vector<Snapshot> read_session(const fs::path& p) {
    auto b = read_bytes(p, session_limit + 64);
    if (b.size() < 77 || !b.starts_with("INPSTATE1") ||
        sha256(std::string_view(b).substr(0, b.size() - 64)) != b.substr(b.size() - 64))
        throw Error("Recovery file is corrupt. It has been preserved for inspection.");
    Reader r{std::string_view(b).substr(9, b.size() - 9 - 64)};
    auto count = r.number(4);
    if (count > 50)
        throw Error("Invalid recovery tab count.");
    std::vector<Snapshot> docs;
    for (uint64_t n = 0; n < count; ++n) {
        Snapshot d;
        d.path = wide(r.str());
        d.title = wide(r.str());
        d.text = r.str();
        if (d.text.size() > edit_limit || !valid_utf8(d.text) ||
            d.text.find('\0') != std::string::npos)
            throw Error("Invalid recovery document.");
        auto enc = r.number(4), eol = r.number(4);
        if (enc > 3 || eol > 4)
            throw Error("Invalid recovery encoding.");
        d.encoding = Encoding(enc);
        d.eol = Eol(eol);
        d.cursor = r.number(8);
        d.anchor = r.number(8);
        d.dirty = r.number(4) != 0;
        d.stamp.volume = r.number(8);
        d.stamp.id = r.number(8);
        d.stamp.size = r.number(8);
        d.stamp.modified = r.number(8);
        d.stamp.exists = r.number(4) != 0;
        d.stamp.sha256 = r.str();
        if (d.cursor > d.text.size() || d.anchor > d.text.size())
            throw Error("Invalid recovery cursor.");
        docs.push_back(std::move(d));
    }
    if (r.at != r.data.size())
        throw Error("Unexpected recovery data.");
    return docs;
}
} // namespace np
