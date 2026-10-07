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
set -u
if [ ! -f /.dockerenv ]; then
  root=$(cd "$(dirname "$0")/.." && pwd)
  keyargs=()
  if [ -n "${OTA_SIGNING_KEY:-}" ]; then
    [ -r "$OTA_SIGNING_KEY" ] || { echo "OTA_SIGNING_KEY: can't read $OTA_SIGNING_KEY" >&2; exit 1; }
    keyargs=(-v "$(realpath "$OTA_SIGNING_KEY"):/run/ota-key.pem:ro" -e OTA_SIGNING_KEY=/run/ota-key.pem)
  fi
  exec docker run --rm -v "$root:/src" -v pio-cache:/root/.platformio "${keyargs[@]}" \
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
ENVS="cyd cyd-fast cyd-ili9341 cyd-ili9341-fast cyd32c cyd35-fast freenove32 rlphantom-r awok twatch-s3 freenove-s3 crowpanel7 nm-cyd-c5"
failed=""
for e in $ENVS; do
  echo "=== building $e"
  if pio run -e "$e" > "$OUT/../build-$e.log" 2>&1; then
    B=.pio/build/$e
    cp "$B/firmware.bin" "$OUT/$e-firmware.bin"
    # A .sig left over from an older build would make boards download the
    # new bin and then refuse it, so it goes either way.
    rm -f "$OUT/$e-firmware.sig"
    if [ -n "$KEY" ] && ! python tools/sign_firmware.py --key "$KEY" --pub "$PUB" --env "$e" \
        --bin "$OUT/$e-firmware.bin" --out "$OUT/$e-firmware.sig"; then
      rm -f "$OUT/$e-firmware.sig"
      echo "    SIGNING FAILED"; failed="$failed $e(sig)"; continue
    fi
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
ls -la "$OUT"
echo "FAILED:${failed:- none}"
