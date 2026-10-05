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
set -u
if [ ! -f /.dockerenv ]; then
  root=$(cd "$(dirname "$0")/.." && pwd)
  exec docker run --rm -v "$root:/src" -v pio-cache:/root/.platformio \
    python:3.12 bash /src/tools/build_flasher_bins.sh
fi
pip install -q platformio
cd /src
OUT=/src/.pio/flasher-bins
mkdir -p "$OUT"
ENVS="cyd cyd-fast cyd-ili9341 cyd-ili9341-fast cyd32c cyd35-fast freenove32 rlphantom-r awok twatch-s3 freenove-s3 crowpanel7 nm-cyd-c5"
failed=""
for e in $ENVS; do
  echo "=== building $e"
  if pio run -e "$e" > "$OUT/../build-$e.log" 2>&1; then
    B=.pio/build/$e
    cp "$B/firmware.bin" "$OUT/$e-firmware.bin"
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
