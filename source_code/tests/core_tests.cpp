#include "core/core.h"
#include <functional>
#include <iostream>
using namespace np;
int main() {
    int count = 0;
    fs::path dir = fs::temp_directory_path() /
                   (L"indic-notepad-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(dir);
        auto check = [&](bool ok, const char* name) {
            if (!ok)
                throw Error(name);
            ++count;
            std::cout << "PASS " << name << "\n";
        };
        auto rejects = [&](std::function<void()> fn, const char* name) {
            bool rejected = false;
            try {
                fn();
            } catch (const Error&) {
                rejected = true;
            }
            check(rejected, name);
        };
        std::string unicode = "Hello नमस्ते दुनिया 👩🏽‍💻 العربية עברית "
                              "é\r\nNext\nLast\r";
        for (auto enc : {Encoding::utf8, Encoding::utf8bom, Encoding::utf16le, Encoding::utf16be}) {
            auto bytes = encode(unicode, enc);
            auto d = decode(bytes);
            check(d.text == unicode && d.encoding == enc && encode(d.text, d.encoding) == bytes,
                  "Unicode-BOM-byte-round-trip");
        }
        check(detect_eol(unicode) == Eol::mixed, "mixed-EOL-detection");
        check(convert_eol("a\r\nb\rc\n", Eol::lf) == "a\nb\nc\n", "mixed-EOL-explicit-conversion");
        check(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256-known-vector");
        rejects([&] { decode(std::string("\xff", 1)); }, "invalid-UTF8-refused");
        rejects([&] { decode(std::string("a\0b", 3)); }, "binary-NUL-refused");
        rejects([&] { decode(std::string("\xff\xfe\x00\xd8", 4)); }, "unpaired-surrogate-refused");
        rejects([&] { decode(std::string("\xfe\xff\x00", 3)); }, "truncated-UTF16-refused");
        auto p = dir / L"日本語-नोट-😀.txt";
        auto stamp = save(p, unicode, Encoding::utf16be, std::nullopt);
        check(load(p).text == unicode, "Unicode-Windows-filename");
        auto original = read_bytes(p, edit_limit);
        rejects([&] { save(p, "destroy", Encoding::utf8, std::nullopt); },
                "existing-save-as-needs-confirmation");
        check(read_bytes(p, edit_limit) == original, "rejected-save-preserves-original");
        save(p, "external", Encoding::utf8, stamp);
        rejects([&] { save(p, "stale", Encoding::utf8, stamp); }, "external-modification-conflict");
        check(read_bytes(p, 100) == "external", "conflict-preserves-external-content");
        auto current = file_stamp(p);
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_READONLY);
        rejects([&] { save(p, "bad", Encoding::utf8, current); }, "read-only-refused");
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        auto link = dir / L"hardlink.txt";
        if (CreateHardLinkW(link.c_str(), p.c_str(), nullptr)) {
            rejects([&] { save(p, "bad", Encoding::utf8, current); }, "hard-link-save-refused");
            DeleteFileW(link.c_str());
        }
        rejects([&] { save(dir / L"missing" / L"file.txt", "new", Encoding::utf8, std::nullopt); },
                "failed-write-refused");
        check(read_bytes(p, 100) == "external", "failure-does-not-damage-other-files");
        std::vector<Snapshot> docs;
        Snapshot recovery;
        recovery.path = p.wstring();
        recovery.title = L"Recovery";
        recovery.text = unicode;
        recovery.encoding = Encoding::utf16be;
        recovery.eol = Eol::mixed;
        recovery.dirty = true;
        recovery.cursor = 5;
        recovery.stamp = current;
        docs.push_back(recovery);
        save_session(dir / L"recovery.dat", docs);
        auto restored = read_session(dir / L"recovery.dat");
        check(restored.size() == 1 && restored[0].text == unicode && restored[0].stamp == current &&
                  restored[0].dirty,
              "recovery-integrity-round-trip");
        auto corrupt = read_bytes(dir / L"recovery.dat", session_limit);
        corrupt[20] ^= 1;
        write_atomic(dir / L"corrupt.dat", corrupt);
        rejects([&] { read_session(dir / L"corrupt.dat"); },
                "corrupt-recovery-refused-and-retained");
        check(fs::exists(dir / L"corrupt.dat"), "corrupt-recovery-preserved");
        check(np::search("one ONE stone one", "one", {false, true, false}).size() == 3,
              "case-insensitive-whole-word");
        check(np::search("नमस्ते दुनिया नमस्ते", "नमस्ते", {false, true, false}).size() == 2,
              "Indic-search");
        auto matches = np::search("a1 b22 c333", "[a-z]\\d+", {true, false, true});
        check(matches.size() == 3, "bounded-regex");
        check(replace_matches("a1 b22 c333", matches, "$1") == "$1 $1 $1",
              "literal-regex-replacement");
        rejects([&] { np::search("abc", "[", {false, false, true}); }, "invalid-pattern-refused");
        std::atomic_bool cancel{true};
        rejects([&] { np::search("abc", "a", {}, &cancel); }, "search-cancellation");
        rejects(
            [&] {
                np::search(std::string(100000, 'a') + "!", "(*NO_START_OPT)(a+)+$",
                           {true, false, true});
            },
            "catastrophic-regex-bounded");
        check(np::search("a😀b", "(?=.)", {true, false, true}).size() == 3,
              "zero-length-Unicode-progress");
        // Sparse file exercises the large-file guard without allocating a giant buffer.
        auto big = dir / L"large.log";
        {
            Handle h(CreateFileW(big.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr));
            std::string prefix(size_t(preview_limit), 'x');
            DWORD written{};
            WriteFile(h.h, prefix.data(), DWORD(prefix.size()), &written, nullptr);
            LARGE_INTEGER size{};
            size.QuadPart = 1024ll * 1024 * 1024;
            SetFilePointerEx(h.h, size, nullptr, FILE_BEGIN);
            SetEndOfFile(h.h);
        }
        rejects([&] { load(big); }, "1GiB-edit-refused");
        auto peek = load(big, true);
        check(peek.truncated && peek.read_only && peek.text.size() == preview_limit &&
                  peek.stamp.size == 1024ull * 1024 * 1024,
              "1GiB-bounded-read-only-prefix");
        // Decode hostile/truncated inputs; deterministic fuzz regression corpus.
        uint32_t rng = 0x12345678;
        for (int n = 0; n < 1000; ++n) {
            std::string bytes;
            for (int k = 0; k < n % 97; ++k) {
                rng = rng * 1664525 + 1013904223;
                bytes += char(rng >> 24);
            }
            try {
                auto d = decode(bytes);
                check(encode(d.text, d.encoding) == bytes, "fuzz-valid-lossless-round-trip");
            } catch (const Error&) {
            }
        }
        std::cout << "Completed " << count << " assertions.\n";
        fs::remove_all(dir);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << "; fixtures retained at " << utf8(dir.wstring())
                  << "\n";
        return 1;
    }
}
