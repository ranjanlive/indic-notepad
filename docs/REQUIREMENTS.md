# Indic NotePad · delivery scope

Planning date: 26 October 2026. First release: **0.1.0-beta.1**, Windows x64.
The supplied engineering plan and visual mockups are direction, not a claim that
every proposed 1.x feature or acceptance target has already shipped. The owner
approved MIT and `ranjanlive/indic-notepad` for the public source repository.

## Purpose

A native, offline text editor for everyday notes, Markdown, configuration and
source files. Keep useful editing close at hand without a browser runtime,
account, upload service, background daemon, interpreter or language server.

## Implemented beta

- Multi-document tabs, close/reorder, duplicate canonical path/file-identity handling.
- Unicode file dialogs, drag/drop queue, new/open/save/save as/save all, reload.
- Scintilla editing, undo/redo, line numbers, wrap, zoom, indentation and whitespace.
- Extension-based syntax for C-family, Python, JSON, Markdown, HTML/XML, CSS,
  Bash, PowerShell, INI-like properties and YAML; plain fallback.
- Case/whole-word/PCRE2 regex search, next/previous wrapping, literal replacement,
  confirmed replace all with a single undo transaction; background search and cancellation.
- UTF-8 with/without BOM and BOM-marked UTF-16 LE/BE. Strict decoding. Explicit
  save encoding and EOL conversion. Preserve untouched BOM/EOL bytes.
- Atomic local replacement, conflict detection, read-only/hard-link safeguards.
- Bounded recovery snapshots, restoration consent, recent paths/clear, clean-tab
  restore setting and profile isolation. Original documents are never timer-saved.
- Dark/light/system/high-contrast tokens, native title bar, keyboard shortcuts,
  font picker, focus mode, command palette and labeled native menus/controls.
- Optional lazy folder explorer and native Markdown reading/properties pane.
  Panes resize and remember widths; narrow windows hide optional panes.
- 32 MiB editing limit, at most 4 MiB read-only prefix for larger files, 128 MiB
  total text and 50 tabs. Bounded recovery and conservative undo budgets.
- Portable ZIP with portable profile marker, per-user installer/uninstaller,
  MIT source, offline help, dependency notices, SHA-256 and manual releases.

## Intentionally deferred

Full virtual/indexed large-file navigation or whole-large-file search; split
editing; folder content search; complete CommonMark/HTML rendering; printing,
diff, user-defined keybindings, extensions, auto-updates, cloud/Git/AI integrations,
Linux/macOS/ARM builds, file association or shell-context registration.

There is no fake terminal, Git status, marketplace or disabled button claiming
these features. The native reader only formats headings, lists and code blocks.
Regex replacements are literal, not capture-substitution expressions.

## Acceptance and limits

Actual Windows release build, core corruption/conflict/round-trip tests, visible
Scintilla integration, basic user-input smoke, release packaging and website
validation must pass for a public beta. No claim of stable v1.0, clean-machine
Windows 10/11 coverage, exhaustive screen-reader/RTL/IME compatibility, fuzz
certification, arbitrary filesystem atomicity or all original performance targets.
See verification records and HELP.md for the observed behavior and remaining work.
