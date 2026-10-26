# Indic NotePad · 0.1.0-beta.1

A native, offline Windows text editor from IndicStudio. MIT open source.
Windows 10/11 x64. No embedded browser, account, analytics or background service.

## Everyday use

Ctrl+N New · Ctrl+O Open · Ctrl+S Save · Ctrl+Shift+S Save As · Ctrl+Shift+Alt+S
Save All · Ctrl+W Close · Ctrl+Tab Next tab · Ctrl+Shift+Tab Previous tab.
Drag text files onto the window to open them. The same canonical path opens
once. Move tabs by dragging their labels or use Window → Move tab left/right.

Ctrl+Z Undo · Ctrl+Y Redo · Ctrl+F Find · Ctrl+H Replace · F3 Next · Shift+F3
Previous · Ctrl+G Go to line · Ctrl+Plus/Minus Zoom · Ctrl+0 Reset.
F11 Focus mode · Escape leaves focus mode/closes search · Ctrl+Shift+P commands.

View controls the sidebar, preview, wrap, line numbers, whitespace and theme.
Dark/light/system and Windows high contrast are supported; native Windows title
bar preserves snap, resizing, Alt+Space and system accessibility behavior.
The optional Markdown reading pane renders text locally, never HTML/scripts or
remote images. It is a simplified reader, not a full CommonMark renderer.

## File safety

UTF-8 and BOM-marked UTF-16 LE/BE open automatically. Malformed Unicode,
NUL-containing/binary files and unsupported legacy encodings are refused.
Save encoding is selected in File → Encoding. Existing mixed EOLs are preserved;
new inserted lines use the document's selected insertion EOL. File → Line endings
explicitly converts all endings in a single undo action. Undoing text does not
undo separately selected encoding metadata.

Files up to 32 MiB are editable. Larger files offer a **limited read-only prefix
preview of at most 4 MiB**, including for 1 GiB files; this is not a virtual full
large-file viewer. It can never be saved over the complete original. Large full
file search, split view, diff and printing are future work.
Word wrap is disabled for buffers over 1 MiB to avoid expensive giant-line layout.
Open text is capped at 128 MiB total. Undo history resets when its conservative
budget reaches 16 MiB for one tab or 64 MiB across tabs; a status notice explains
the reset and current text remains dirty until successfully saved.

Save writes/flushed a new temporary sibling, then uses Windows ReplaceFile or
a non-overwriting rename. Files changed externally cause a conflict error;
reload or Save As a different path. Read-only files and files with multiple hard
links require Save As. Saving checks identity, size, time and SHA-256. This does
not replace a backup or guarantee atomic behavior on every network/cloud provider.
Use regular local files for the beta. Permission/sharing errors preserve dirty
buffers. Original reparse targets are resolved on open; no link itself is edited.

## Find and replace

Case, Unicode-aware whole word and PCRE2 regex modes. Wraps around the document.
Replace uses **literal replacement text**, including in regex mode (no $1 expansion).
Replace All confirms the count and forms one undo transaction. Work runs off the
UI thread with cancellation, 1.5 second deadline, match/depth/heap limits and a
100,000-result cap. Invalid/expensive patterns apply no partial replacements.

## Recovery and privacy

Dirty buffers are snapshotted after a pause, separately from original files.
The recovery file has an integrity checksum and 64 MiB total budget, 50 tabs max.
Recovery is not encryption: it may contain sensitive text and file paths.
Settings, recent paths and recovery normally live in
`%LOCALAPPDATA%\IndicStudio\IndicNotePad`. `--portable` uses a sibling `data` folder;
`--data-dir <folder>` selects a separate local profile. Do not share a profile
between running instances. A profile lock prevents concurrent state writers.
Recovery prompts offer restore, keep for later, or discard. Cancel leaves the
stored file untouched. Recovery writes stop when recovery is disabled; disabling
does not delete old snapshots without a separate confirmed clear action.
Recent files can be cleared. Settings can disable history/session/recovery.
No documents are automatically saved back to originals by a timer.

## Command line

`indic-notepad.exe [--portable | --data-dir <folder>] [--readonly] [--] [files...]`
`--help` and `--version` show native information dialogs. Unknown flags are rejected.
Manual updates only; no updater/service/file-association hijacking.
The portable ZIP includes a `portable.mode` marker, so starting its executable
normally uses the adjacent data folder. The installer omits that marker.
An explicit `--data-dir` takes precedence over the marker. For benchmarking only,
`--benchmark-ms 8500 --data-dir <new-folder>` opens a synthetic 10 KiB note and
exits normally after the specified 1000–30000 ms. It requires a new profile and
accepts no document paths, so it cannot close a normal working session.

## Beta boundaries

Unsigned beta; no security-warning bypass instructions. Full Windows 10/11 clean
machine, assistive technology, complex-script shaping and exhaustive failure
tests require additional hardware/manual review. Consult release verification
before treating planning performance targets as measured promises.
Help → About shows source/support information. Report reproducible bugs without
sending private documents; source: https://github.com/ranjanlive/indic-notepad
