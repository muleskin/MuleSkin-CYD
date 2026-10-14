#!/usr/bin/env python3
"""The flasher's versions.json, from the release folders in flasher-bins.

    python3 tools/flasher_versions.py <flasher-bins dir>

Each release built from its tag is kept in flasher-bins/vX.Y.Z/; root-tag
names the one the top level holds (written by build_flasher_bins.sh after a
clean tagged build, and by redeploy.sh after a deploy). The newest five are
kept, plus the top level's whatever its age; that one points at the top
level (""), every other at its folder. Run by build_flasher_bins.sh, and by
redeploy.sh from origin/main after every deploy -- so a rollback to a tag
whose own build script predates this still gets a correct list. Python 3
standard library only (the server runs it outside docker).
"""
import json, os, re, shutil, sys
out = sys.argv[1]
key = lambda t: tuple(int(x) for x in t[1:].split("."))
try:
    root = open(os.path.join(out, "root-tag")).read().strip()
except OSError:
    root = ""
tags = sorted((d for d in os.listdir(out)
               if re.fullmatch(r"v\d+\.\d+\.\d+", d) and os.path.isdir(os.path.join(out, d))),
              key=key, reverse=True)
# The newest five, and the one at the top level whatever its age: a rollback
# to an old release must not prune the release it rolled back to.
keep = tags[:5] + ([root] if root in tags[5:] else [])
for old in tags:
    if old not in keep:
        shutil.rmtree(os.path.join(out, old))
tags = keep
# Each release points at its own folder, except the one the top level holds,
# which points there (""). That is not always the newest: redeploying an older
# tag puts IT at the top level, and the newest stays in its folder -- before
# 2026-10-14 the newest was always called "", which after a rollback offered
# the old firmware under the new version's name.
versions = [{"tag": t, "dir": "" if t == root else t + "/"} for t in tags]
with open(os.path.join(out, "versions.json"), "w") as f:
    json.dump({"versions": versions}, f, indent=2)
print("versions.json:", ", ".join(tags) or "(no releases archived)")
