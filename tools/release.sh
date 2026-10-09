#!/bin/bash
# Cuts a release: every web-flasher manifest gets the new version, the commit
# is tagged, and (optionally) pushed and built.
#
#   tools/release.sh 3.1.2            # bump, commit, tag -- nothing leaves this machine
#   tools/release.sh 3.1.2 --push     # ...and push main and the tag
#   tools/release.sh 3.1.2 --push --build
#                                     # ...and build the signed bins (needs Docker and
#                                     #    OTA_SIGNING_KEY; run it on the server)
#   tools/release.sh 3.1.3 --name "Backup" --note "Settings backup in the flasher" \
#                          --note "Tap a blip for its details" --push
#                                     # release notes: a name (19 chars) and up to four
#                                     # lines (39 chars each) the boards show with the
#                                     # update notice, in System Properties
#
# The version is three numbers because that is all the boards compare
# (verNewer in src/ota_core.cpp): "3.1" or a commit hash never announces an
# update. The splash screen, the boot banner and the board's own update check
# all read the tag through FIRMWARE_VERSION, so the manifests are the only
# place a version is written by hand -- and this script writes them. The notes
# go in as "release_name" and "whats_new" (src/ota_wifi.cpp parseRelease());
# a release without them drops the previous release's.
set -euo pipefail
cd "$(dirname "$0")/.."

usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
die() { echo "release: $*" >&2; exit 1; }
[ $# -ge 1 ] || usage
VER=${1#v}; shift
PUSH=0; BUILD=0; NAME=""; NOTES=()
while [ $# -gt 0 ]; do
  case $1 in
    --push)  PUSH=1 ;;
    --build) BUILD=1 ;;
    --name)  [ $# -ge 2 ] || usage; NAME=$2; shift ;;
    --note)  [ $# -ge 2 ] || usage; NOTES+=("$2"); shift ;;
    *) usage ;;
  esac
  shift
done
TAG=v$VER

# What the boards can show: plain ASCII (the screen font has nothing else),
# and the sizes their buffers hold.
asciiOk() { LC_ALL=C; [[ "$1" =~ ^[\ -~]*$ ]]; }
[ ${#NAME} -le 19 ] || die "--name is ${#NAME} characters; the boards show 19"
asciiOk "$NAME" || die "--name has a character the boards can't draw (plain ASCII only)"
[ ${#NOTES[@]} -le 4 ] || die "${#NOTES[@]} notes; the boards show four"
for n in "${NOTES[@]+"${NOTES[@]}"}"; do
  [ ${#n} -le 39 ] || die "note \"$n\" is ${#n} characters; the boards show 39"
  asciiOk "$n" || die "note \"$n\" has a character the boards can't draw (plain ASCII only)"
done
jsonEsc() { local s=${1//\\/\\\\}; printf '%s' "${s//\"/\\\"}"; }
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
notes_json=""
if [ ${#NOTES[@]} -gt 0 ]; then
  for n in "${NOTES[@]}"; do notes_json+="${notes_json:+, }\"$(jsonEsc "$n")\""; done
fi
for m in "${manifests[@]}"; do
  grep -q '"version": *"[^"]*"' "$m" || die "$m has no \"version\" field"
  sed -i -E "s/(\"version\": *)\"[^\"]*\"/\1\"$VER\"/" "$m"
  # The last release's notes go; this one's go in under "version".
  sed -i -E '/^ *"(release_name|whats_new)":/d' "$m"
  ins=""
  [ -n "$NAME" ] && ins+="  \"release_name\": \"$(jsonEsc "$NAME")\","$'\n'
  [ -n "$notes_json" ] && ins+="  \"whats_new\": [$notes_json],"$'\n'
  if [ -n "$ins" ]; then
    # Through the environment, not awk -v, which would undo the escaping.
    INS="$ins" awk '{ print } /^ *"version":/ { printf "%s", ENVIRON["INS"] }' "$m" > "$m.tmp" && mv "$m.tmp" "$m"
  fi
  # The boards read the manifest into 1 KB.
  [ "$(wc -c < "$m")" -lt 1000 ] || die "$m is over 1000 bytes; the boards read 1 KB"
done
echo "stamped ${#manifests[@]} manifests with $VER${NAME:+ \"$NAME\"}${NOTES[0]+ and ${#NOTES[@]} notes}"

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

Next, on the server (see deploy/README.md) -- it builds, deploys and checks:
  sudo OTA_SIGNING_KEY=<private key .pem> /root/MuleSkin-CYD/deploy/redeploy.sh $TAG
EOF
