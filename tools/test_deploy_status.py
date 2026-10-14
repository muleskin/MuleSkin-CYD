#!/usr/bin/env python3
"""Tests the site's deploy status, end to end: what deploy/redeploy.sh writes
(deploy-status.json, served on the site and shown in the flasher's footer)
and what deploy/ntfy/quiet-check.py makes of it (one push per redeploy that
failed or got stuck, and one when the next works).

redeploy.sh itself needs root, git, docker and the signing key, so its status
functions are cut out of it and run alone -- the same lines, not a copy.

    python3 tools/test_deploy_status.py        (CI: the host-tests job)

BASH_BIN picks the bash on Windows (Git Bash), where "bash" may be WSL's.
"""
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASH = os.environ.get("BASH_BIN", "bash")
fails = 0


def ck(name, ok):
    global fails
    print("  %-60s %s" % (name, "ok" if ok else "FAILED"))
    if not ok:
        fails += 1


def status_functions():
    text = open(os.path.join(ROOT, "deploy", "redeploy.sh"), encoding="utf-8").read()
    m = re.search(r"^STATUS_DIR=.*?^die\(\) .*?$", text, re.S | re.M)
    assert m, "redeploy.sh: no STATUS_DIR= ... die() block"
    return m.group(0)


def run_status(script):
    with tempfile.TemporaryDirectory() as d:
        body = "set -euo pipefail\nREPO=%s\nTAG=v9.9.9\n%s\n%s\n" % (d.replace("\\", "/"), status_functions(), script)
        r = subprocess.run([BASH, "-c", body], capture_output=True, text=True)
        p = os.path.join(d, ".pio", "flasher-bins", "deploy-status.json")
        return r.returncode, (open(p, encoding="utf-8").read() if os.path.exists(p) else None)


print("redeploy.sh's deploy-status.json")
rc, out = run_status('stage build "building the bins"')
st = json.loads(out) if out else {}
ck("a stage: running, with the stage and the tag", st.get("state") == "running" and st.get("stage") == "build" and st.get("tag") == "v9.9.9")
ck("started and updated, UTC", bool(re.match(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$", st.get("updated", ""))) and st.get("started"))

rc, out = run_status('FAILED_BOARDS=" cyd32c awok(sig)"; stage verify "say \\"hi\\" a\\\\b"')
try:
    st = json.loads(out)
    ok = True
except (TypeError, ValueError):
    st, ok = {}, False
ck("quotes and backslashes in the message: still JSON", ok)
ck("...the message kept readable", st.get("message") == "say 'hi' a/b")
ck("failed boards as a list", st.get("failed") == ["cyd32c", "awok(sig)"])

rc, out = run_status('STAGE=checkout; die "no tag v9.9.9"; echo not-reached')
st = json.loads(out) if out else {}
ck("die(): failed, with the reason, and the script stops", rc == 1 and st.get("state") == "failed" and st.get("message") == "no tag v9.9.9")

rc, out = run_status('STAGE=build\ntrap \'status failed "a command failed at line $LINENO (stage $STAGE)"\' ERR\nfalse')
st = json.loads(out) if out else {}
ck("a command that fails: the ERR trap says so", rc != 0 and st.get("state") == "failed" and "stage build" in st.get("message", ""))

rc, out = run_status('STAGE=done; status ok "flasher.oillie.cloud serves v9.9.9"')
st = json.loads(out) if out else {}
ck("done: ok", st.get("state") == "ok" and st.get("failed") == [])

# The verify stage pipes curl into grep. "grep -q" stops at the first match and
# curl, still writing, fails on the closed pipe -- under pipefail a false
# "failed" (the v3.6.0 redeploy, 2026-10-13). grep -c reads to the end.
text = open(os.path.join(ROOT, "deploy", "redeploy.sh"), encoding="utf-8").read()
ck("no curl piped into grep -q (pipefail turns it into a false failure)",
   not re.search(r"curl[^\n|]*\|\s*grep -q", text))

print("build_flasher_bins.sh's versions.json")
bfb = open(os.path.join(ROOT, "tools", "build_flasher_bins.sh"), encoding="utf-8").read()
m = re.search(r"python3 - \"\$OUT\" <<'PY'\n(.*?)\nPY\n", bfb, re.S)
assert m, "build_flasher_bins.sh: no versions.json block"
VBLOCK = m.group(1)


def versions(folders, root):
    """The block run over a fake flasher-bins: `folders` archived, `root` in
    root-tag (None: no file). Returns ([(tag, dir)], folders left)."""
    with tempfile.TemporaryDirectory() as d:
        for t in folders:
            os.makedirs(os.path.join(d, t))
        if root is not None:
            open(os.path.join(d, "root-tag"), "w").write(root + "\n")
        subprocess.run([sys.executable, "-c", VBLOCK, d], check=True, capture_output=True)
        v = json.load(open(os.path.join(d, "versions.json")))["versions"]
        return [(e["tag"], e["dir"]) for e in v], sorted(x for x in os.listdir(d) if os.path.isdir(os.path.join(d, x)))


v, _ = versions(["v3.4.0", "v3.5.0", "v3.6.0"], "v3.6.0")
ck("the newest at the top level: it is \"\", the others their folders",
   v == [("v3.6.0", ""), ("v3.5.0", "v3.5.0/"), ("v3.4.0", "v3.4.0/")])
v, _ = versions(["v3.4.0", "v3.5.0", "v3.6.0"], "v3.4.0")
ck("rolled back to v3.4.0: IT is \"\", v3.6.0 keeps its folder",
   v == [("v3.6.0", "v3.6.0/"), ("v3.5.0", "v3.5.0/"), ("v3.4.0", "")])
v, _ = versions(["v3.5.0", "v3.6.0"], None)
ck("a failed build (no root-tag): nothing claims the top level",
   all(d for _, d in v) and [t for t, _ in v] == ["v3.6.0", "v3.5.0"])
tags = ["v3.%d.0" % i for i in range(1, 8)]
v, left = versions(tags, "v3.1.0")
ck("seven kept as five, plus the rolled-back-to v3.1.0",
   left == sorted(["v3.7.0", "v3.6.0", "v3.5.0", "v3.4.0", "v3.3.0", "v3.1.0"]) and ("v3.1.0", "") in v)
v, left = versions(tags, "v3.7.0")
ck("...and the oldest go when the newest is live", "v3.2.0" not in left and "v3.1.0" not in left and len(v) == 5)

print("quiet-check.py's flasher pushes")
spec = importlib.util.spec_from_file_location("quiet_check", os.path.join(ROOT, "deploy", "ntfy", "quiet-check.py"))
qc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qc)
NOW = qc.iso_s("2026-10-13T16:00:00Z")
ck("iso_s reads the stamp as UTC", NOW == 1791907200)


def stat(state, updated="2026-10-13T15:50:00Z", started="2026-10-13T15:30:00Z", **kw):
    d = {"tag": "v3.4.0", "state": state, "stage": kw.pop("stage", "build"), "message": kw.pop("message", "m"),
         "failed": kw.pop("failed", []), "started": started, "updated": updated}
    d.update(kw)
    return d


news, keep = qc.deploy_news(None, {}, NOW)
ck("no status file: nothing", news is None)
news, keep = qc.deploy_news(stat("running"), {}, NOW)
ck("running a few minutes: nothing", news is None)
news, keep = qc.deploy_news(stat("running", updated="2026-10-13T13:00:00Z"), keep, NOW)
ck("running for three hours: stuck", news and "stuck" in news[0])
news, keep2 = qc.deploy_news(stat("running", updated="2026-10-13T13:00:00Z"), keep, NOW)
ck("...said once", news is None)
news, keep = qc.deploy_news(stat("failed", stage="verify", message="the site serves 3.3.0"), keep2, NOW)
ck("failed: FAILED, with the stage and reason", news and "FAILED" in news[0] and "verify" in news[1] and news[2] == "4")
news, keep = qc.deploy_news(stat("failed", stage="verify"), keep, NOW)
ck("...said once", news is None)
news, keep = qc.deploy_news(stat("ok", started="2026-10-13T15:55:00Z", updated="2026-10-13T15:59:00Z"), keep, NOW)
ck("the next redeploy works: deployed", news and news[0] == "flasher: v3.4.0 deployed")
news, keep = qc.deploy_news(stat("ok", started="2026-10-13T15:55:00Z"), keep, NOW)
ck("...said once", news is None)
news, keep = qc.deploy_news(stat("ok", started="2026-10-14T09:00:00Z"), keep, NOW)
ck("a redeploy that just works, after one that worked: nothing", news is None)
news, keep = qc.deploy_news(stat("ok", started="2026-10-15T09:00:00Z", failed=["cyd32c"]), keep, NOW)
ck("ok but a board not built: a warning naming it", news and "not every board" in news[0] and "cyd32c" in news[1])

print("all passed" if not fails else "%d FAILED" % fails)
sys.exit(1 if fails else 0)
