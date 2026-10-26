# Indic NotePad

**Your words, in focus.** A native, offline Windows text editor from IndicStudio.
C++20 + Win32 + Scintilla. MIT open source. First release: **0.1.0-beta.1**.

Multi-tab notes and code, strict Unicode files, syntax highlighting, bounded
find/replace, dark/light/system themes, recovery, a command palette, optional
folder explorer and native Markdown reading pane. No embedded browser, account,
telemetry, upload service or background daemon.

- [Product page](https://indicstudio.com/tools/indic-notepad/)
- [Downloads and checksums](https://github.com/ranjanlive/indic-notepad/releases)
- [Offline help and shortcuts](docs/HELP.md)
- [Build, test and release](docs/BUILD_AND_RELEASE.md)
- [Scope and deferred features](docs/REQUIREMENTS.md)
- [Engineering and safety model](docs/PLAN.md)
- [Measured verification](docs/verification/)

Windows 10/11 x64 distribution target; actual host/CI coverage is recorded rather
than assumed. Portable ZIP and per-user installer. **Unsigned beta**, not a
stable v1.0. No Linux/macOS/ARM release is claimed.

```powershell
git clone https://github.com/ranjanlive/indic-notepad.git
cd indic-notepad
python -m pip install -r scripts/requirements.txt
python scripts/build.py
```

Build with installed MSVC C++ tools, Windows SDK, CMake/Ninja and Python. Official
dependencies are fetched by hash. Runtime is native Windows only. Use the complete
repository: source_code, assets, docs and public release records belong together.

Editable documents are limited to 32 MiB. Larger files offer a clearly labeled
first-4-MiB read-only preview, not a full large-file viewer. Regex replacements
are literal. Full split editing, folder search, printing, diff and plugins are
future work. See help before relying on recovery or editing important documents.

The source is MIT; Scintilla/Lexilla and PCRE2 retain their licenses in distributed
notices. IndicStudio marks do not imply endorsement of modified distributions.
