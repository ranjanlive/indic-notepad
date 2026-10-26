# Contributing

Read docs/REQUIREMENTS.md, PLAN.md, HELP.md and BUILD_AND_RELEASE.md first.
Keep the app native, offline and bounded. Never add hidden uploads, persistent
services, auto-execution of file content or destructive default associations.

Use C++20 with RAII and the checked core APIs. UI objects stay on the UI thread;
blocking IO/search uses owned workers and version-checked results. Preserve
document-reference and view-selection lifetimes when changing tab handling.
One meaningful regression should accompany each reliability bug fix. Use the
included .clang-format; do not reformat upstream dependency sources.

Run core tests, the visible native integration suite and affected UI checks.
Report performance with method/hardware/sample count; targets are not promises.
Submit small focused changes. Never include private documents, test profiles,
signing keys, local environment files, binaries, compiler caches or credentials.
Release snapshots and tags are immutable; corrections need another version.
