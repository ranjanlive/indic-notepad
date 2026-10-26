> **Indic NotePad:** Native C++/Windows instructions are in [BUILD_AND_RELEASE.md](BUILD_AND_RELEASE.md), [HELP.md](HELP.md) and [REQUIREMENTS.md](REQUIREMENTS.md). Android commands below are shared company examples and do not apply to this tool.

# IndicStudio development guide

This guide accompanies PRODUCT_STANDARD.md. In a product repository commands run from the **product root**. In the central workspace substitute `development/tools/project.py --project <product-path>` for `scripts/project.py`.

## First use

```powershell
python -m pip install -r scripts/requirements.txt
python scripts/project.py validate
```

The four main directories are source_code, assets, docs and release. Open source_code in the relevant IDE. For Android it is the Gradle root, not a parent containing another android/ directory.

## Develop and build

```powershell
python scripts/project.py build --task debug
python scripts/project.py build --task check
python scripts/project.py build --task bundle
```

`debug` builds an APK, `check` runs the existing focused JVM tests and Android lint (plus tests in explicitly included shared/server JVM modules when present), and `bundle` builds the existing release variant (unsigned until signing is separately configured). Existing application code, build dependencies and Gradle wrapper are preserved by the migration. The helper uses source_code/, auto-discovers Android Studio's local JBR/SDK on this Windows machine, or respects JAVA_HOME and ANDROID_HOME elsewhere. Missing local.properties is generated as ignored machine configuration. It launches one build with --no-daemon and --max-workers=2, logs under ignored docs/verification/local-*, records owned PID/creation-time identities and reclaims only surviving processes it started. It does not kill shared adb or unrelated JVMs. No emulator is started by this helper.

Run only the checks appropriate to the change. A structural source copy is not grounds to assert new UI testing. For actual app changes perform a brief visible emulator/device smoke check, save truthful evidence, then close only the emulator you own using the established resource policy. Do not blindly reuse old emulator-owner files or historical session scripts with absolute paths.

The build helper takes a shared build slot when projects live under the studio catalog, preventing overlapping app builds by different agents. Standalone checkouts use a per-project slot. A stale slot is removed only after its recorded PID/creation-time identity has exited; an active build is never displaced.

For games/tools choose the engine/framework and platform-specific commands in the project plan. The new-project scaffold supplies organization and metadata, not a fabricated working game or desktop implementation.

If a corrected configuration still produces a confirmed stale lint result, run `python scripts/project.py build --task check --rerun-tasks` once. This reruns tasks and preserves reusable caches; do not use it for routine builds.

Do not infer emulator RAM from `-memory` alone. The installed emulator can automatically raise its minimum (the API 37 image raised a 1536 MB request to 4096 MB during the legacy review). Its supported `-lowram` option removes that automatic floor. Verify `/proc/meminfo` in the owned guest before testing. In this review, enforced 1536 MB on API 37 produced repeated guest low-memory kills; evidence is in the company workspace's development/reports/legacy-smoke/resource-oom-1536.json. Only the guest cap was raised to 2048 MB for an isolated API 36 test AVD; Java stayed at 1536 MB. Keep the original AVD configurations unchanged, record the actual API/RAM and close tracked emulator processes afterward.

## Documentation and assets

- Current code decisions/design go in docs/design/ or a focused document under docs/.
- Icons go in assets/icons/, banners in assets/banners/, actual device captures in assets/screenshots/<device>/.
- Optimized web assets go in assets/website/; use meaningful alt text, dimensions and current screenshots.
- Use the approved original rocket as the company attribution. Do not replace a product's own identity with the company logo accidentally.
- Historical original plans/delivery docs remain in ignored docs/archive/. They can contain obsolete paths and unreviewed claims; they are not the current runbook.

## Stage a versioned release

Example after building and editing the current website/store metadata:

```powershell
python scripts/project.py stage-release 1.0.0-preview.2 --apk source_code/app/build/outputs/apk/debug/app-debug.apk --aab source_code/app/build/outputs/bundle/release/app-release.aab
python scripts/project.py validate --require-builds
```

The command copies the binaries into a new immutable snapshot and records SHA-256, byte size, source content manifest, package, versionName/versionCode, metadata and privacy JSON. It refuses to overwrite an existing ID. It does not sign or publish. APK signing is labeled unverified/debug build; AABs without signature entries are labeled unsigned. Do not infer production signing validity from the file name or a signature-entry check. Use Android signing verification and the actual publisher identity before production distribution.

`release/latest.json` points to the newest staged local snapshot. `published: false` is intentional. The initial migrated snapshot is 1.0.0-preview.1; its binary bytes are unchanged from the overnight output, even where the Android versionName is simply 1.0.

For a game or desktop tool, stage actual built installers/archives with explicit platform assignments:

```powershell
python scripts/project.py stage-release 0.1.0-preview.1 --artifact "windows=source_code/output/my-tool.exe" --artifact "linux=source_code/output/my-tool.AppImage" --artifact "macos=source_code/output/my-tool.dmg"
```

Use only platforms declared in project.json. Signing remains unverified; staging is not code-signing, notarization or publication. These example output paths must be replaced by your real builds.

`validate` works on a source-only GitHub clone: it validates available metadata/snapshot hashes and reports binary files not present. Use `--require-builds` on a local release workspace to require and verify every binary. Missing non-binary snapshot files always fail.

## Private signing keys and secrets

Provision real signing/upload material only when preparing publishing. Store local files in release/private/ or use an appropriate external secret manager. The examples under docs/signing/ contain field names and blank values only. They are not active signing configuration.

Never put key passwords in project.json, store-listing.json, metadata.json, release.json or public documentation. Never include signing keys in a GitHub Release, a source ZIP, a website ZIP or a dataset. Keep an independent encrypted backup with recovery access; Git ignore rules alone do not secure or back up files. No existing signing keys were found in the inspected app source/artifact/publishing locations; the migration does not search unrelated accounts or folders for credentials.

The packaging tool enforces forbidden filenames/paths and checks selected high-confidence credential patterns without echoing values. This is a guard, not comprehensive secret scanning. Review a package and enable the repository's appropriate secret-scanning protections before actual publication.

## Prepare a GitHub repository upload

```powershell
python scripts/project.py package --destination ../github-uploads
```

Upload the resulting `<slug>-github-source.zip` **contents** into one organization repository, e.g. IndicStudio/indic-tasks. The root includes source_code, assets, current docs, release manifests, scripts and CI. Do not upload the entire local workspace with a browser picker; that can bypass .gitignore. The source package explicitly excludes release binaries/private keys, historical raw prompts, caches and host configuration.

Alternatively, review local `git status`, stage only intended public files, make a commit using your own Git identity, create the chosen IndicStudio repository with the intended visibility, set its verified origin and push main. The migration initializes local Git only and intentionally makes no remote repository, commit, credential or visibility decisions. Pick a software license deliberately; no open-source license has been invented.

GitHub CI validates structure, schemas and public release records without Android SDK installation or signing secrets. It does not automatically run twelve cloud Android builds or publish artifacts. Local build commands remain the source of build evidence until a project-specific build workflow is deliberately added.

## Website integration

Editable files:

```
docs/website/metadata.json
docs/website/privacy_content.json
assets/website/...
```

Use product-root-relative `assets/...` URLs for local website images. The current imports use assets/website/icon.webp and assets/website/screen-1.webp. Full HTTPS store/release URLs stay unchanged. Shared website URLs starting `/assets/` remain references to that website's public assets. Publish larger binaries using actual GitHub Release/store URLs rather than copying ignored installers into a static asset folder.

Preview a synchronization plan, then apply it:

```powershell
python scripts/project.py sync-website --website "C:/path/to/indicstudio/website"
python scripts/project.py sync-website --website "C:/path/to/indicstudio/website" --apply
```

The operation copies only current product JSON and its referenced local images. It does not change release snapshots or upload the website. In the website project, follow its guide: run scripts/build.py and scripts/check_site.py, review the page, then commit/publish according to its GitHub Pages workflow.

Edit product information in this project's docs/website going forward; the website copy is generated delivery content. Avoid editing both independently. Support mailbox and public product/privacy URLs were taken from the supplied company website; domain deployment and mailbox operation are separate facts.

## New products

From the central indicstudio/development directory:

```powershell
python tools/new_product.py games my-game --title "My Game" --platforms android
python tools/new_product.py tools my-tool --title "My Tool" --platforms windows,linux,macos
```

This creates the same four-directory standard, draft metadata/privacy, project identity, safe Git rules and tooling. It leaves source_code ready for the selected engine/framework. Games/tools are not developed or published by scaffolding. Do not populate a public listing with example features or placeholder privacy statements.

## Maintenance checks

- Keep slugs, package IDs and repository names stable.
- Preserve existing release IDs and checksums. Stage a fresh ID for a new build.
- After code changes, build/check/smoke as appropriate and update current docs.
- Before distribution, inspect signing identity, actual store URLs, privacy behavior and accurate release notes.
- Keep source and assets backed up; private signing backups are separate from Git.
- Keep only one app build and one visible owned emulator active. Preserve reusable caches and reclaim owned helpers.

The original overnight workspace and Training/ data remain untouched. See the migration report for hashes and provenance. Do not start a new overnight run against legacy Apps/ paths after switching to products/.

## Whole-studio maintenance

From the central development directory, run sequential checks and source packaging:

```powershell
python tools/manage_products.py validate
python tools/manage_products.py package
python tools/manage_products.py sync-website
python tools/manage_products.py sync-website --apply
```

Packages go into ../github_uploads/. The last two commands preview/apply the catalog's current metadata and referenced web images to the separate website source. They do not build or deploy it. Each app's versioned release metadata remains unchanged by a website sync.
