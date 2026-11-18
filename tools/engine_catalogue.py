#!/usr/bin/env python3
"""Checks the engine picker's catalogue (gui/assets/engines.json) against each engine's GitHub releases.

The program reads the engines' latest versions itself; the catalogue only needs a change when an engine's files are
named otherwise, a platform or a processor level comes or goes. For each engine, its newest release of its own (side
releases, pre-releases and drafts left out, as engine_catalogue.cpp does), and for each platform the files the
catalogue names that it has: a platform with none is to look at.

    tools/engine_catalogue.py            the whole catalogue
    tools/engine_catalogue.py stockfish  some engines

Read-only. With `gh` logged in, through its API (5,000 requests an hour); otherwise anonymously (60 an hour, one per
engine). Exit status 1 when a platform has no file in an engine's newest release.
"""

import json
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

CATALOGUE = Path(__file__).resolve().parent.parent / "gui" / "assets" / "engines.json"
VERSION = r"[0-9]+(?:\.[0-9]+)*"


def regex(pattern, capture=False):
    return re.compile(pattern.replace("{ver}", f"({VERSION})" if capture else f"(?:{VERSION})"))


def releases(repo):
    path = f"repos/{repo}/releases?per_page=20"
    if shutil.which("gh"):
        out = subprocess.run(["gh", "api", path], capture_output=True, text=True, check=True)
        return json.loads(out.stdout)
    request = urllib.request.Request(
        f"https://api.github.com/{path}",
        headers={"Accept": "application/vnd.github+json", "User-Agent": "RookStudio-engine-catalogue-check"},
    )
    with urllib.request.urlopen(request, timeout=20) as answer:
        return json.loads(answer.read().decode("utf-8"))


def key(version):
    return [int(part) for part in version.split(".")]


def check(engine):
    tag = regex(engine["tag"], capture=True)
    ours = []
    for r in releases(engine["repo"]):
        m = tag.fullmatch(r["tag_name"])
        if r.get("draft") or r.get("prerelease") or not m:
            continue
        ours.append((key(m.group(1)), r.get("published_at") or "", m.group(1), r))
    if not ours:
        print(f"{engine['id']}: no release of its own among the 20 latest")
        return False
    ours.sort(key=lambda o: (o[0], o[1]), reverse=True)
    _, published, version, release = ours[0]
    names = [a["name"] for a in release.get("assets", []) if a.get("state") == "uploaded"]
    print(f"{engine['name']} {version} ({release['tag_name']}, {published[:10]})")
    fine = True
    for platform, files in engine["files"].items():
        found = []
        for f in files:
            matched = [n for n in names if regex(f["asset"]).fullmatch(n)]
            if len(matched) > 1:
                found.append(f"{f['needs']}: {len(matched)} files ({', '.join(matched)})")
            elif matched:
                found.append(f"{f['needs']}: {matched[0]}")
        if not found:
            fine = False
            print(f"  {platform:14} NONE of its {len(files)} files: their names may have changed")
        else:
            print(f"  {platform:14} " + "; ".join(found))
    return fine


def main(argv):
    catalogue = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    wanted = set(argv[1:])
    fine = True
    for engine in catalogue["engines"]:
        if wanted and engine["id"] not in wanted:
            continue
        fine = check(engine) and fine
    return 0 if fine else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
