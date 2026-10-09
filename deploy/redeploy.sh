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
die() { echo "redeploy: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run it with sudo"
if [ $UNSIGNED = 0 ]; then
  [ -n "${OTA_SIGNING_KEY:-}" ] || die "OTA_SIGNING_KEY is not set (or pass --unsigned)"
  [ -r "$OTA_SIGNING_KEY" ] || die "can't read $OTA_SIGNING_KEY"
fi

cd "$REPO"
echo "== fetching"
git fetch -q --tags origin
if [ -z "$TAG" ]; then
  TAG=$(git tag -l 'v[0-9]*.[0-9]*.[0-9]*' | sort -V | tail -1)
fi
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null || die "no tag $TAG"
VER=${TAG#v}
echo "== checking out $TAG"
git -c advice.detachedHead=false checkout -q "$TAG"
grep -q "\"version\": *\"$VER\"" web-flasher/manifest-cyd-ili9341.json \
  || die "$TAG's manifests don't say $VER -- was it cut with tools/release.sh?"

echo "== building the bins (all 13 boards, a few minutes)"
if [ $UNSIGNED = 1 ]; then OTA_SIGNING_KEY= tools/build_flasher_bins.sh; else tools/build_flasher_bins.sh; fi
grep -q '"tag": *"'"$TAG"'"' .pio/flasher-bins/versions.json \
  || die "$TAG was not archived -- a board failed to build? (see .pio/build-*.log)"

echo "== rebuilding the flasher container"
(cd "$COMPOSE_DIR" && docker compose up -d --build)

echo "== checking the live site"
sleep 3
live=$(curl -fsS "$SITE/manifest-cyd-ili9341.json" | grep -o '"version": *"[^"]*"' | grep -o '[0-9][^"]*' || true)
[ "$live" = "$VER" ] || die "the site serves '${live:-nothing}', not $VER"
if [ $UNSIGNED = 0 ]; then
  code=$(curl -s -o /dev/null -w '%{http_code}' -r 0-0 "$SITE/cyd-ili9341-firmware.sig")
  case $code in 200|206) ;; *) die "the signature isn't served (HTTP $code)" ;; esac
fi
curl -fsS "$SITE/versions.json" | grep -q "\"$TAG\"" || die "versions.json doesn't list $TAG"
echo "== done: flasher.oillie.cloud serves $TAG"
