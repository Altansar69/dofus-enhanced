"""Publish an already tested, committed and pushed package to the owner's GitHub."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import urllib.error
import urllib.parse
import urllib.request

REPO = "Altansar69/dofus-enhanced"
ACCOUNT = REPO.split("/")[0]
source = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--package", type=Path, required=True)
parser.add_argument("--notes", type=Path, required=True)
args = parser.parse_args()
package = json.loads(args.package.read_text(encoding="utf8"))
asset = Path(package["asset"])
assert hashlib.sha256(asset.read_bytes()).hexdigest() == package["sha256"]
assert package["testsPassed"] >= 48
def git(*arguments):
    return subprocess.check_output(["git", "-C", str(source), *arguments], text=True).strip()

assert not git("status", "--porcelain"), "Commit the reviewed sources before publishing"
commit = git("rev-parse", "HEAD")
remote = git("remote", "get-url", "origin")
assert remote in (f"https://{ACCOUNT}@github.com/{REPO}.git", f"https://github.com/{REPO}.git")
assert git("ls-remote", "origin", "refs/heads/main").split()[0] == commit, "Push main before publishing"
# Use only the configured GitHub owner's credential via Git Credential Manager.
# It stays in memory, is never printed, saved, or included in downloaded packages.
env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never", GCM_GUI_PROMPT="false")
credential = subprocess.run(
    ["git", "credential", "fill"], input=f"protocol=https\nhost=github.com\nusername={ACCOUNT}\n\n",
    capture_output=True, text=True, env=env, timeout=20, check=True,
)
fields = dict(line.split("=", 1) for line in credential.stdout.splitlines() if "=" in line)
headers = {
    "Authorization": "Bearer " + fields["password"], "Accept": "application/vnd.github+json",
    "User-Agent": "DofusMiniatures-release-maintenance", "X-GitHub-Api-Version": "2022-11-28",
}
def request(method, path, data=None, upload=False):
    url = path if upload else "https://api.github.com" + path
    if upload:
        assert urllib.parse.urlparse(url).netloc == "uploads.github.com"
    body = data if upload else None if data is None else json.dumps(data).encode()
    sent_headers = dict(headers)
    if upload:
        sent_headers["Content-Type"] = "application/zip"
    req = urllib.request.Request(url, data=body, headers=sent_headers, method=method)
    with urllib.request.urlopen(req, timeout=60) as response:
        return json.load(response)

assert request("GET", "/user")["login"] == ACCOUNT, "Wrong GitHub account"
repo = request("GET", f"/repos/{REPO}")
assert not repo["private"] and repo["permissions"]["push"], "Public writable repository required"
tag = f"r{package['revision']}"
try:
    release = request("GET", f"/repos/{REPO}/releases/tags/{tag}")
except urllib.error.HTTPError as error:
    if error.code != 404:
        raise
    release = request("POST", f"/repos/{REPO}/releases", {
        "tag_name": tag, "target_commitish": commit,
        "name": (f"Dofus Enhanced {package['version']} ({tag}) — Dofus {package['clientVersion']}" if package.get("version")
                 else f"{tag} — Dofus {package['clientVersion']}"),
        "body": args.notes.read_text(encoding="utf8"), "draft": True, "prerelease": False,
    })
assets = request("GET", f"/repos/{REPO}/releases/{release['id']}/assets")
existing = next((item for item in assets if item["name"] == asset.name), None)
if existing is None:
    assert release["draft"], "Never modify a published release; increment the revision"
    url = release["upload_url"].split("{", 1)[0] + "?name=" + urllib.parse.quote(asset.name)
    existing = request("POST", url, asset.read_bytes(), upload=True)
assert existing["size"] == package["bytes"] and existing["digest"] == "sha256:" + package["sha256"], "Uploaded asset integrity mismatch"
if release["draft"]:
    release = request("PATCH", f"/repos/{REPO}/releases/{release['id']}", {"draft": False, "make_latest": "true"})
assert not release["draft"] and not release["prerelease"]
print(json.dumps({"release": release["html_url"], "asset": existing["browser_download_url"],
    "digest": existing["digest"], "commit": commit}, indent=2))
