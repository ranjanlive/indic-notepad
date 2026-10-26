# Build and release

Clone the **product root**, not only source_code. Assets, documentation and
dependency notices are part of the build/distribution inputs.

Requirements: Windows x64, MSVC C++ Build Tools and Windows SDK, CMake 3.24+,
Ninja, Python 3.11+, NSIS 3 for the optional installer. The helper locates an
installed MSVC through vswhere and never prints its environment or credentials.

```powershell
python -m pip install -r scripts/requirements.txt
python scripts/build.py
python scripts/run_ui_tests.py
python scripts/benchmark.py --samples 5
python scripts/package_native.py --makensis C:/path/to/makensis.exe
```

The first configure downloads official source archives with pinned SHA-256.
Later builds reuse the CMake dependency/build cache. Never remove system caches
or terminate processes by name. The app does not require Node, WebView, Qt,
Java, Gradle, Python or NSIS to run. MSVC CRT is linked statically.

Build outputs: source_code/build. Public artifacts: local-work/native. Neither
belongs in source Git. `run_ui_tests.py` uses a fresh visible test executable/profile,
waits for its report, and checks PID+creation time+path before timeout cleanup.
The smoke-test flag exists only in the separate test executable, which is not shipped.

`benchmark.py` runs the real app visibly. Benchmark mode requires a fresh explicit
profile, accepts no document paths and exits through the normal close flow.
The timing is process-start to Windows input-idle, not a cold-cache or frame/typing
latency certification. Keep hardware/method/sample count with any public numbers.

## Packaging

Portable ZIP includes the executable, portable.mode marker, README, full offline
help, MIT and complete upstream notices. Installer is per user; adds one Start
menu shortcut and its own Beta uninstall record. It never registers itself as a
default editor, requests elevation, starts a service, installs a runtime or
deletes personal text/recovery on uninstall.

Unsigned beta. A valid SHA-256 confirms expected bytes, not publisher identity.
No signing credentials are present. Do not tell users to bypass OS warnings.
Configure trusted signing for a future separately versioned production release.

## Release order

1. Review code, run core/native tests and record honest manual/platform limits.
2. Commit reviewed build inputs. Build/package the tagged source in CI.
3. Keep source tag stable. Create GitHub prerelease with real artifacts, notices,
   build receipt, SBOM, tests and correct-filename SHA256SUMS.
4. Download/verify released ZIP and installer hashes. Inspect Authenticode state.
5. Set current metadata to beta and actual download URLs/hashes/sizes; sync/build/check
   the website, review real screenshots and deploy its existing Pages workflow.
6. Freeze a new local release ID; retain source/commit manifests and measured evidence.
   The local stager's `published:false` is a production/store certification boundary,
   not evidence that the independently verified GitHub prerelease is absent.
7. Close only owned preview/test/build resources. Preserve reusable caches and data.

Never modify files inside an existing frozen release directory or overwrite
published binary assets. Bug fixes/rebuilds get another version and tag.
