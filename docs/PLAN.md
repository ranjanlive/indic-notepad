# Engineering plan and implementation map

1. Native feasibility: Windows/MSVC x64, C++20, CMake/Ninja and two workers.
   Pinned SHA-256 source archives for Scintilla 5.6.7, Lexilla 5.5.4 and PCRE2 10.49.
   Static dependencies and CRT; only Windows DLLs at runtime.
2. Reliability core: strict conversion, lossless supported encoding round trips,
   BOM/EOL policy, owned Windows handles, fingerprinted replacement and bounded
   checksummed recovery. No lossy legacy-encoding guessing.
3. Shell: native title/menu/dialogs, accessible common-control tabs, custom colors
   and restrained chrome. One Scintilla view switching reference-owned documents.
   Dedicated native DirectWrite DC rendering supports Windows font fallback.
4. Productivity: bounded cancellable PCRE2 worker, version-checked results,
   command palette, native reading pane loaded only on request, lazy folder enumeration.
5. Resource controls: event-driven message loop; one background operation at a
   time; one-shot recovery/preview debounce; sequential UI/build/test helpers;
   explicit text/recovery/undo budgets and no service/emulator/JDK.
6. Verification: core tests, native integration executable, user-input smoke,
   visible warm-launch metrics with normal self-exit, installer/portable checks.
7. Release: immutable beta snapshot and source tag, public MIT repository,
   unsigned artifacts and checksums, accurate product/privacy data, real captures,
   existing static-site pipeline and verified deployment.

## Source map

- `source_code/core/core.*`: encoding, files, fingerprints, persistence and limits.
- `source_code/core/search.cpp`: PCRE2 compilation, callout deadline, cancellation,
  result cap and replacement planning.
- `source_code/editor/lexers.cpp`: small static lexer catalogue.
- `source_code/app/main.cpp`: Win32 controller, Scintilla adapter, message/command
  routing and native view helpers. No web frontend or renderer process.
- `source_code/resources/`: DPI/long-path/asInvoker manifest, version and icon.
- `source_code/tests/`: corruption, filesystem, Unicode, regex and size regressions.
- `scripts/build.py`, `run_ui_tests.py`, `benchmark.py`, `package_native.py`:
  reproducible local/CI entry points, isolated profiles and owned helper lifetimes.

## Data-safety details

Save fingerprints include volume/file ID, size, modified timestamp and SHA-256.
The original handle denies writers while the replacement is prepared. New data
is flushed to a fresh sibling before ReplaceFile (existing) or non-overwriting
MoveFileEx (new). Concurrent rename/replace races, network provider semantics,
ADS/cloud placeholders and power-loss guarantees are not exhaustively certified.
Keep independent backups and avoid treating this beta as the sole keeper of work.

Recovery files are local plaintext with an integrity checksum, not encryption or
an archival backup. A profile lock prevents concurrent state writers. Cancel at
the recovery prompt preserves old snapshots and pauses new recovery writes.
Invalid state is retained rather than silently overwritten.

Undo history is conservatively accounted from edit notifications. Resetting it
at the published budget preserves current text and its dirty state. Text-size
overflow reverses the last undoable edit and shows an explanation; large input
may still incur transient allocation before rollback. These limits are safeguards,
not a process hard-memory sandbox.

## Next releases

Prioritize independent native usability and assistive technology review, real
clean-machine installs, broader Indic shaping/IME/RTL checks, performance profiling
on additional systems, then carefully scoped split/large-file features.
