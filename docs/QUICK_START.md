> **Indic NotePad:** Native C++/Windows instructions are in [BUILD_AND_RELEASE.md](BUILD_AND_RELEASE.md), [HELP.md](HELP.md) and [REQUIREMENTS.md](REQUIREMENTS.md). Android commands below are shared company examples and do not apply to this tool.

# IndicStudio — new product mini checklist

**Idea → Requirements → Develop → Test → Release → CDN → Website → Publish**

Start in `indicstudio/`. Keep one workspace/session per product.

## 1. Create the workspace

- [ ] Choose a permanent slug, product type and supported platforms.
- [ ] Create the draft:

```powershell
python development/tools/new_product.py apps my-app --title "My App" --platforms android
```

Use `games` for games; use `tools` and `--platforms windows,linux,macos` for desktop tools.

- [ ] Work in `products/<type>/<slug>/`. Put the actual Gradle/engine project in `source_code/`; the scaffold creates folders, not an implementation.

## 2. Write requirements and plan

- [ ] `docs/REQUIREMENTS.md`: users/problem, MVP, screens, platforms/minimum OS, data/permissions and acceptance checks.
- [ ] `docs/PLAN.md`: stack, storage, implementation tasks and completion criteria.
- [ ] Set product identity/platforms in `project.json`; Android package: `com.indicstudio.appname`.

## 3. Develop and verify

- [ ] Implement in `source_code/`; use the company brand kit for attribution and a product-specific icon/UI.
- [ ] Build and run focused checks. Android commands, from the product root:

```powershell
python scripts/project.py build --task debug
python scripts/project.py build --task check
```

- [ ] Briefly test the main flow on a **visible** emulator/device; save evidence in `docs/verification/`, then close owned resources. Retain the two-worker/no-daemon/1536 MB policy.

## 4. Complete the product files

- [ ] `source_code/`: buildable code and build instructions.
- [ ] `assets/`: icon, banners, real screenshots and optimized web images.
- [ ] `docs/`: requirements, plan, design/architecture, verification and store listing.
- [ ] Finish `docs/website/metadata.json` and `privacy_content.json`: real features, screenshots, platforms, permissions/privacy, contact and changelog. Remove draft placeholders.

## 5. Prepare the release

- [ ] Update versions, Android `versionCode`, README and CHANGELOG.
- [ ] Produce signed distribution files: APK for Android downloads, AAB for stores, or platform installers/archives. Keep keys/passwords in ignored `release/private/` or a secret manager, with separate backups.
- [ ] Freeze a new `release/<version>/`: `builds/`, `release.json`, `metadata.json`, `privacy_content.json`, `source-manifest.json`, `SHA256SUMS.txt`. Never overwrite a previous version.
- [ ] Follow [release staging details](guide.md#stage-a-versioned-release), then run `python scripts/project.py validate --require-builds`.

**Current automation stages preview/beta snapshots; it does not certify or publish production releases.**

## 6. Add CDN downloads

- [ ] Stage public binaries in `cdn/public/<type>/<slug>/releases/<version>/`; keep internal AABs in `cdn/internal/`.
- [ ] For Android app/game debug previews (an unsigned AAB is optional), from `indicstudio/`:

```powershell
python cdn/scripts/stage_releases.py stage
python cdn/scripts/stage_releases.py verify
```

- [ ] Signed production builds and desktop/web game or tool installers need the appropriate publishing workflow first; the CDN helper handles Android app/game debug previews only. A missing AAB is reported as not built.
- [ ] Upload **public/ contents only** to public R2; verify downloads/hashes, then update the channel pointer last. Never upload keys or internal files. See the company workspace's `cdn/README.md`.

## 7. Publish the website and source

- [ ] Add verified live CDN/store URLs, sizes, hashes and release notes to current product metadata; set its real lifecycle/indexing status.
- [ ] From the product root, sync the website:

```powershell
python scripts/project.py sync-website --website ../../../website --apply
```

- [ ] Build/check/review the website, then publish through GitHub Pages. Confirm the product page, privacy policy and correct platform downloads work.
- [ ] Push reviewed source/assets/docs to `IndicStudio/<slug>`; use the safe source packager for manual uploads. Exclude binaries and secrets from Git.

## 8. Finish and maintain

- [ ] Record actual published URLs/status and signing/test evidence; close owned build/emulator resources.
- [ ] For updates, repeat steps 3–8 with a new version and retain the previous release for rollback.
