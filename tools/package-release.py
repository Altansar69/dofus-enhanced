"""Build a public runtime archive, never containing local preferences or game files."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import xml.etree.ElementTree as ET
import zipfile

source = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--tests", type=Path, default=source / "build/test-results.xml")
args = parser.parse_args()
tests = ET.parse(args.tests).getroot()
cases = list(tests.iter("testcase"))
assert len(cases) >= 48 and all(
    not any(case.find(name) is not None for name in ("failure", "error", "skipped"))
    and case.get("status", "run") in ("run", "passed")
    for case in cases
), "All release checks must pass before packaging"
revision = int(re.search(r"ModRevision = (\d+)", (source / "src/core/version.hpp").read_text()).group(1))
semantic = re.search(r'ModSemanticVersion\[\] = "([0-9]+\.[0-9]+\.[0-9]+)"', (source / "src/core/version.hpp").read_text(encoding="utf8")).group(1)
assert revision >= 75
constants = (source / "src/core/common.hpp").read_text()
game = re.search(r'ExpectedGameHash\[\] = "([a-f0-9]{64})"', constants).group(1)
metadata = re.search(r'ExpectedMetadataHash\[\] = "([a-f0-9]{64})"', constants).group(1)
version = re.search(r'ClientVersion = "([0-9.]+)"', (source / "src/core/release-policy.hpp").read_text()).group(1)
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
folder = root / f"DofusMiniatures-3.7.11.11-r{revision}"
assert not folder.exists(), "Never overwrite an existing release; increment the revision"
folder.mkdir()
names = ("DofusMiniatures.dll", "ModuleLoader.exe", "ModuleAutoLoader.exe", "TreasureOverlay.exe", "Update-GitHub.ps1")
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
for name in names:
    origin = source / "package" / name if name.endswith(".ps1") else source / "build" / name
    shutil.copy2(origin, folder / name)
shutil.copy2(source / "package/LISEZ-MOI.txt", folder / "LISEZ-MOI.txt")
shutil.copy2(source / "third_party/webview2/LICENSE.txt", folder / "WEBVIEW2-LICENSE.txt")
hashes = {name: sha(folder / name) for name in names}
manifest = [
    "format=1", f"clientVersion={version}", f"revision={revision}",
    f"gameHash={game}", f"metadataHash={metadata}",
    *(f"{name}={hashes[name]}" for name in names[:3]),
]
(folder / "release.ini").write_text("\n".join(manifest) + "\n", encoding="utf8")
(folder / "update-files.ini").write_text("".join(f"{name}={value}\n" for name, value in hashes.items()), encoding="utf8")
# The updater's allowlist and digest cover all nine entries. No settings,
# access token, machine path, log, game binary or metadata is in this archive.
asset = root / f"DofusMiniatures-r{revision}-{game}-{metadata}.zip"
with zipfile.ZipFile(asset, "x", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(folder.iterdir()):
        archive.write(path, f"{folder.name}/{path.name}")
with zipfile.ZipFile(asset) as archive:
    assert archive.testzip() is None and len(archive.namelist()) == 9
    for name in names:
        assert hashlib.sha256(archive.read(f"{folder.name}/{name}")).hexdigest() == hashes[name]
result = {
    "revision": revision, "version": semantic, "clientVersion": version, "folder": str(folder),
    "asset": str(asset), "sha256": sha(asset), "bytes": asset.stat().st_size,
    "testsPassed": len(cases), "hashes": hashes,
}
(root / f"r{revision}-package.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
print(json.dumps(result, indent=2))
