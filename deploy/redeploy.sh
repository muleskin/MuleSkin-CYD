#!/bin/bash
# Puts a release on flasher.oillie.cloud: the three server steps of
# deploy/README.md in one go, then a check that the live site really serves
# it (a redeploy once "worked" while serving the old files, because the
# checkout had never been updated).
#
#   sudo OTA_SIGNING_KEY=/path/to/ota-key.pem /root/MuleSkin-CYD/deploy/redeploy.sh [vX.Y.Z]
#
# With no tag it takes the newest vX.Y.Z on origin. The checkout is moved to
# that tag (detached HEAD) so the bins are built from exactly the release --
# they then say "v3.1.2" rather than "v3.1.2-3-g…" and get archived for the
# flasher's version picker. Unsigned bins are refused unless --unsigned is
# given: boards can't install them over WiFi.
#
# Each step is written to deploy-status.json next to the bins, which the site
# serves (https://flasher.oillie.cloud/deploy-status.json, and the flasher
# page's footer): what is being deployed, how far it got, which boards failed
# -- so a redeploy that did not take shows on the site, not only in a
# terminal on the server.
set -euo pipefail

REPO=${REPO:-/root/MuleSkin-CYD}
COMPOSE_DIR=${COMPOSE_DIR:-/docker/muleskin-flasher}
SITE=${SITE:-http://flasher.oillie.cloud}
TAG=""; UNSIGNED=0
for a in "$@"; do
  case $a in
    --unsigned) UNSIGNED=1 ;;
    v[0-9]*.[0-9]*.[0-9]*) TAG=$a ;;
    *) echo "usage: $0 [vX.Y.Z] [--unsigned]" >&2; exit 2 ;;
  esac
done
STATUS_DIR="$REPO/.pio/flasher-bins"
STARTED=$(date -u +%Y-%m-%dT%H:%M:%SZ)
STAGE=starting; FAILED_BOARDS=""
# status <state> <message>: state is running, ok or failed.
status() {
  mkdir -p "$STATUS_DIR" 2>/dev/null || return 0
  # Backslashes and double quotes would break the JSON: / and ' instead.
  # Quoted patterns, which every bash reads alike (5.3 dropped the escaped one).
  local bs="\\" dq="\"" sq="'" sl="/"
  local msg=${2//"$bs"/"$sl"}; msg=${msg//"$dq"/"$sq"}
  local fb="" b
  for b in $FAILED_BOARDS; do fb="$fb${fb:+, }\"$b\""; done
  printf '{\n  "tag": "%s",\n  "state": "%s",\n  "stage": "%s",\n  "message": "%s",\n  "failed": [%s],\n  "started": "%s",\n  "updated": "%s"\n}\n' \
    "${TAG:-}" "$1" "$STAGE" "$msg" "$fb" "$STARTED" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    > "$STATUS_DIR/deploy-status.json.tmp" 2>/dev/null \
    && mv -f "$STATUS_DIR/deploy-status.json.tmp" "$STATUS_DIR/deploy-status.json" || true
}
stage() { STAGE=$1; echo "== $2"; status running "$2"; }
die() { echo "redeploy: $*" >&2; status failed "$*"; exit 1; }
trap 'status failed "a command failed at line $LINENO (stage $STAGE)"' ERR
[ "$(id -u)" = 0 ] || die "run it with sudo"
# One at a time. Two runs build in the same .pio directory inside docker and
# break each other's builds (2026-10-14: a second v3.6.0 run started while the
# first was building, and its build stage failed). The lock is held by fd 9
# until the script ends; the re-exec of the newest copy below takes it again.
if command -v flock >/dev/null; then
  exec 9>/run/muleskin-redeploy.lock
  flock -n 9 || { echo "redeploy: another redeploy is running -- wait for it to finish" >&2; exit 1; }
fi
if [ $UNSIGNED = 0 ]; then
  [ -n "${OTA_SIGNING_KEY:-}" ] || die "OTA_SIGNING_KEY is not set (or pass --unsigned)"
  [ -r "$OTA_SIGNING_KEY" ] || die "can't read $OTA_SIGNING_KEY"
fi

cd "$REPO"
stage fetch "fetching"
git fetch -q --tags origin
# The newest redeploy.sh does the deploy, whichever tag is being deployed.
# The copy on disk is the one the LAST redeploy checked out, and bash keeps
# running it after the checkout below -- so every run used the previous
# release's logic: its board count ("13" while deploying 15), and its bugs
# (the v3.6.0 deploy-status said "failed" on a good deploy, from a check
# fixed after the tag). origin/main's copy, run from a temp file, replaces
# this one once; REDEPLOY_NEWEST stops it doing so twice.
if [ -z "${REDEPLOY_NEWEST:-}" ]; then
  newest=$(mktemp /tmp/redeploy.XXXXXX)
  if git show origin/main:deploy/redeploy.sh > "$newest" 2>/dev/null && [ -s "$newest" ] && ! cmp -s "$newest" "$0"; then
    echo "== running the newest redeploy.sh (origin/main) instead of the copy on disk"
    REDEPLOY_NEWEST="$newest" exec bash "$newest" "$@"
  fi
  rm -f "$newest"
fi
# Running from that temp copy: bash has it open, so it can go now.
case "${REDEPLOY_NEWEST:-}" in /tmp/redeploy.*) rm -f -- "$REDEPLOY_NEWEST" ;; esac
if [ -z "$TAG" ]; then
  TAG=$(git tag -l 'v[0-9]*.[0-9]*.[0-9]*' | sort -V | tail -1)
fi
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null || die "no tag $TAG"
VER=${TAG#v}
stage checkout "checking out $TAG"
git -c advice.detachedHead=false checkout -q "$TAG"
grep -q "\"version\": *\"$VER\"" web-flasher/manifest-cyd-ili9341.json \
  || die "$TAG's manifests don't say $VER -- was it cut with tools/release.sh?"

# The count from the build script this tag builds with, not a number here.
BOARDS=$(sed -n 's/^ENVS="\(.*\)"$/\1/p' tools/build_flasher_bins.sh | wc -w)
stage build "building the bins (all $BOARDS boards, a few minutes)"
BUILD_LOG="$REPO/.pio/redeploy-build.log"
if [ $UNSIGNED = 1 ]; then OTA_SIGNING_KEY= tools/build_flasher_bins.sh 2>&1 | tee "$BUILD_LOG"
else tools/build_flasher_bins.sh 2>&1 | tee "$BUILD_LOG"; fi
# The build's last word: FAILED: none, or the boards (and "signatures") that failed.
FAILED_BOARDS=$(grep '^FAILED:' "$BUILD_LOG" | tail -1 | cut -d: -f2- | sed 's/^ *none *$//' || true)
[ -z "$FAILED_BOARDS" ] || status running "built, but not:$FAILED_BOARDS"
grep -q '"tag": *"'"$TAG"'"' .pio/flasher-bins/versions.json \
  || die "$TAG was not archived -- a board failed to build? (see .pio/build-*.log)"
# The top level holds $TAG now, and versions.json is rewritten to say so by
# origin/main's flasher_versions.py -- not whatever this tag's own build script
# did: one from before 2026-10-14 called the HIGHEST release the top level, so
# after a rollback the picker offered the old files under the new name.
echo "$TAG" > .pio/flasher-bins/root-tag
if command -v python3 >/dev/null && git show origin/main:tools/flasher_versions.py > /tmp/flasher_versions.py 2>/dev/null; then
  python3 /tmp/flasher_versions.py .pio/flasher-bins
  rm -f /tmp/flasher_versions.py
else
  echo "redeploy: no python3 or no tools/flasher_versions.py on main -- versions.json left as the build wrote it" >&2
fi

stage container "rebuilding the flasher container"
(cd "$COMPOSE_DIR" && docker compose up -d --build)

stage verify "checking the live site"
sleep 3
live=$(curl -fsS "$SITE/manifest-cyd-ili9341.json" | grep -o '"version": *"[^"]*"' | grep -o '[0-9][^"]*' || true)
[ "$live" = "$VER" ] || die "the site serves '${live:-nothing}', not $VER"
if [ $UNSIGNED = 0 ]; then
  code=$(curl -s -o /dev/null -w '%{http_code}' -r 0-0 "$SITE/cyd-ili9341-firmware.sig")
  case $code in 200|206) ;; *) die "the signature isn't served (HTTP $code)" ;; esac
fi
# grep -c, not -q: -q stops at the match and leaves curl writing into a closed
# pipe, which pipefail counts as a failure -- the v3.6.0 redeploy said
# "versions.json doesn't list v3.6.0" while the site listed it.
# -L as well: only the manifests and the firmware files are served on plain
# HTTP (for the boards; deploy/muleskin-flasher/docker-compose.yml), and
# versions.json is a 301 to HTTPS there. Without -L the check read the words
# "Moved Permanently" and reported a good deploy as failed -- every deploy
# from v3.6.0 to v3.7.0 (found 2026-10-14; the grep -q above was a red herring).
n=$(curl -fsSL "$SITE/versions.json" | grep -c "\"$TAG\"" || true)
[ "${n:-0}" -gt 0 ] || die "versions.json doesn't list $TAG"
# Every board's firmware, as the site serves it, carries this tag's version
# stamp (SQWVER:<tag>; ota_core.cpp) -- a bin left over from an older build
# would carry another, or none.
stage verify "checking every firmware is $TAG"
for m in web-flasher/manifest-*.json; do
  e=$(basename "$m" .json); e=${e#manifest-}
  # grep -c reads to the end: -q would stop at the match and leave curl
  # writing into a closed pipe, which pipefail counts as a failure.
  n=$(curl -fsS "$SITE/$e-firmware.bin" | grep -a -c "SQWVER:$TAG;" || true)
  [ "${n:-0}" -gt 0 ] || die "$e-firmware.bin on the site is not $TAG"
done
STAGE=done
status ok "flasher.oillie.cloud serves $TAG"
echo "== done: flasher.oillie.cloud serves $TAG"
