# Reporting a security issue

Email support@indicstudio.com with the product and version and a minimal description. Do not post private keys, tokens or personal data in a public issue. This mailbox was supplied by the existing company website; no response SLA is asserted.

Signing material belongs only in ignored release/private/ or an appropriate external secret manager. A .gitignore is not encryption, access control or a backup.

The app does not execute opened content, render HTML/scripts, fetch Markdown
images or upload text. It has no telemetry or updater. Windows/editor libraries
process untrusted text; limits and tests are safeguards, not certification.
Keep OS protections active and independently back up documents.

Recovery is local plaintext plus an integrity checksum, not encryption. A profile
is locked against concurrent app writers. See HELP.md for history and recovery
controls. The first beta is unsigned. A security fix must use a new release ID;
never replace immutable published binaries under the same name.
