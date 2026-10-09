#!/bin/bash
# Builds every board the web flasher lists and lays the bins out under the
# names the manifests use, in .pio/flasher-bins (mount that at /firmware in
# the web-flasher container).
#
#   tools/build_flasher_bins.sh
#
# Run on the host it re-launches itself inside python:3.12, with the
# PlatformIO toolchains cached in the "pio-cache" volume, so nothing needs
# installing locally.
#
# To sign the images for over-the-air updates, point OTA_SIGNING_KEY at the
# private half of include/ota_pubkey.h:
#
#   OTA_SIGNING_KEY=~/keys/ota.pem tools/build_flasher_bins.sh
#
# Each board then gets a <env>-firmware.sig next to its bin, checked against
# the public key before it is kept. Without the key the bins are still built,
# but boards report "not signed" when they check for an update.
#
# Built from a release tag (vX.Y.Z, exactly), the bins, signatures and
# manifests are also kept in .pio/flasher-bins/vX.Y.Z/, and versions.json is
# rewritten from those folders -- the flasher's "Firmware version" picker
# reads it, so older releases stay one click away. The newest five are kept.
#
# A LAB build -- a test build ahead of a release, for boards whose SYSTEM ->
# UPDATES is set to LAB:
#
#   LAB_VERSION=3.1.4 OTA_SIGNING_KEY=... tools/build_flasher_bins.sh
#
# builds the checkout as it is, reporting "v3.1.4-lab", and publishes ONLY
# lab-<env>-firmware.bin/.sig and manifest-lab-<env>.json (which the flasher
# page also offers at ?lab=1). The stable files, the archive and versions.json
# are left alone. The version must be newer than the boards' current release
# or they won't take it.
set -u
if [ ! -f /.dockerenv ]; then
  root=$(cd "$(dirname "$0")/.." && pwd)
  keyargs=()
  if [ -n "${OTA_SIGNING_KEY:-}" ]; then
    [ -r "$OTA_SIGNING_KEY" ] || { echo "OTA_SIGNING_KEY: can't read $OTA_SIGNING_KEY" >&2; exit 1; }
    keyargs=(-v "$(realpath "$OTA_SIGNING_KEY"):/run/ota-key.pem:ro" -e OTA_SIGNING_KEY=/run/ota-key.pem)
  fi
  tag=$(git -C "$root" describe --tags --exact-match 2>/dev/null || true)
  exec docker run --rm -v "$root:/src" -v pio-cache:/root/.platformio "${keyargs[@]}" \
    -e RELEASE_TAG="$tag" -e LAB_VERSION="${LAB_VERSION:-}" \
    python:3.12 bash /src/tools/build_flasher_bins.sh
fi
pip install -q platformio
cd /src
OUT=/src/.pio/flasher-bins
mkdir -p "$OUT"
KEY=${OTA_SIGNING_KEY:-}
PUB=/tmp/ota-pub.pem
if [ -n "$KEY" ]; then
  # The PEM quoted in ota_pubkey.h's comment -- what the boards trust.
  awk '/BEGIN PUBLIC KEY/,/END PUBLIC KEY/ { sub(/^\/\/ */, ""); print }' \
    include/ota_pubkey.h > "$PUB"
else
  echo "OTA_SIGNING_KEY not set: building unsigned bins"
fi
LAB=${LAB_VERSION:-}
PFX=""
if [ -n "$LAB" ]; then
  [[ "$LAB" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "LAB_VERSION must be x.y.z" >&2; exit 2; }
  export SQW_VERSION="v$LAB-lab"     # extra_script.py: what the build reports
  PFX="lab-"
  echo "LAB build: v$LAB-lab, publishing lab-* files only"
fi
ENVS="cyd cyd-fast cyd-ili9341 cyd-ili9341-fast cyd32c cyd35-fast freenove32 rlphantom-r awok twatch-s3 freenove-s3 crowpanel7 nm-cyd-c5"
failed=""
for e in $ENVS; do
  echo "=== building $e"
  if pio run -e "$e" > "$OUT/../build-$e.log" 2>&1; then
    B=.pio/build/$e
    cp "$B/firmware.bin" "$OUT/$PFX$e-firmware.bin"
    # A .sig left over from an older build would make boards download the
    # new bin and then refuse it, so it goes either way.
    rm -f "$OUT/$PFX$e-firmware.sig"
    if [ -n "$KEY" ] && ! python tools/sign_firmware.py --key "$KEY" --pub "$PUB" --env "$e" \
        --bin "$OUT/$PFX$e-firmware.bin" --out "$OUT/$PFX$e-firmware.sig"; then
      rm -f "$OUT/$PFX$e-firmware.sig"
      echo "    SIGNING FAILED"; failed="$failed $e(sig)"; continue
    fi
    # A lab build shares the stable bootloader and partition table.
    if [ -n "$LAB" ]; then echo "    ok"; continue; fi
    case $e in
      twatch-s3|freenove-s3|crowpanel7)
        cp "$B/bootloader.bin" "$OUT/esp32s3-bootloader.bin"
        cp "$B/partitions.bin" "$OUT/twatch-s3-partitions.bin" ;;
      nm-cyd-c5)
        cp "$B/bootloader.bin" "$OUT/esp32c5-bootloader.bin"
        cp "$B/partitions.bin" "$OUT/nm-cyd-c5-partitions.bin" ;;
      *)
        cp "$B/bootloader.bin" "$OUT/esp32-bootloader.bin"
        cp "$B/partitions.bin" "$OUT/esp32-partitions.bin" ;;
    esac
    echo "    ok"
  else
    echo "    FAILED (see .pio/build-$e.log)"; failed="$failed $e"
  fi
done
# otadata image (boot_app0.bin) from each framework
a=$(find /root/.platformio/packages -path '*framework-arduinoespressif32/tools/partitions/boot_app0.bin' | head -1)
[ -n "$a" ] && cp "$a" "$OUT/esp32-otadata.bin"
c=$(find /root/.platformio/packages -name boot_app0.bin -path '*arduino*' | grep -v "^$a$" | head -1)
cp "${c:-$a}" "$OUT/esp32c5-boot_app0.bin"

# The extra detection rules (web-flasher/signatures.txt), signed into
# manifest-signatures.json for the boards' boot check -- only with the key:
# boards ignore an unsigned set anyway.
if [ -f web-flasher/signatures.txt ] && [ -n "$KEY" ]; then
  python3 - web-flasher/signatures.txt /tmp/sigx-body.txt <<'PY'
import sys
serial, rules = 0, []
for line in open(sys.argv[1]):
    line = line.strip()
    if not line or line.startswith("#"):
        continue
    if line.upper().startswith("SERIAL "):
        serial = int(line.split()[1])
    else:
        rules.append(" ".join(line.split()))
body = ";".join(rules)
if not serial:
    sys.exit("signatures.txt: no SERIAL line")
if len(body) > 2000:
    sys.exit("signatures.txt: %d bytes of rules, the boards hold 2000" % len(body))
open(sys.argv[2], "w", newline="").write(body)
open(sys.argv[2] + ".serial", "w").write(str(serial))
PY
  if [ $? -eq 0 ] && python tools/sign_firmware.py --raw --key "$KEY" --pub "$PUB" --env signatures \
       --bin /tmp/sigx-body.txt --out /tmp/sigx.sig > /dev/null; then
    python3 - "$OUT/manifest-signatures.json" <<'PY'
import json, sys
body = open("/tmp/sigx-body.txt").read()
serial = int(open("/tmp/sigx-body.txt.serial").read())
sig = open("/tmp/sigx.sig", "rb").read().hex()
json.dump({"serial": serial, "body": body, "sig": sig}, open(sys.argv[1], "w"))
print("signatures: set %d, %d rules, signed" % (serial, body.count(";") + 1 if body else 0))
PY
  else
    echo "signatures: NOT published (see above)"; failed="$failed signatures"
  fi
fi

# A lab build: its manifests, from the stable ones, and nothing else.
if [ -n "$LAB" ]; then
  python3 - "$OUT" "$LAB" $ENVS <<'PY'
import json, os, sys
out, ver, envs = sys.argv[1], sys.argv[2], sys.argv[3:]
for e in envs:
    if not os.path.exists(os.path.join(out, "lab-%s-firmware.bin" % e)):
        continue
    with open("web-flasher/manifest-%s.json" % e) as f:
        m = json.load(f)
    m["version"] = ver
    m["name"] = "LAB -- " + m.get("name", e)
    m.pop("whats_new", None)
    m["release_name"] = "LAB BUILD"
    for b in m.get("builds", []):
        for p in b.get("parts", []):
            if p.get("path") == "%s-firmware.bin" % e:
                p["path"] = "lab-%s-firmware.bin" % e
    with open(os.path.join(out, "manifest-lab-%s.json" % e), "w") as f:
        json.dump(m, f, indent=2)
print("lab manifests written for", ver)
PY
  ls -la "$OUT"/*lab*
  echo "FAILED:${failed:- none}"
  exit 0
fi

# The release archive and the version list (see the top of this file).
TAG=${RELEASE_TAG-$(git -c safe.directory=/src describe --tags --exact-match 2>/dev/null || true)}
if [[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] && [ -z "$failed" ]; then
  rm -rf "$OUT/$TAG"; mkdir -p "$OUT/$TAG"
  cp "$OUT"/*.bin "$OUT/$TAG/"
  cp "$OUT"/*.sig "$OUT/$TAG/" 2>/dev/null || true
  cp web-flasher/manifest-*.json "$OUT/$TAG/"
  echo "archived $TAG"
elif [ -n "$TAG" ]; then
  echo "not archived: ${failed:+a board failed}${failed:-$TAG is not a vX.Y.Z tag}"
fi
python3 - "$OUT" <<'PY'
import json, os, re, shutil, sys
out = sys.argv[1]
key = lambda t: tuple(int(x) for x in t[1:].split("."))
tags = sorted((d for d in os.listdir(out)
               if re.fullmatch(r"v\d+\.\d+\.\d+", d) and os.path.isdir(os.path.join(out, d))),
              key=key, reverse=True)
for old in tags[5:]:
    shutil.rmtree(os.path.join(out, old))
tags = tags[:5]
# The newest is the one at the root, which is what boards update from.
versions = [{"tag": t, "dir": "" if i == 0 else t + "/"} for i, t in enumerate(tags)]
with open(os.path.join(out, "versions.json"), "w") as f:
    json.dump({"versions": versions}, f, indent=2)
print("versions.json:", ", ".join(tags) or "(no releases archived)")
PY
ls -la "$OUT"
echo "FAILED:${failed:- none}"
