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
if [ $UNSIGNED = 0 ]; then
  [ -n "${OTA_SIGNING_KEY:-}" ] || die "OTA_SIGNING_KEY is not set (or pass --unsigned)"
  [ -r "$OTA_SIGNING_KEY" ] || die "can't read $OTA_SIGNING_KEY"
fi

cd "$REPO"
stage fetch "fetching"
git fetch -q --tags origin
if [ -z "$TAG" ]; then
  TAG=$(git tag -l 'v[0-9]*.[0-9]*.[0-9]*' | sort -V | tail -1)
fi
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null || die "no tag $TAG"
VER=${TAG#v}
stage checkout "checking out $TAG"
git -c advice.detachedHead=false checkout -q "$TAG"
grep -q "\"version\": *\"$VER\"" web-flasher/manifest-cyd-ili9341.json \
  || die "$TAG's manifests don't say $VER -- was it cut with tools/release.sh?"

stage build "building the bins (all 13 boards, a few minutes)"
BUILD_LOG="$REPO/.pio/redeploy-build.log"
if [ $UNSIGNED = 1 ]; then OTA_SIGNING_KEY= tools/build_flasher_bins.sh 2>&1 | tee "$BUILD_LOG"
else tools/build_flasher_bins.sh 2>&1 | tee "$BUILD_LOG"; fi
# The build's last word: FAILED: none, or the boards (and "signatures") that failed.
FAILED_BOARDS=$(grep '^FAILED:' "$BUILD_LOG" | tail -1 | cut -d: -f2- | sed 's/^ *none *$//' || true)
[ -z "$FAILED_BOARDS" ] || status running "built, but not:$FAILED_BOARDS"
grep -q '"tag": *"'"$TAG"'"' .pio/flasher-bins/versions.json \
  || die "$TAG was not archived -- a board failed to build? (see .pio/build-*.log)"

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
curl -fsS "$SITE/versions.json" | grep -q "\"$TAG\"" || die "versions.json doesn't list $TAG"
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
