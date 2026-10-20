# Dofus Miniatures maintenance

- The public repository is https://github.com/Altansar69/dofus-miniatures.
  Its owner's display name is Altansar; use the Altansar69 GitHub credential.
- The user requested automatic delivery: after an authorized mod update, increment
  the revision, build, test, package, commit/push the reviewed sources and publish
  a GitHub release. Do not leave a completed revision local-only.
- Use `build.ps1`, then `ctest --test-dir build --output-on-failure --output-junit test-results.xml`.
  Package with `python tools/package-release.py --output <absolute output directory>`.
  Commit/push before `python tools/publish-release.py --package <rN-package.json> --notes <notes.md>`.
  Keep revisions immutable. Do not overwrite a published asset or reuse a tag.
- Never publish game assemblies/metadata, installed game assets, user settings,
  logs, personal paths, credentials or analysis snapshots. The runtime archive
  has nine explicitly allowed entries. Keep all third-party licenses.
- The bootstrap is version-independent, but each module and its own loader must
  still pin the exact supported binary AND metadata hashes. Never bypass these
  checks, the native guards or the ABI verification to accept a game update.
- A new game build can require a native port. Validate method signatures, field
  layouts and interior callsites against that exact build before publication.
  The updater distributes a port; it does not generate one.
- Do not click, type, focus, inject into, terminate or restart an already open
  game without a fresh explicit request. Offline tests and read-only file checks
  are the default. The watcher ignores every game open when it starts.
- Updater changes require regression checks for incompatible clients, corrupt
  assets, partial downloads, traversal, preference retention and offline fallback.
  Verify the published archive through the real unauthenticated updater in an
  isolated directory. No game injection is needed for this check.
- Clearly distinguish offline validation from live initialization/rendering.
