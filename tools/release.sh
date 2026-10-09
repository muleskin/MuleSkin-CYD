#!/bin/bash
# Cuts a release: every web-flasher manifest gets the new version, the commit
# is tagged, and (optionally) pushed and built.
#
#   tools/release.sh 3.1.2            # bump, commit, tag -- nothing leaves this machine
#   tools/release.sh 3.1.2 --push     # ...and push main and the tag
#   tools/release.sh 3.1.2 --push --build
#                                     # ...and build the signed bins (needs Docker and
#                                     #    OTA_SIGNING_KEY; run it on the server)
#
# The version is three numbers because that is all the boards compare
# (verNewer in src/ota_core.cpp): "3.1" or a commit hash never announces an
# update. The splash screen, the boot banner and the board's own update check
# all read the tag through FIRMWARE_VERSION, so the manifests are the only
# place a version is written by hand -- and this script writes them.
set -euo pipefail
cd "$(dirname "$0")/.."

usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 1 ] || usage
VER=${1#v}; shift
PUSH=0; BUILD=0
for a in "$@"; do
  case $a in
    --push)  PUSH=1 ;;
    --build) BUILD=1 ;;
    *) usage ;;
  esac
done
TAG=v$VER

die() { echo "release: $*" >&2; exit 1; }
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "'$VER' is not x.y.z"
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "not on main"
[ -z "$(git status --porcelain)" ] || die "the working tree has changes; commit or stash them first"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && die "$TAG already exists"

# Newer than every release so far, or the boards would never take it.
last=$(git tag -l 'v[0-9]*.[0-9]*.[0-9]*' | sed 's/^v//' | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
if [ -n "$last" ]; then
  newest=$(printf '%s\n%s\n' "$last" "$VER" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
  [ "$newest" = "$VER" ] && [ "$last" != "$VER" ] || die "$VER is not newer than v$last"
fi

shopt -s nullglob
manifests=(web-flasher/manifest-*.json)
[ ${#manifests[@]} -gt 0 ] || die "no web-flasher/manifest-*.json"
for m in "${manifests[@]}"; do
  grep -q '"version": *"[^"]*"' "$m" || die "$m has no \"version\" field"
  sed -i -E "s/(\"version\": *)\"[^\"]*\"/\1\"$VER\"/" "$m"
done
echo "stamped ${#manifests[@]} manifests with $VER"

git add -- "${manifests[@]}"
git commit -q -m "chore(release): $TAG"
git tag -a "$TAG" -m "MuleSkin-CYD $TAG"
echo "committed and tagged $TAG ($(git rev-parse --short HEAD))"

if [ $PUSH = 1 ]; then
  git push -q origin main
  git push -q origin "$TAG"
  echo "pushed main and $TAG"
fi

if [ $BUILD = 1 ]; then
  [ -n "${OTA_SIGNING_KEY:-}" ] || echo "warning: OTA_SIGNING_KEY not set -- the bins will be unsigned and boards can't install them over WiFi"
  tools/build_flasher_bins.sh
fi

cat <<EOF

Next, on the server (see deploy/README.md):
  sudo git -C /root/MuleSkin-CYD pull --ff-only origin main && sudo git -C /root/MuleSkin-CYD fetch --tags
  cd /root/MuleSkin-CYD && sudo OTA_SIGNING_KEY=<private key .pem> tools/build_flasher_bins.sh
  cd /docker/muleskin-flasher && sudo docker compose up -d --build
Then check: curl -s http://flasher.oillie.cloud/manifest-cyd-ili9341.json | grep version
EOF
