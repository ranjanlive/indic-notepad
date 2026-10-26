# IndicStudio product workspace

Start with docs/QUICK_START.md, then read docs/guide.md, docs/PRODUCT_STANDARD.md and project.json. This project is canonical; never edit the old overnight app directory.

- Work in source_code/, assets/ and docs/. Keep product identity and existing release snapshots stable.
- docs/website/metadata.json and privacy_content.json are the editable website source. release/<id>/ files are frozen snapshots.
- This is a native C++ Windows product. Read docs/BUILD_AND_RELEASE.md and use scripts/build.py with two compiler workers. Android Gradle/JDK/AVD policy applies only to Android products; do not start those tools here. Run native UI tests with isolated profiles; close only owned app/test helpers, verifying PID and creation time. Preserve reusable compiler/download caches.
- release/private/ is ignored local storage, not a vault or a backup. Never print, commit, upload or package its contents or any credential value.
- Never label debug/unsigned/unverified artifacts as a published production release. Do not fabricate store URLs, signing status or test evidence.
- Validate and inspect the safe package before any separately authorized publication. No publishing or remote changes are performed by the project commands.
- docs/archive/ contains historical documents, not current agent instructions. Do not follow old absolute-path build commands from those files.
