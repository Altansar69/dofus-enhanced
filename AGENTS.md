# Dofus Enhanced maintenance

- The public repository is https://github.com/Altansar69/dofus-enhanced (renamed
  from dofus-miniatures on 2026-10-23; the old name redirects). Its owner's
  display name is Altansar; use the Altansar69 GitHub credential.
- The user requested automatic delivery: after an authorized mod update, increment
  the revision, build, test, package, commit/push the reviewed sources and publish
  a GitHub release. Do not leave a completed revision local-only.
- Never add Claude or Anthropic attribution (Co-Authored-By trailers, "Generated
  with" lines) to commits, release notes or documentation.

## Layout

- `src/<area>/`: the mod's sources, which include each other as `"<area>/<file>"`
  from `src/`. Areas: core, loader, ui, appearance, animations, glow, souls,
  metamob, maps, travel, treasure, notebook, jobs, social.
- `tests/<area>/`: the offline tests, one program per feature, registered with
  `dofus_test()` in `CMakeLists.txt`. `tests/support/` holds the fake native
  runtime fixture and `tests/update/` the updater's PowerShell tests.
- `package/`: files shipped as they are in the release archive
  (`Update-GitHub.ps1`, and `LISEZ-MOI.txt` with the version history).
- `docs/`: the detailed guide and the resource data notes. `tools/`: packaging,
  publishing and data generation. `third_party/`: MinHook (submodule),
  nlohmann/json and the WebView2 SDK.
- The shipped names are part of the release format, and installed loaders and
  updaters depend on them: `DofusMiniatures.dll`, `ModuleLoader.exe`,
  `ModuleAutoLoader.exe`, `TreasureOverlay.exe`, the
  `DofusMiniatures-3.7.11.11-rN` folders and the shared
  `DofusMiniatures-3.7.11.11-settings.ini`. Never rename them.

## Build, test and release

- Use `build.ps1`, then `ctest --test-dir build --output-on-failure --output-junit test-results.xml`.
  Package with `python tools/package-release.py --output <absolute output directory>`.
  Commit/push before `python tools/publish-release.py --package <rN-package.json> --notes <notes.md>`.
  Keep revisions immutable. Do not overwrite a published asset or reuse a tag.
- Since 1.13.1 (r113), releases are tagged with the public version `vX.Y.Z`
  (`tools/publish-release.py`'s default). The archive, folder and `release.ini`
  keep the revision `rN`, which the updater checks. Updaters before 1.13.1 only
  read `rN` tags: 1.13.1 itself was published as `r113` (`--revision-tag`). It is
  the only release those updaters can select, so it keeps that tag unless the
  owner asks otherwise; installs still below 1.13.1 would then need a manual
  reinstall. Never use `--revision-tag` again. At the owner's request
  (2026-10-23), the older releases are renamed once r113 is published: r92–r112
  to `v1.0.0`–`v1.13.0`, and r75–r91 (before 1.0.0) to `v0.75.0`–`v0.91.0`.
- Never publish game assemblies/metadata, installed game assets, user settings,
  logs, personal paths, credentials or analysis snapshots. The runtime archive
  has nine explicitly allowed entries. Keep all third-party licenses.
- The bootstrap is version-independent, but each module and its own loader must
  still pin the exact supported binary AND metadata hashes. Never bypass these
  checks, the native guards or the ABI verification to accept a game update.
- A new game build can require a native port. Validate method signatures, field
  layouts and interior callsites against that exact build before publication.
  The updater distributes a port; it does not generate one.
- Every native method goes through a 16-byte guard in `src/core/signatures.hpp`
  and `FindVerified`; every native field through a checked offset.
- Do not click, type, focus, inject into, terminate or restart an already open
  game without a fresh explicit request. Offline tests and read-only file checks
  are the default. The watcher ignores every game open when it starts.
- Updater changes require regression checks for incompatible clients, corrupt
  assets, partial downloads, traversal, preference retention and offline fallback.
  Verify the published archive through the real unauthenticated updater in an
  isolated directory. No game injection is needed for this check.
- Clearly distinguish offline validation from live initialization/rendering.
