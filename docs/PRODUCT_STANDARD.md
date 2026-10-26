# IndicStudio product standard — version 1

One product, one canonical workspace, one GitHub repository. Applies to apps, games and tools. Established 25 October 2026.

```
indicstudio/
  products/
    apps/indic-tasks/
      project.json
      source_code/                   # Android Gradle root, engine project or desktop source
      assets/
        icons/
        banners/
        screenshots/android/         # only actual captures
        screenshots/tablet/
        website/                     # optimized web media
        brand/
      docs/
        guide.md
        PRODUCT_STANDARD.md
        design/
        store/store-listing.json
        website/metadata.json        # current editable website data
        website/privacy_content.json
        schemas/
        verification/
        signing/signing.properties.example
        archive/                     # ignored historical plans/raw draft docs
      release/
        latest.json
        1.0.0-preview.1/
          release.json
          metadata.json              # frozen website snapshot
          privacy_content.json       # frozen policy snapshot
          store-listing.json
          source-manifest.json
          SHA256SUMS.txt
          builds/android/            # local binaries, ignored by Git
        private/                     # signing keys/config, ignored and never packaged
      scripts/project.py
      scripts/requirements.txt
      .github/workflows/validate.yml
      .gitignore
      .gitattributes
      README.md
      AGENTS.md
      CHANGELOG.md
      SECURITY.md
      NOTICE.md
    games/<slug>/                    # same four main directories
    tools/<slug>/                    # same four main directories
    catalog.json
  development/                       # standard, templates, migration and maintenance tools
  website/                           # independent website repository
  kits_site/                         # independent brand-kit repository
```

## Ownership

- `source_code/` is the buildable project. For current Android apps it contains gradlew, app/, Gradle configuration and source resources directly. Open this folder in Android Studio.
- `assets/` is the authoritative design/export library. App-bundled resources remain inside source_code/app/src/main/res; changing an external icon does not automatically regenerate Android adaptive icons.
- `docs/` contains the current documentation. `docs/website/` is the current editable product and privacy metadata; update it before syncing to the website. Store-listing data is a draft until reviewed against the released binary.
- `release/<id>/` is a frozen evidence/build/JSON snapshot. Never replace a file in an existing release ID. A correction or rebuild gets another version/prerelease ID. The staging command labels preview/beta builds; it does not certify production releases.
- `release/private/` holds product-specific signing material **locally when provisioned**. It is not committed, synced, printed, published, included in source ZIPs or used as a public download. Git ignore rules do not encrypt it or replace independent secure backups.
- `project.json` identifies type, slug, package, supported systems, paths, planned GitHub organization/repository and latest staged release.

## Repository boundary

The GitHub repository is the **product root**, including source_code/, assets/, current docs and public release JSON/checksums. This keeps source, design and documentation in one version history. Upload the prepared `*-github-source.zip` contents, not the entire local folder and not just the source_code subdirectory. `source_code/` remains the dedicated code directory requested by the owner.

Local APK/AAB/installers belong in versioned release/builds directories, then on GitHub Releases or the appropriate store when publication is authorized. Do not keep generated binary history or secrets in Git. The safe package excludes Git internals, builds, caches, logs, original agent prompts, local configuration and private files.

Every product is intended for a separate repository under **IndicStudio**. A planned repository URL is not evidence that a repository exists or is accessible. Current migration creates local main branches only; it does not create remote repositories, make commits, change visibility, push, submit to stores or upload datasets.

## Release information

Use semantic release IDs, e.g. 1.0.0-preview.1, 1.0.0-beta.1, 1.0.0. The Android versionName and versionCode remain explicit and may differ from a workspace prerelease ID. Increase platform version codes according to the distribution channel before shipping a newer binary.

Each snapshot records file SHA-256 and size, original binary kind, observed/unknown signing state, source file hashes, optional Git commit and publication state. A source tree hash identifies content; it does not prove the artifact was reproducibly built from that tree. Imported snapshots are labeled as historical migration artifacts.

Signing is a separate release operation. No upload or signing keystores are generated during organization. Never confuse an unsigned AAB, a debug-signed APK or a staged beta with a published production release. Keep product upload keys distinct, backed up and outside version control. Configure production signing deliberately; the included properties example is not automatically wired into Gradle.

## Lifecycle and working practice

Idea → brief/plan → source implementation → appropriate build/checks → brief visible-device/emulator smoke → versioned release snapshot → metadata/assets update → website sync/build → separately authorized publishing.

Use one product session/workspace at a time. For current Android projects: two workers, 1536 MB Java heap, no persistent Gradle daemon, in-process Kotlin compiler. Preserve reusable download/build caches. Use a visible emulator only while needed, with 1536 MB guest RAM/two cores and the existing owned-process cleanup policy. Never stop shared adb, purge system caches or kill processes by name. Record a real OOM/boot failure before raising only the necessary cap to 2048 MB.

The original Apps/ overnight workspaces are retained as historical evidence because their logs and training dataset refer to those paths. Future work goes into products/, not both copies. Do not restart the legacy overnight controller against the old directories. Do not duplicate or export the original agent transcript inside a public source repository.

## Reference guidance

- Android signing and key custody: https://developer.android.com/studio/publish/app-signing
- GitHub binary distribution: https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github
- Current brand assets: https://kits.indicstudio.com/guide.md (or local kits_site/guide.md before deployment)
