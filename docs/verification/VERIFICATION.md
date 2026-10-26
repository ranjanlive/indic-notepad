# Release verification · 0.1.0-beta.1

Published Windows x64 unsigned public beta, 26 October 2026.

- Product: https://indicstudio.com/tools/indic-notepad/
- Privacy: https://indicstudio.com/tools/indic-notepad/privacy/
- MIT source: https://github.com/ranjanlive/indic-notepad
- Immutable source tag: v0.1.0-beta.1 at 36484de0ebc70d5b396584fad8c88f5e83fca777
- Release: https://github.com/ranjanlive/indic-notepad/releases/tag/v0.1.0-beta.1
- Native CI: https://github.com/ranjanlive/indic-notepad/actions/runs/40267885460
- Website deploy: https://github.com/ranjanlive/indicstudio.com/actions/runs/40268553196

## Checked

Core Unicode/BOM/EOL byte round trips; malformed/truncated/binary input; real
Unicode filenames; save conflicts/read-only/hard-link/failing-directory errors;
recovery corruption; search/regex/cancellation limits; sparse 1 GiB preview guard;
1,000 deterministic decoder fuzz inputs. Actual assertions: core-tests.txt.

19 native control checks and 9 package checks passed. Built-preview user-input
smoke covered find, typed English/Devanagari and real Save As with exact UTF-8
bytes. Final portable-package smoke covered themes, populated palette, search,
focus mode and adjacent profile. These are separate stages, not exhaustive
native dialog, assistive technology or failure-matrix coverage.

The first UI launch exposed a document reference lifetime crash; the integration
suite now exercises that path. Tab-selection preservation and empty palette
were also corrected before publication. Initial CI compiled/passed core tests,
then its harness raced a missing local-work parent directory. The corrected
clean-checkout harness and final tagged-source CI passed. Earlier failures were
not relabeled as passes.

Published installer: Authenticode NotSigned, no signer certificate. The app PE
imports only Windows system DLLs, with no separate VC/WebView/Python imports.
Both public downloads matched expected SHA-256 and size; portable executable
matched the tested build. A fresh owned per-user install/uninstall preserved
an adjacent user-data sentinel and removed its own shortcut/registry entry.

Website: build/check, 11 pipeline checks and 17 browser checks passed. Includes
61 normal routes, 320/390/650/768/1024/1440 widths, new gallery/download/hash
rendering, privacy, no-JS and nested Pages paths. Zero browser errors/failed
resources. Five live URLs returned 200 with matching download metadata,
structured data, catalog entry and sitemap routes.

## Limits and preservation

Read REQUIREMENTS.md, HELP.md and PERFORMANCE.md. Full virtual large files,
split editing, printing, diff, folder search and full CommonMark are deferred.
Clean Windows 10/11 machines, exhaustive accessibility/IME/RTL/complex shaping,
power-loss/disk-full/network race matrices, cold startup, typing latency,
scrolling FPS and large-edit peak memory remain additional work. Actual idle
CPU did not fully meet the near-zero planning target; raw results are published.

The final host snapshot compared recorded PID/creation-time/executable identities
and found no product-owned app/test executable running. Owned browser/server
helpers closed; reusable caches, evidence and unrelated apps/services remained.

Frozen local release records retain published:false as the generic stager's
production/store certification boundary. published-release.json records the
actual GitHub prerelease. Frozen files and public binary assets were not changed.
App inputs are identical between implementation build commit 1b46beb and the
release tag, which adds harness/docs/captures/metadata. No reproducible-binary
certification is asserted.
