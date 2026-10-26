# Indic NotePad 0.1.0-beta.1

A native Windows text editor from IndicStudio. Your words, in focus.

**Windows 10/11 x64 distribution target · MIT source · Unsigned public beta.**

Download the portable ZIP to unpack and run locally, or use the per-user installer.
No WebView, browser runtime, Java, account, service or separate VC runtime install.
The ZIP includes a portable profile marker; the installer uses LocalAppData.
Check the artifact's SHA-256 against SHA256SUMS.txt and read the bundled HELP.md.

Includes tabs, undo/redo, common syntax modes, bounded regex/literal find and
replace, UTF-8/UTF-16, explicit EOL conversion, conflict-aware local replacement,
recovery snapshots, themes, focus mode, font choice, command palette, optional
folder explorer and native Markdown reader.

Editable text is limited to 32 MiB per file. Larger files offer a limited first
4 MiB read-only preview, not full virtual navigation/search. Reading pane is a
simplified renderer; regex replacement text is literal. Split editing, printing,
diff, folder search and full 1.x proposal scope are deferred.

No code-signing credentials are configured. This is not stable v1.0. Read the
published verification for actual host/CI/manual coverage and measured warm
startup, private bytes/working set and idle CPU; planning targets are not promises.
Clean-machine Windows 10/11, broad screen-reader/IME/RTL testing, cold startup and
typing/scrolling benchmarks remain additional validation work.

No automatic update/download check runs. User documents, settings and local
plaintext recovery remain under user control. Uninstall retains personal data.

Source: https://github.com/ranjanlive/indic-notepad
Product: https://indicstudio.com/tools/indic-notepad/
